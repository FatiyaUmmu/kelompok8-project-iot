/*
 * Smart Parking System with Gas Monitoring & Automated Billing
 * Kelompok 8 - Firmware v0.1 (RFID Whitelist & MQTT Control)
 * Board: ESP32 Dev Module
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===================== FUNCTION PROTOTYPES =====================
float readUltrasonic(int trigPin, int echoPin);
void displayLCD(String line1, String line2);
void checkGas(int adcValue, int percentage);
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length); 

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- MEMULAI SISTEM PARKIR ---");

  Wire.begin(21, 22); 

  lcd.init();
  lcd.backlight();
  displayLCD("Smart Parking", "Menghubungkan...");

  setupWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback); 

  // ----------------- RTC DS1307 -----------------
  if (!rtc.begin()) {
    Serial.println("❌ RTC tidak terdeteksi!");
    displayLCD("RTC Error!", "Cek Kabel I2C");
    while(1);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // ----------------- RFID MFRC522 -----------------
  SPI.begin(18, 19, 23, 5); 
  rfid.PCD_Init();

  // ----------------- Servo & I/O -----------------
  portalServo.attach(SERVO_PIN);
  portalServo.write(0);  
  
  pinMode(TRIG_GATE, OUTPUT);
  pinMode(ECHO_GATE, INPUT);
  pinMode(TRIG_SLOT, OUTPUT);
  pinMode(ECHO_SLOT, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN); 

  // Inisialisasi array parkir
  for(int i = 0; i < SLOT_TOTAL; i++) {
    parkir[i].isParked = false;
    parkir[i].uid = "";
  }

  lcd.clear();
}

// ===================== MAIN LOOP =====================
void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop(); 

  // 1. Baca Sensor Ultrasonik & Gas
  float distanceGate = readUltrasonic(TRIG_GATE, ECHO_GATE);
  float distanceSlot = readUltrasonic(TRIG_SLOT, ECHO_SLOT);
  bool isSlotFisikTerisi = (distanceSlot > 0 && distanceSlot < DISTANCE_THRESH);
  
  int gasValue = analogRead(MQ2_PIN);
  int gasPercent = map(gasValue, 0, 4095, 0, 100); 
  checkGas(gasValue, gasPercent);

  DateTime now = rtc.now();
  String rtcTime = String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());

  // 2. Output Serial Monitor & Publish MQTT (Setiap 3 detik)
  if (millis() - serialTimer > 3000) {
    Serial.println("----------------------------------------");
    Serial.println("DATA SENSOR - SMART PARKING");
    Serial.println("----------------------------------------");
    Serial.printf("1. Ketersediaan Slot : %d dari %d slot\n", availableSlots, SLOT_TOTAL);
    Serial.printf("2. Sensor Slot Fisik : %s (Jarak: %.2f cm)\n", isSlotFisikTerisi ? "TERISI" : "KOSONG", distanceSlot);
    Serial.printf("3. Sensor Gerbang    : %s (Jarak: %.2f cm)\n", (distanceGate > 0 && distanceGate < DISTANCE_THRESH) ? "ADA MOBIL" : "KOSONG", distanceGate);
    Serial.printf("4. Gas MQ-2          : %d ADC (%d%%)\n", gasValue, gasPercent);
    Serial.printf("5. Waktu RTC         : %s\n", rtcTime.c_str());
    Serial.printf("6. Status Portal     : %s\n", isPortalOpen ? "TERBUKA" : "TERTUTUP");
    Serial.printf("7. Status MQTT       : %s\n\n", client.connected() ? "TERHUBUNG" : "TERPUTUS");
    
    // --- PUBLISH PAYLOAD JSON ---
    StaticJsonDocument<200> doc;
    doc["slot_tersedia"] = availableSlots;
    doc["slot_fisik"] = isSlotFisikTerisi ? "TERISI" : "KOSONG";
    doc["gas_percent"] = gasPercent;
    doc["waktu"] = rtcTime;
    
    char jsonBuffer[200];
    serializeJson(doc, jsonBuffer);
    client.publish(topic_data, jsonBuffer);
    
    serialTimer = millis();
  }

  // 3. Baca RFID & Logika Masuk/Keluar
  String uidStr = "none";
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    uidStr = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) uidStr += "0"; 
      uidStr += String(rfid.uid.uidByte[i], HEX);
      if (i < rfid.uid.size - 1) uidStr += ":";
    }
    uidStr.toLowerCase(); 

    showRfidMessage = true;
    lcdMessageTimer = millis(); 

    // ---- CEK APAKAH KARTU TERDAFTAR (WHITELIST) ----
    bool isKartuValid = false;
    for (int i = 0; i < JUMLAH_KARTU_TERDAFTAR; i++) {
      if (uidStr == kartuTerdaftar[i]) {
        isKartuValid = true;
        break;
      }
    }

    if (isKartuValid) {
      // Cari apakah mobil sudah ada di dalam area parkir
      int indexKendaraan = -1;
      for (int i = 0; i < SLOT_TOTAL; i++) {
        if (parkir[i].isParked && parkir[i].uid == uidStr) {
          indexKendaraan = i; 
          break;
        }
      }

      // LOGIKA MASUK
      if (indexKendaraan == -1) { 
        if (availableSlots > 0) {
          for (int i = 0; i < SLOT_TOTAL; i++) {
            if (!parkir[i].isParked) {
              parkir[i].uid = uidStr;
              parkir[i].isParked = true;
              parkir[i].waktuMasuk = rtc.now();
              break;
            }
          }
          availableSlots--; 
          Serial.println("\n>>> [IN] KENDARAAN MASUK <<<");
          Serial.println("UID: " + uidStr);
          displayLCD("Akses Diterima", "Silakan Masuk");
          tone(BUZZER_PIN, 1500, 150); delay(200); tone(BUZZER_PIN, 1500, 150);

          isPortalOpen = true;    
          portalServo.write(90);  
          delay(3000); 
          portalServo.write(0);   
          isPortalOpen = false;   
        } else {
          Serial.println("\n>>> [REJECT] PARKIR PENUH <<<");
          displayLCD("Parkir Penuh!", "Akses Ditolak");
          tone(BUZZER_PIN, 500, 1000); 
        }
      } 
      // LOGIKA KELUAR
      else { 
        DateTime waktuKeluar = rtc.now();
        TimeSpan durasi = waktuKeluar - parkir[indexKendaraan].waktuMasuk;
        
        int jamParkir = durasi.hours();
        if (durasi.minutes() > 0 || durasi.seconds() > 0) jamParkir++;
        if (jamParkir == 0) jamParkir = 1; 
        
        long totalBiaya = jamParkir * TARIF_PER_JAM;
        
        parkir[indexKendaraan].isParked = false;
        parkir[indexKendaraan].uid = "";
        availableSlots++; 
        
        Serial.println("\n>>> [OUT] KENDARAAN KELUAR <<<");
        Serial.println("UID: " + uidStr);
        Serial.printf("Tagihan : Rp %ld\n", totalBiaya);
        
        String costStr = "Rp " + String(totalBiaya);
        displayLCD("Keluar. Biaya:", costStr);
        tone(BUZZER_PIN, 2000, 150); delay(200); tone(BUZZER_PIN, 2000, 150);

        isPortalOpen = true;    
        portalServo.write(90);  
        delay(3000); 
        portalServo.write(0);   
        isPortalOpen = false; 
      }
    } 
    // JIKA KARTU TIDAK TERDAFTAR
    else {
      Serial.println("\n>>> [REJECT] KARTU ILEGAL <<<");
      Serial.println("UID " + uidStr + " tidak terdaftar di sistem!");
      displayLCD("UID Tak Dikenal", "Akses Ditolak");
      tone(BUZZER_PIN, 500, 1000); 
    }
    rfid.PICC_HaltA(); 
  }

  // 4. Update LCD 
  if (showRfidMessage && (millis() - lcdMessageTimer > 3000)) showRfidMessage = false; 

  if (!showRfidMessage) {
    String lcdLine1 = "Sisa Slot: " + String(availableSlots);
    String lcdLine2 = "Gas: " + String(gasPercent) + "%";
    
    if (gasValue > GAS_THRESHOLD) {
       lcdLine1 = "PERINGATAN GAS!";
       lcdLine2 = "Level: " + String(gasPercent) + "%";
    }
    displayLCD(lcdLine1, lcdLine2);
  }
  delay(50); 
}

// ===================== HELPER FUNCTIONS =====================
void setupWiFi() {
  delay(10);
  Serial.print("\nMenghubungkan ke WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Terhubung!");
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT Broker...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("✅ Berhasil terhubung ke Broker MQTT!");
      
      client.subscribe(topic_control);
      Serial.println("   --> Mendengarkan topik: " + String(topic_control));
      
    } else {
      Serial.print("❌ Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" Coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// Fungsi Callback MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("\n[MQTT] Pesan diterima di topik: ");
  Serial.println(topic);
  
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("[MQTT] Isi Pesan: ");
  Serial.println(message);

  if (message == "OPEN_GATE") {
    Serial.println(">>> OVERRIDE KONTROL: MEMBUKA PORTAL SECARA MANUAL <<<");
    displayLCD("Manual Override", "Portal Terbuka");
    tone(BUZZER_PIN, 1000, 500); 
    
    isPortalOpen = true;    
    portalServo.write(90);  
    delay(5000);            
    portalServo.write(0);   
    isPortalOpen = false;
    
    showRfidMessage = true;
    lcdMessageTimer = millis();
  } 
  else if (message == "RESET") {
    Serial.println(">>> OVERRIDE KONTROL: RESET SISTEM PARKIR <<<");
    displayLCD("Sistem Di-Reset", "Slot Dikosongkan");
    
    availableSlots = SLOT_TOTAL;
    
    for(int i = 0; i < SLOT_TOTAL; i++) {
      parkir[i].isParked = false;
      parkir[i].uid = "";
    }

    tone(BUZZER_PIN, 2500, 100); delay(150);
    tone(BUZZER_PIN, 2500, 100); delay(150);
    tone(BUZZER_PIN, 2500, 300);

    showRfidMessage = true;
    lcdMessageTimer = millis();
  }
}

float readUltrasonic(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 0.0; 
  return duration * 0.0343 / 2.0;
}

void checkGas(int adcValue, int percentage) {
  if (adcValue > GAS_THRESHOLD) {
    tone(BUZZER_PIN, 2000); 
    StaticJsonDocument<100> alertDoc;
    alertDoc["status"] = "BAHAYA";
    alertDoc["gas_level"] = percentage;
    char alertBuffer[100];
    serializeJson(alertDoc, alertBuffer);
    client.publish(topic_alert, alertBuffer);
  } else {
    noTone(BUZZER_PIN);     
  }
}

void displayLCD(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(line1);
  lcd.setCursor(0,1);
  lcd.print(line2);
}
