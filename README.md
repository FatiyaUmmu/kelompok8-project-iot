# kelompok8-project-iot

## Nama Anggota Kelompok :
1. Agatha Monalisa 				 (2330105030008)
2. Muhammad Rony Kurniawan		 (2330105030018)
3. Ni Putu Lowryanty 				 (2330105030029)
4. Fatiya Ummu Hanifah Zahra		 (2330205030042)

## Topik
Smart Parking System with Gas Monitoring & Automated Billing.

## Deskripsi Singkat
Sistem Parkir Cerdas ini dirancang untuk mengotomatisasi proses masuk dan keluar kendaraan menggunakan kartu RFID sebagai kunci akses berbasis whitelist, sekaligus menghitung biaya parkir secara otomatis berdasarkan durasi parkir yang direkam oleh RTC. Sistem ini juga dilengkapi dengan sensor gas MQ-2 untuk memantau kadar gas berbahaya di lingkungan parkir serta mengirimkan notifikasi peringatan secara real-time melalui protokol MQTT ke dashboard monitoring jarak jauh.

## Hardware yang Digunakan
1) ESP32 Dev Module (mikrokontroler utama)
2) 2x Sensor Ultrasonik HC-SR04 (deteksi kendaraan di gerbang dan slot parkir)
3) RFID MFRC522 (membaca kartu akses)
4) Sensor Gas MQ-2 (monitoring kebocoran gas)
5) RTC DS1307 (pencatat waktu masuk dan keluar)
6) Servo Motor SG90 (penggerak palang pintu portal)
7) LCD I2C 16x2 (tampilan status sistem secara lokal)
8) Buzzer Active (indikator suara)
