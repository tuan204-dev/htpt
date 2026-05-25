# HTPT Web Monitor (Node.js)

Web giám sát real-time cho đồ án HTPT — chạy bằng **Node.js + Express + Socket.IO + mqtt.js**.

## Kiến trúc

```
   ┌────────────┐         ┌─────────────────┐         ┌─────────────┐
   │  Browser   │ ←─ws──→ │  server.js      │ ←─mqtt─→│ Mosquitto   │
   │ index.html │ Socket  │ Express + Socket│   1883  │   broker    │
   └────────────┘   IO    │   IO + mqtt.js  │         └─────────────┘
                          └─────────────────┘                ↑↓
                              port 3000                   ┌─────┐
                                                          │ESP32│
                                                          └─────┘
```

- Browser **không** connect MQTT trực tiếp — chỉ nói chuyện với server qua Socket.IO.
- Server làm middleman: subscribe tất cả topic monitor, đẩy về browser; nhận lệnh control từ browser rồi publish ra MQTT.

## Cài và chạy

Yêu cầu: **Node.js 18+** (`node --version` để kiểm tra).

```bash
cd web
npm install
npm start
```

Mở: **http://localhost:3000**

Tùy chọn:
```bash
PORT=8080 npm start                              # đổi port web
MQTT_URL=mqtt://192.168.1.50:1883 npm start      # broker máy khác
```

Khi server khởi động, nó in ra mọi địa chỉ IP LAN có thể truy cập — copy URL đó để mở trên điện thoại / máy khác cùng mạng.

## Cấu trúc thư mục

```
web/
├── package.json         # dependencies: express, socket.io, mqtt
├── server.js            # backend - kết nối MQTT, serve web, Socket.IO bridge
├── public/
│   └── index.html       # UI - dùng Socket.IO client + Chart.js (CDN)
└── README.md            # file này
```

## Các topic được forward về browser

| Topic | Mô tả |
|-------|-------|
| `sensor/data` | Data DHT11 từ ESP32_A |
| `sensor/ack` | ACK từ ESP32_B |
| `monitor/sent` | Mỗi lần A gửi mới |
| `monitor/retry` | Mỗi lần A retry |
| `monitor/failed` | A đầu hàng sau max retry |
| `monitor/received` | Mỗi message B nhận được (kèm status) |
| `monitor/processed` | Message B đã xử lý (kèm temp, humid, led_main) |
| `monitor/stats/A` | Thống kê tích lũy của A (mỗi 1s) |
| `monitor/stats/B` | Thống kê tích lũy của B (mỗi 1s) |

## Các topic browser được phép publish (qua server)

- `control/drop_rate` — điều khiển drop rate của ESP32_A
- `control/ack_drop_rate` — điều khiển ACK drop của ESP32_B
- `control/threshold` — điều khiển ngưỡng nhiệt độ của ESP32_B

Topic không nằm trong whitelist sẽ bị server từ chối — an toàn cho demo.

## Troubleshooting

| Triệu chứng | Xử lý |
|------------|-------|
| Web bật được nhưng status DISCONNECTED | Mosquitto chưa chạy / sai IP. Kiểm tra `mosquitto -c broker/mosquitto.conf` |
| `Error: listen EADDRINUSE :::3000` | Port 3000 đã bị dùng. Đổi: `PORT=8080 npm start` |
| `npm install` báo lỗi network | Dùng `npm install --registry=https://registry.npmjs.org/` hoặc đặt proxy nếu mạng trường chặn |
| Chart không hiển thị | Trang dùng Chart.js qua CDN — cần internet lần đầu. Có thể tải về `public/chart.umd.min.js` và sửa thẻ `<script src>` |
| Mở trên điện thoại không thấy gì | Cần dùng IP LAN (không phải localhost). Xem log server in ra danh sách IP |

## Khác biệt so với Node-RED

| | Node-RED dashboard | Web Node.js (file này) |
|---|---|---|
| Cài đặt | `npm i -g node-red node-red-dashboard` + import flow | `npm install` + `npm start` |
| Tùy biến UI | Kéo thả widget, hạn chế CSS | HTML/CSS/JS tự do, đẹp tùy ý |
| Truy cập | `http://localhost:1880/ui` | `http://localhost:3000` |
| Đường truyền | Browser ↔ Node-RED ↔ MQTT | Browser ↔ Node.js ↔ MQTT |
| Phù hợp | Dashboard nhanh, sửa nhẹ | Bài tập, demo, có chứng từ "viết web" |

Đồ án có cả hai để bạn linh hoạt — nộp/demo cái nào tùy bạn. File `nodered/flow.json` không bắt buộc dùng.
