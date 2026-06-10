# Smart Parking System with Gas Monitoring & Automated Billing

**Kelompok 8 - Proyek Internet of Things (IoT)**
**JURUSAN TEKNIK INFORMATIKA, FAKULTAS TEKNIK, UNIVERSITAS PALANGKA RAYA, 2026**

## Anggota Kelompok
- **Agatha Monalisa** (2330105030008)
- **Muhammad Rony Kurniawan** (2330105030018)
- **Ni Putu Lowryanty** (2330105030029)
- **Fatiya Ummu Hanifah Zahra** (2330205030042)

## Deskripsi Singkat
Sistem Parkir Cerdas ini dirancang untuk mengotomatisasi proses masuk dan keluar kendaraan menggunakan kartu RFID sebagai kunci akses berbasis whitelist, sekaligus menghitung biaya parkir secara otomatis berdasarkan durasi parkir yang direkam oleh RTC. Sistem ini mendukung monitoring multi-lantai (3 lantai) dan dilengkapi dengan sensor gas MQ-2 untuk memantau kadar gas berbahaya di lingkungan parkir, serta mengirimkan notifikasi peringatan secara real-time melalui protokol MQTT ke dashboard monitoring jarak jauh.

## Kebutuhan Hardware

| Komponen | Fungsi |
|----------|--------|
| ESP32 Dev Module | Mikrokontroler utama (1 unit per lantai) |
| Sensor Ultrasonik HC-SR04 | Deteksi kendaraan di gerbang dan slot parkir |
| RFID MFRC522 | Membaca kartu akses kendaraan (Whitelist) |
| Sensor Gas MQ-2 | Monitoring kebocoran gas berbahaya di area parkir |
| RTC DS1307 | Pencatat waktu masuk dan keluar kendaraan untuk billing |
| Servo Motor SG90 | Penggerak palang pintu portal |
| LCD I2C 16x2 | Tampilan status sistem secara lokal |
| Buzzer Active | Indikator suara (alarm/peringatan) |

## Teknologi yang Digunakan
- **Firmware**: C++ (Arduino IDE / Wokwi Simulator)
- **Backend**: Node.js, Express, SQLite, MQTT
- **Dashboard**: Node-RED Dashboard 2.0
- **Broker MQTT**: HiveMQ Public (broker.hivemq.com:1883)
- **Database**: SQLite (parking.db) dan JSON Log (parking_db.json)
- **Simulasi**: Wokwi
- **Keamanan**: API Key Authentication + bcrypt Login

## Fitur Utama
1. **Monitoring Multi-Lantai**: Pelacakan slot parkir real-time untuk 3 lantai secara simultan
2. **Akses RFID**: Validasi kartu RFID terotorisasi
3. **Deteksi Bahaya Gas**: Sensor MQ-2 dengan alarm buzzer dan notifikasi LCD
4. **Kontrol Gerbang Otomatis**: Servo motor mengatur buka/tutup portal
5. **Dashboard Real-Time**: Node-RED Dashboard dengan gauge, status slot, dan tombol kontrol
6. **Dual Database**: SQLite untuk query terstruktur + JSON untuk logging mentah
7. **Keamanan Terintegrasi**: API Key authentication pada backend + login Node-RED (bcrypt)

## Format Firmware per Lantai
Setiap lantai memiliki **1 folder dan 1 file `.ino`** yang berisi seluruh logika (WiFi, MQTT, Sensor, Aktuator).

## Keamanan Sistem
- **API Key Authentication**: Semua endpoint REST API (`/api/...`) dilindungi dengan header `x-api-key`. Request tanpa key akan ditolak (HTTP 401 Unauthorized).
- **Node-RED Login**: Editor Node-RED dikunci dengan autentikasi kredensial menggunakan bcrypt hashing.
- **Public Dashboard**: Dashboard UI (`/ui`) dibiarkan publik sebagai Public Display Monitor untuk memudahkan monitoring.



## Lisensi
Distribusi proyek ini dilindungi di bawah lisensi MIT.
