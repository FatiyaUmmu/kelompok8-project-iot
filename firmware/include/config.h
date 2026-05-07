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

// ===================== PIN DEFINITIONS =====================
#define TRIG_GATE  27
#define ECHO_GATE  14
#define TRIG_SLOT  26
#define ECHO_SLOT  25
#define SS_PIN     5
#define RST_PIN    17
#define MQ2_PIN    34
#define SERVO_PIN  13
#define BUZZER_PIN 12

// ===================== CONSTANTS =====================
#define SLOT_TOTAL       2
#define DISTANCE_THRESH 50.0
#define GAS_THRESHOLD   2000
#define TARIF_PER_JAM    2000

// ===================== WIFI & MQTT CONFIG =====================
const char* ssid = "Wokwi-GUEST"; 
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const char* topic_data = "parking/2330105030023/data";
const char* topic_alert = "parking/2330105030023/alert";
const char* topic_control = "parking/2330105030023/control"; 

// ===================== WHITELIST RFID =====================
const int JUMLAH_KARTU_TERDAFTAR = 3;
String kartuTerdaftar[JUMLAH_KARTU_TERDAFTAR] = {
  "11:22:33:44",
  "aa:bb:cc:dd",
  "55:66:77:88"
};

// ===================== GLOBAL OBJECTS =====================
LiquidCrystal_I2C lcd(0x27, 16, 2); 
RTC_DS1307 rtc;                     
MFRC522 rfid(SS_PIN, RST_PIN);
Servo portalServo;
WiFiClient espClient;
PubSubClient client(espClient);

// ===================== DATA STRUCTURE =====================
struct DataKendaraan {
  String uid;
  bool isParked;
  DateTime waktuMasuk;
};
DataKendaraan parkir[SLOT_TOTAL];

int availableSlots = SLOT_TOTAL;
bool isPortalOpen = false;

unsigned long lcdMessageTimer = 0;
unsigned long serialTimer = 0;
bool showRfidMessage = false;
