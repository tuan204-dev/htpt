# Đồ án Hệ thống Phân tán — Reliable Message Delivery giữa 2 ESP32 qua MQTT

> Minh họa 3 chủ đề: (1) đảm bảo thứ tự + độ tin cậy của message, (2) xử lý mất + trùng message, (3) cơ chế ACK + retry — dùng phần cứng thực tế.

## 1. Tóm tắt hệ thống

```
┌────────────────┐         WiFi LAN          ┌────────────────┐
│  ESP32_A       │  ──────────────────────►  │  ESP32_B       │
│  (Publisher)   │     MQTT messages         │  (Subscriber)  │
│  • DHT11       │                           │  • LED chính   │
│  • 3 LED status│                           │  • 3 LED status│
└───────┬────────┘                           └───────┬────────┘
        │           ┌──────────────────┐             │
        └──────────►│  Laptop           │◄────────────┘
                    │  Mosquitto broker │
                    │  Node-RED dashboard│
                    └──────────────────┘
```

- **ESP32_A (Sender)**: đọc DHT11, publish `sensor/data` với sequence number.
- **ESP32_B (Receiver)**: kiểm tra seq → phân loại (OK/Duplicate/Out-of-order/Corrupt) → ACK → bật LED chính theo ngưỡng nhiệt.
- **Laptop**: chạy Mosquitto + Node-RED, hiển thị thống kê real-time, điều chỉnh drop rate.

**Điểm mấu chốt**: dùng **MQTT QoS 0** (fire-and-forget) làm nền giả lập mạng không tin cậy, rồi **tự cài tầng tin cậy ở mức ứng dụng** (sequence number + ACK + retry với timeout + duplicate detection + reorder buffer). Đây là cách thể hiện rõ nhất việc *tự xây dựng* các cơ chế tin cậy chứ không dựa vào broker.

## 2. Cấu trúc dự án

```
htpt/
├── README.md                         ← file này
├── esp32_a_sender/
│   └── esp32_a_sender.ino            ← code nạp cho ESP32_A
├── esp32_b_receiver/
│   └── esp32_b_receiver.ino          ← code nạp cho ESP32_B
├── broker/
│   └── mosquitto.conf                ← cấu hình Mosquitto chạy native
├── docker/
│   ├── docker-compose.yml            ← Mosquitto qua Docker (khuyến nghị)
│   ├── mosquitto.conf
│   └── README.md
├── web/
│   ├── server.js                     ← Node.js Express + Socket.IO + mqtt.js
│   ├── package.json
│   ├── public/index.html             ← UI (Chart.js + Socket.IO client)
│   └── README.md                     ← hướng dẫn riêng cho web
├── nodered/
│   └── flow.json                     ← Node-RED flow (tùy chọn thay thế)
└── report/
    ├── theory.md                     ← phần lý thuyết cho báo cáo
    ├── sequence_diagrams.md          ← các sequence diagram
    ├── demo_script.md                ← kịch bản demo từng bước
    ├── wiring.md                     ← hướng dẫn lắp mạch chi tiết
    └── measurements.md               ← bảng số liệu đo được
```

## 3. Phần cứng và sơ đồ lắp mạch

> 👉 **Xem [report/wiring.md](report/wiring.md) để có hướng dẫn lắp mạch chi tiết từng bước**: bảng pinout, sơ đồ breadboard, thứ tự lắp ráp, checklist test, và lưu ý an toàn. Phần dưới đây chỉ là tóm tắt.

### Linh kiện cần có
| STT | Linh kiện | Số lượng |
|-----|-----------|----------|
| 1 | ESP32 DevKit (ví dụ HW-394, USB-C, 30 chân) | 2 |
| 2 | DHT11 | 1 |
| 3 | LED 5mm (xanh/vàng/đỏ/trắng) | 7 (3 cho A + 4 cho B) |
| 4 | Điện trở 220Ω | 7 |
| 5 | Điện trở 10kΩ | 1 (pull-up DHT11) |
| 6 | Breadboard + dây jumper | tùy số lượng |

### Sơ đồ ESP32_A (Sender + DHT11)
```
        ESP32_A
       ┌─────────┐
  3V3 ─┤         │
  GND ─┤         │
       │  GPIO 4 ├───── DATA ─────┬──── DHT11 (DATA)
       │         │                │
       │         │            [10kΩ pull-up giữa DATA và 3V3]
       │         │
       │ GPIO 25 ├── [220Ω] ── LED_GREEN  (ACK OK)
       │ GPIO 26 ├── [220Ω] ── LED_YELLOW (Retrying)
       │ GPIO 27 ├── [220Ω] ── LED_RED    (Failed)
       │     GND ├──── chung GND tất cả LED + DHT11
       └─────────┘

DHT11:  VCC → 3V3, GND → GND, DATA → GPIO 4 (kèm pull-up 10kΩ)
```

### Sơ đồ ESP32_B (Receiver + nhiều LED)
```
        ESP32_B
       ┌─────────┐
  3V3 ─┤         │
  GND ─┤         │
       │ GPIO 14 ├── [220Ω] ── LED_MAIN   (Bật khi temp > ngưỡng)
       │ GPIO 25 ├── [220Ω] ── LED_GREEN  (OK đúng thứ tự)
       │ GPIO 26 ├── [220Ω] ── LED_YELLOW (Duplicate)
       │ GPIO 27 ├── [220Ω] ── LED_RED    (Out-of-order/Missing)
       │     GND ├──── chung GND tất cả LED
       └─────────┘
```

**Lưu ý nối LED**: chân anode (dài) của LED → GPIO **qua điện trở 220Ω**; chân cathode (ngắn) → GND. Không đảo chiều.

## 4. Cài đặt môi trường

### 4.1. Trên laptop (Mac/Windows/Linux)

#### Chạy Mosquitto (chọn 1 trong 2 cách)

**Cách A — qua Docker (khuyến nghị, sạch và portable, có auth):**
```bash
cd docker
cp .env.example .env       # (lần đầu) tạo credentials
./setup-auth.sh            # (lần đầu) sinh mosquitto.passwd
docker compose up -d
docker compose logs -f     # xem log
```
Broker có sẵn 4 user: `esp32_a`, `esp32_b`, `webserver`, `admin` — password mặc định trong `.env.example`. Chi tiết: [docker/README.md](docker/README.md).

**Cách B — cài native lên máy:**
- macOS: `brew install mosquitto`
- Windows: tải installer từ https://mosquitto.org/download/
- Ubuntu: `sudo apt install mosquitto mosquitto-clients`

Chạy:
```bash
cd broker
mkdir -p mosquitto_data
mosquitto -c mosquitto.conf -v
```

Cả 2 cách đều lắng nghe port **1883** (MQTT) và **9001** (WebSocket).

#### Chạy Web Monitor (Node.js — khuyến nghị)
```bash
cd web
npm install
npm start
```
Mở trình duyệt: **http://localhost:3000**

Web tự kết nối broker MQTT, hiển thị real-time gauges, counters, biểu đồ sequence number, log sự kiện, và slider điều khiển. Xem chi tiết tại [web/README.md](web/README.md).

#### (Tùy chọn) Cài Node-RED
Nếu bạn muốn dùng Node-RED thay vì web Node.js tự viết:
```bash
npm install -g --unsafe-perm node-red node-red-dashboard
node-red
```
Mở `http://localhost:1880` (editor) → menu (☰) → Import → chọn `nodered/flow.json` → Deploy.
Dashboard tại `http://localhost:1880/ui`.

#### Lấy IP của laptop (để ESP32 kết nối broker)
- **macOS / Linux**: `ifconfig | grep "inet "` (chọn IP của Wi-Fi adapter, thường bắt đầu bằng `192.168.x.x`)
- **Windows**: `ipconfig` → tìm "IPv4 Address" của adapter Wi-Fi

#### Mở firewall cho port 1883
- **macOS**: System Settings → Network → Firewall → tắt hoặc thêm rule
- **Windows**: Windows Defender Firewall → Allow an app → tìm Mosquitto
- **Linux**: `sudo ufw allow 1883`

### 4.2. Trên Arduino IDE

#### Cài board ESP32
1. File → Preferences → Additional Board URLs:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Tools → Board → Boards Manager → tìm "esp32" → Install

#### Cài thư viện (Library Manager)
Sketch → Include Library → Manage Libraries → cài:
- **PubSubClient** (Nick O'Leary)
- **DHT sensor library** (Adafruit)
- **Adafruit Unified Sensor**
- **ArduinoJson** (Benoit Blanchon, v6.x)

#### Sửa thông số trước khi nạp
Mở `esp32_a_sender/esp32_a_sender.ino` và `esp32_b_receiver/esp32_b_receiver.ino`, sửa 3 dòng đầu trong phần CẤU HÌNH:
```cpp
const char*    WIFI_SSID   = "TÊN_WIFI_CỦA_BẠN";
const char*    WIFI_PASS   = "MẬT_KHẨU_WIFI";
const char*    MQTT_BROKER = "192.168.1.100";   // IP laptop chạy Mosquitto
```

#### Nạp code
1. Cắm ESP32 đầu tiên vào USB (board HW-394 dùng cáp **USB-C**) → Tools → Board → "ESP32 Dev Module" → chọn cổng COM → Upload `esp32_a_sender.ino`
2. Rút ra, cắm ESP32 thứ hai → Upload `esp32_b_receiver.ino`
3. Mở Serial Monitor (115200 baud) để theo dõi log

> **Nếu Arduino IDE không nhận cổng COM**: board HW-394 thường dùng chip USB-to-Serial CH340. Cài driver CH340 từ http://www.wch-ic.com/downloads/CH341SER_ZIP.html (Windows). macOS/Linux thường có driver sẵn.
>
> **Nếu báo lỗi `Failed to connect / Timed out waiting for packet header`**: giữ nút **BOOT** trong khi Arduino IDE bắt đầu upload, thả ra khi thấy quá trình ghi flash bắt đầu.

## 5. Chạy hệ thống

Thứ tự khuyến nghị:
1. Bật **Mosquitto** (cửa sổ terminal 1)
2. Bật **Node-RED** (cửa sổ terminal 2)
3. Mở dashboard: `http://localhost:1880/ui`
4. Cấp nguồn **ESP32_A** → Serial Monitor sẽ in `[WiFi] OK`, `[MQTT] Connecting OK`, `[SEND] seq=1 ...`
5. Cấp nguồn **ESP32_B** → Serial Monitor in `[OK] seq=X ...`, LED_GREEN nháy
6. Trên dashboard sẽ thấy gauge nhiệt độ/độ ẩm cập nhật, các counter chạy

### Kiểm tra nhanh bằng dòng lệnh (debug)
Trong khi hệ thống đang chạy, mở terminal khác:
```bash
# Xem tất cả message bay trên broker
mosquitto_sub -h localhost -t '#' -v

# Gửi thử lệnh điều khiển thay vì dùng dashboard
mosquitto_pub -h localhost -t control/drop_rate -m "0.3"
mosquitto_pub -h localhost -t control/threshold -m "28"
mosquitto_pub -h localhost -t control/ack_drop_rate -m "0.2"
```

## 6. Cách demo (tóm tắt — chi tiết xem `report/demo_script.md`)

| Bước | Thao tác | Hiện tượng |
|------|---------|------------|
| 1 | Drop rate = 0 | Mọi gói đến đích, LED_GREEN cả hai bên nháy đều |
| 2 | Kéo slider Drop rate → 0.3 | LED_YELLOW (A) nháy → retry. Counter `Retries` tăng. `Failed` ≈ 0 |
| 3 | Kéo Drop rate → 0.8 | LED_RED (A) bật → vượt giới hạn retry. `Failed` tăng |
| 4 | Drop rate = 0, ACK drop rate → 0.4 | LED_YELLOW (B) nháy → duplicate detection |
| 5 | Làm nóng DHT11 bằng tay | LED_MAIN bật khi temp vượt ngưỡng |
| 6 | Tắt ESP32_B 10s rồi bật lại | A retry liên tục; B nhận lại nhưng không xử lý duplicate cũ |

## 7. Tóm tắt giao thức tầng ứng dụng

### Topics MQTT
| Topic | Hướng | Nội dung |
|-------|-------|---------|
| `sensor/data` | A → B | Dữ liệu DHT11 + seq + checksum |
| `sensor/ack` | B → A | ACK kèm status (OK/DUPLICATE/OUT_OF_ORDER) |
| `monitor/sent` | A → Node-RED | Log mỗi lần publish lần đầu |
| `monitor/retry` | A → Node-RED | Log mỗi lần retry |
| `monitor/failed` | A → Node-RED | Log gói fail sau MAX_RETRY |
| `monitor/received` | B → Node-RED | Log mỗi gói nhận được + phân loại |
| `monitor/processed` | B → Node-RED | Log message đã xử lý xong |
| `monitor/stats/A` | A → Node-RED | Thống kê tích lũy của A (mỗi 1s) |
| `monitor/stats/B` | B → Node-RED | Thống kê tích lũy của B (mỗi 1s) |
| `control/drop_rate` | Node-RED → A | Điều khiển tỉ lệ drop data của A |
| `control/ack_drop_rate` | Node-RED → B | Điều khiển tỉ lệ drop ACK của B |
| `control/threshold` | Node-RED → B | Điều khiển ngưỡng nhiệt độ |

### Format data message (A → B)
```json
{
  "seq": 42,
  "ts": 1234567890,
  "sender": "ESP32_A_Sender",
  "temp": 28.5,
  "humid": 65.2,
  "checksum": 12345
}
```

### Format ACK message (B → A)
```json
{
  "ack_seq": 42,
  "receiver": "ESP32_B_Receiver",
  "status": "OK",
  "ts": 1234567950
}
```
`status`: `OK` | `DUPLICATE` | `OUT_OF_ORDER` (đã nhận nhưng chưa xử lý)

### Tham số có thể chỉnh trong code
| Hằng | Mặc định | Vị trí |
|------|---------|--------|
| `SEND_INTERVAL_MS` | 2000 | ESP32_A |
| `RETRY_TIMEOUT_MS` | 2000 | ESP32_A |
| `MAX_RETRY` | 3 | ESP32_A |
| `PENDING_BUFFER_SIZE` | 16 | ESP32_A |
| `REORDER_BUFFER_SIZE` | 16 | ESP32_B |
| `TEMP_THRESHOLD` | 30.0 | ESP32_B (điều khiển qua MQTT) |

## 8. Troubleshooting

| Triệu chứng | Nguyên nhân thường gặp | Cách xử lý |
|------------|------------------------|------------|
| ESP32 không kết nối WiFi | SSID/Pass sai, WiFi 5GHz | ESP32 chỉ hỗ trợ 2.4GHz; check lại WiFi |
| ESP32 in `[MQTT] FAIL rc=-2` liên tục | Sai IP broker hoặc firewall chặn 1883 | `ping` IP laptop từ điện thoại; tắt firewall |
| DHT11 in `nan` | Pull-up sai, dây lỏng | Đảm bảo điện trở 10kΩ giữa DATA và VCC |
| LED không sáng | Nối ngược chiều LED, quên điện trở | Anode (chân dài) → GPIO; cathode → GND |
| Dashboard không hiện gì | Node-RED chưa Deploy, broker khác cấu hình | Kiểm tra node MQTT có chấm xanh "connected" |
| MQTT broker timeout 15s | Keepalive PubSubClient mặc định 15s | OK với cấu hình mặc định |

## 9. Tài liệu tham khảo

- [MQTT Version 3.1.1 Specification](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
- A. Tanenbaum, M. van Steen, *Distributed Systems: Principles and Paradigms*, ch. 4 (Communication)
- RFC 793 — Transmission Control Protocol (phần Reliability)
- [PubSubClient documentation](https://pubsubclient.knolleary.net/)
- [ArduinoJson documentation](https://arduinojson.org/v6/doc/)
- [Node-RED dashboard documentation](https://flows.nodered.org/node/node-red-dashboard)

---
*Sinh viên thực hiện: [họ và tên] — môn Hệ thống Phân tán — 2026*
