/*
 * Smart Parking System - Lantai 1 (3 Slot)
 * Fitur: RFID, Servo, Buzzer, LCD, Sensor Gas, RTC, MQTT
 * Ditambahkan: Gate detection (mobil mendekat) + info slot kosong
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

// ===================== IDENTITAS LANTAI =====================
#define LANTAI 3

// ===================== PIN DEFINISI =====================
// Sensor gerbang (mobil mendekat)
#define TRIG_GATE   27
#define ECHO_GATE   14

// Sensor slot (3 slot)
#define TRIG_SLOT_A  26
#define ECHO_SLOT_A  25
#define TRIG_SLOT_B  33
#define ECHO_SLOT_B  32
#define TRIG_SLOT_C  16
#define ECHO_SLOT_C  17

// RFID
#define SS_PIN       5
#define RST_PIN      4

// Sensor gas
#define GAS_PIN      34

// LED indikator (hijau=ada slot kosong, merah=penuh)
#define LED_HIJAU    15
#define LED_MERAH    2

// Servo dan buzzer
#define SERVO_PIN    13
#define BUZZER_PIN   12

// LCD dan RTC menggunakan I2C (pin 21=SDA, 22=SCL)

// ===================== KONSTANTA =====================
#define DISTANCE_THRESH  50.0     // cm, jika jarak < 50 dianggap mobil mendekat
#define GAS_THRESHOLD    2000     // ADC
#define TARIF_JAM_PERTAMA    5000
#define TARIF_JAM_SELANJUT   2000
#define SLOT_TOTAL  3
#define DEMO_SCALE_MS_PER_JAM 60000  // 1 menit = 1 jam (demo)

// ===================== WIFI & MQTT =====================
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";

String baseTopic    = "parking/lantai" + String(LANTAI);
String topicStatus  = baseTopic + "/status";
String topicAlert   = baseTopic + "/alert";
String topicEvent   = baseTopic + "/event";
String topicControl = "parking/control";

// ===================== WHITELIST RFID =====================
const int JUMLAH_KARTU_TERDAFTAR = 3;
String kartuTerdaftar[JUMLAH_KARTU_TERDAFTAR] = {
  "11:22:33:44",
  "aa:bb:cc:dd",
  "55:66:77:88"
};

// ===================== OBJEK GLOBAL =====================
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS1307 rtc;
MFRC522 rfid(SS_PIN, RST_PIN);
Servo portalServo;
WiFiClient espClient;
PubSubClient client(espClient);

struct Slot {
  bool isTerisi;
  String uid;
  unsigned long waktuMasukMillis;
};
Slot slot[SLOT_TOTAL];
int slotTerisi = 0;

unsigned long lastPublish = 0;
unsigned long lastLCD     = 0;
bool showTempMsg          = false;
unsigned long tempMsgTimer = 0;

// ===================== GATE DETECTION =====================
unsigned long lastGateCheck = 0;
bool lastCarDetected = false;

// ===================== PROTOTIPE =====================
float readUltrasonic(int trig, int echo);
float readGateDistance();
void displayLCD(String l1, String l2);
void updateLED();
void bukaPortal(int durasi);
long hitungBiaya(unsigned long masuk, unsigned long keluar);
int  hitungJam(unsigned long masuk, unsigned long keluar);
void publishStatus();
void updateLCDDisplay();
void handleRFID();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void setupWiFi();
void setupMQTT();

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- SMART PARKING LANTAI " + String(LANTAI) + " ---");

  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  displayLCD("Lantai " + String(LANTAI), "Starting...");

  // RTC
  if (!rtc.begin()) {
    displayLCD("RTC Error!", "Cek I2C"); 
    while(1);
  }
  if (!rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // RFID
  SPI.begin(18, 19, 23, 5);
  rfid.PCD_Init();

  // Servo
  portalServo.attach(SERVO_PIN);
  portalServo.write(0);
  
  // Pin mode
  pinMode(TRIG_GATE, OUTPUT); pinMode(ECHO_GATE, INPUT);
  pinMode(TRIG_SLOT_A, OUTPUT); pinMode(ECHO_SLOT_A, INPUT);
  pinMode(TRIG_SLOT_B, OUTPUT); pinMode(ECHO_SLOT_B, INPUT);
  pinMode(TRIG_SLOT_C, OUTPUT); pinMode(ECHO_SLOT_C, INPUT);
  pinMode(BUZZER_PIN, OUTPUT); noTone(BUZZER_PIN);
  pinMode(LED_HIJAU, OUTPUT); pinMode(LED_MERAH, OUTPUT);

  // Inisialisasi slot kosong
  for (int i=0; i<SLOT_TOTAL; i++) {
    slot[i].isTerisi = false;
    slot[i].uid = "";
  }
  slotTerisi = 0;
  updateLED();

  // WiFi
  setupWiFi();

  // MQTT
  setupMQTT();

  displayLCD("Lantai " + String(LANTAI), "Tap Kartu RFID");
  tone(BUZZER_PIN, 2500, 100);
  delay(150);
  tone(BUZZER_PIN, 2500, 100);
  Serial.println("Sistem siap. Gate detection aktif.");
}

void setupWiFi() {
  Serial.print("Menghubungkan WiFi");
  WiFi.begin(ssid, password);
  int tryCount = 0;
  while (WiFi.status() != WL_CONNECTED && tryCount < 30) {
    delay(500);
    Serial.print(".");
    tryCount++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi terhubung! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi GAGAL! Lanjut tanpa WiFi?");
  }
}

void setupMQTT() {
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
}

// ===================== LOOP =====================
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  // ========== GATE DETECTION (mobil mendekat) ==========
  if (millis() - lastGateCheck > 500) {
    float jarak = readGateDistance();
    bool carNear = (jarak > 0 && jarak < DISTANCE_THRESH);

    if (carNear && !lastCarDetected) {
      Serial.println("[GATE] Mobil mendekat! Jarak: " + String(jarak) + " cm");
      
      int slotKosong = SLOT_TOTAL - slotTerisi;
      String infoKosong = (slotKosong > 0) ? ("Tersedia " + String(slotKosong) + " slot") : "PENUH!";
      
      // Tampilkan di LCD (sementara)
      displayLCD("Mobil Mendekat!", infoKosong);
      showTempMsg = true;
      tempMsgTimer = millis();
      
      // Buzzer singkat
      tone(BUZZER_PIN, 1000, 200);
      
      // Kirim notifikasi MQTT
      StaticJsonDocument<150> gateDoc;
      gateDoc["lantai"] = LANTAI;
      gateDoc["jarak_cm"] = jarak;
      gateDoc["tersedia"] = slotKosong;
      char gateBuf[150];
      serializeJson(gateDoc, gateBuf);
      client.publish("parking/gate/detected", gateBuf);
    }
    lastCarDetected = carNear;
    lastGateCheck = millis();
  }

  // ========== Sensor Gas ==========
  int gasADC = analogRead(GAS_PIN);
  int gasPercent = map(gasADC, 0, 4095, 0, 100);
  if (gasADC > GAS_THRESHOLD) {
    tone(BUZZER_PIN, 2000);
    StaticJsonDocument<100> alertDoc;
    alertDoc["gas_persen"] = gasPercent;
    alertDoc["lantai"] = LANTAI;
    char buf[100];
    serializeJson(alertDoc, buf);
    client.publish(topicAlert.c_str(), buf);
    Serial.println("[ALERT] Gas: " + String(gasPercent) + "%");
    if (!showTempMsg) displayLCD("GAS BAHAYA!", "Evakuasi!");
  } else {
    noTone(BUZZER_PIN);
  }

  // ========== RFID Handler ==========
  handleRFID();

  // ========== Publish status periodik ==========
  if (millis() - lastPublish > 3000) {
    publishStatus();
    lastPublish = millis();
    Serial.println("--- LANTAI " + String(LANTAI) + " ---");
    for (int i=0; i<SLOT_TOTAL; i++) {
      Serial.printf("Slot %c: %s\n", 'A'+i, slot[i].isTerisi ? "TERISI" : "KOSONG");
    }
    Serial.printf("Gas: %d%% | Terisi: %d/%d\n", gasPercent, slotTerisi, SLOT_TOTAL);
  }

  // ========== Update LCD jika tidak ada pesan sementara ==========
  if (millis() - lastLCD > 1500 && !showTempMsg) {
    updateLCDDisplay();
    lastLCD = millis();
  }
  if (showTempMsg && millis() - tempMsgTimer > 3000) {
    showTempMsg = false;
    updateLCDDisplay();
  }
  delay(50);
}

// ===================== FUNGSI GATE =====================
float readGateDistance() {
  digitalWrite(TRIG_GATE, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_GATE, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_GATE, LOW);
  long duration = pulseIn(ECHO_GATE, HIGH, 30000);
  if (duration == 0) return 0.0;
  return duration * 0.0343 / 2.0;
}

// ===================== RFID HANDLER =====================
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidStr += ":";
  }
  uidStr.toLowerCase();

  Serial.println("[RFID] Kartu: " + uidStr);
  showTempMsg = true;
  tempMsgTimer = millis();

  // Validasi whitelist
  bool valid = false;
  for (int i = 0; i < JUMLAH_KARTU_TERDAFTAR; i++) {
    if (uidStr == kartuTerdaftar[i]) { valid = true; break; }
  }
  if (!valid) {
    displayLCD("Kartu Tak Valid", "Akses Ditolak");
    tone(BUZZER_PIN, 500, 1000);
    rfid.PICC_HaltA();
    return;
  }

  // Cek apakah sudah parkir (proses keluar)
  int idxSlot = -1;
  for (int i = 0; i < SLOT_TOTAL; i++) {
    if (slot[i].isTerisi && slot[i].uid == uidStr) {
      idxSlot = i;
      break;
    }
  }

  unsigned long nowMillis = millis();

  // KELUAR
  if (idxSlot != -1) {
    int jam = hitungJam(slot[idxSlot].waktuMasukMillis, nowMillis);
    long biaya = hitungBiaya(slot[idxSlot].waktuMasukMillis, nowMillis);
    slot[idxSlot].isTerisi = false;
    slot[idxSlot].uid = "";
    slotTerisi--;
    updateLED();

    Serial.printf("[KELUAR] Slot %c | Durasi %d jam | Bayar Rp%ld\n", 'A'+idxSlot, jam, biaya);
    displayLCD("Keluar Slot "+String(char('A'+idxSlot)), "Rp"+String(biaya));
    tone(BUZZER_PIN, 2000, 150); delay(200); tone(BUZZER_PIN, 2000, 150);
    bukaPortal(3000);

    StaticJsonDocument<200> doc;
    doc["event"] = "keluar";
    doc["lantai"] = LANTAI;
    doc["slot"] = String(char('A'+idxSlot));
    doc["uid"] = uidStr;
    doc["jam"] = jam;
    doc["biaya"] = biaya;
    char buf[200];
    serializeJson(doc, buf);
    client.publish(topicEvent.c_str(), buf);
  }
  // MASUK
  else {
    int slotKosong = -1;
    for (int i = 0; i < SLOT_TOTAL; i++) {
      if (!slot[i].isTerisi) { slotKosong = i; break; }
    }
    if (slotKosong != -1) {
      slot[slotKosong].isTerisi = true;
      slot[slotKosong].uid = uidStr;
      slot[slotKosong].waktuMasukMillis = nowMillis;
      slotTerisi++;
      updateLED();

      Serial.printf("[MASUK] Lantai %d Slot %c\n", LANTAI, 'A'+slotKosong);
      displayLCD("Slot "+String(char('A'+slotKosong))+" Masuk", "Silakan Parkir");
      tone(BUZZER_PIN, 1500, 150); delay(200); tone(BUZZER_PIN, 1500, 150);
      bukaPortal(3000);

      StaticJsonDocument<200> doc;
      doc["event"] = "masuk";
      doc["lantai"] = LANTAI;
      doc["slot"] = String(char('A'+slotKosong));
      doc["uid"] = uidStr;
      char buf[200];
      serializeJson(doc, buf);
      client.publish(topicEvent.c_str(), buf);
    } else {
      displayLCD("Parkir Penuh!", "Tidak ada slot");
      tone(BUZZER_PIN, 800, 1000);
    }
  }
  rfid.PICC_HaltA();
}

// ===================== HITUNG TARIF =====================
int hitungJam(unsigned long masuk, unsigned long keluar) {
  unsigned long durasiMs = keluar - masuk;
  int jam = durasiMs / DEMO_SCALE_MS_PER_JAM;
  if (durasiMs % DEMO_SCALE_MS_PER_JAM > 0) jam++;
  if (jam == 0) jam = 1;
  return jam;
}

long hitungBiaya(unsigned long masuk, unsigned long keluar) {
  int jam = hitungJam(masuk, keluar);
  if (jam == 1) return TARIF_JAM_PERTAMA;
  return TARIF_JAM_PERTAMA + (long)(jam - 1) * TARIF_JAM_SELANJUT;
}

// ===================== PUBLISH MQTT =====================
void publishStatus() {
  StaticJsonDocument<256> doc;
  doc["lantai"] = LANTAI;
  doc["slot_total"] = SLOT_TOTAL;
  doc["slot_terisi"] = slotTerisi;
  doc["slot_tersedia"] = SLOT_TOTAL - slotTerisi;
  doc["slotA"] = slot[0].isTerisi ? 1 : 0;
  doc["slotB"] = slot[1].isTerisi ? 1 : 0;
  doc["slotC"] = slot[2].isTerisi ? 1 : 0;
  doc["gas_persen"] = map(analogRead(GAS_PIN), 0, 4095, 0, 100);
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(topicStatus.c_str(), buffer);
  client.publish((baseTopic + "/slot/A").c_str(), slot[0].isTerisi ? "1" : "0");
  client.publish((baseTopic + "/slot/B").c_str(), slot[1].isTerisi ? "1" : "0");
  client.publish((baseTopic + "/slot/C").c_str(), slot[2].isTerisi ? "1" : "0");
}

// ===================== MQTT =====================
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("[MQTT] Menghubungkan...");
    String clientId = "ESP32-L" + String(LANTAI) + "-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" OK");
      client.subscribe(topicControl.c_str());
    } else {
      Serial.println(" Gagal, coba lagi 3 detik");
      delay(3000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("[MQTT IN] " + String(topic) + " -> " + msg);

  if (msg == "OPEN_GATE") {
    bukaPortal(5000);
    displayLCD("Manual Override", "Portal Terbuka");
    showTempMsg = true;
    tempMsgTimer = millis();
  } else if (msg == "RESET") {
    for (int i = 0; i < SLOT_TOTAL; i++) {
      slot[i].isTerisi = false;
      slot[i].uid = "";
    }
    slotTerisi = 0;
    updateLED();
    displayLCD("System Reset", "Slot Dikosongkan");
    tone(BUZZER_PIN, 2500, 300);
    showTempMsg = true;
    tempMsgTimer = millis();
  }
}

// ===================== LCD =====================
void updateLCDDisplay() {
  int kosong = SLOT_TOTAL - slotTerisi;
  int gas = map(analogRead(GAS_PIN), 0, 4095, 0, 100);
  String line1 = "L" + String(LANTAI) + " Sisa:" + String(kosong) + "/" + String(SLOT_TOTAL);
  String line2 = "A:" + String(slot[0].isTerisi ? "ISI" : "OK") + " " +
                 "B:" + String(slot[1].isTerisi ? "ISI" : "OK") + " " +
                 "C:" + String(slot[2].isTerisi ? "ISI" : "OK");
  displayLCD(line1, line2);
}

void displayLCD(String l1, String l2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

// ===================== LED & SERVO =====================
void updateLED() {
  bool penuh = (slotTerisi == SLOT_TOTAL);
  digitalWrite(LED_HIJAU, penuh ? LOW : HIGH);
  digitalWrite(LED_MERAH, penuh ? HIGH : LOW);
}

void bukaPortal(int durasi) {
  Serial.println("[SERVO] Portal buka");
  portalServo.write(90);
  delay(durasi);
  portalServo.write(0);
  Serial.println("[SERVO] Portal tutup");
}

// ===================== ULTRASONIC UNTUK SLOT (opsional) =====================
float readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 25000);
  if (duration == 0) return 0.0;
  return duration * 0.0343 / 2.0;
}
