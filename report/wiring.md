# Hướng dẫn lắp mạch chi tiết

> Tài liệu này hướng dẫn lắp 2 module phần cứng (ESP32_A + DHT11, ESP32_B + nhiều LED) **từng bước**, có sơ đồ ASCII và checklist sau khi lắp. Đọc kỹ phần "Lưu ý an toàn" trước khi cắm nguồn.

## 1. Danh sách linh kiện chi tiết

| Mã | Linh kiện | Số lượng | Ghi chú |
|----|-----------|----------|---------|
| U1 | ESP32 DevKit (ESP-WROOM-32) | 2 | 30 chân hoặc 38 chân đều dùng được |
| S1 | Cảm biến DHT11 (3 hoặc 4 chân) | 1 | Cho ESP32_A |
| L1 | LED xanh lá 5mm | 2 | LED báo OK (1 cho A, 1 cho B) |
| L2 | LED vàng 5mm | 2 | LED báo retry/duplicate (1 cho A, 1 cho B) |
| L3 | LED đỏ 5mm | 2 | LED báo failed/OOO (1 cho A, 1 cho B) |
| L4 | LED trắng hoặc xanh dương 5mm | 1 | LED_MAIN actuator (cho B) |
| R1 | Điện trở 220Ω | 7 | Mã màu: đỏ-đỏ-nâu-vàng |
| R2 | Điện trở 10kΩ | 1 | Pull-up DHT11. Mã màu: nâu-đen-cam-vàng |
| BB1 | Breadboard 400 lỗ | 2 | Hoặc 1 cái 830 lỗ chia đôi |
| W1 | Dây jumper M-M | ~25 | Cắm breadboard ↔ breadboard |
| W2 | Dây jumper M-F | ~10 | Cắm ESP32 ↔ breadboard (nếu ESP32 nằm trên breadboard thì chỉ cần M-M) |
| USB | Cáp **USB-C** (cho board HW-394) | 2 | Để nạp code và cấp nguồn. Các board ESP32 đời cũ dùng Micro-USB |

**Tổng giá ước tính**: 150,000–250,000đ (nếu phải mua mới hết) — rẻ hơn nhiều nếu bạn đã có sẵn breadboard, jumper, một số LED.

## 2. Sơ đồ chân (pinout) ESP32 cần dùng

Sơ đồ dưới đây vẽ chính xác cho **board ESP32 HW-394 USB-C 30 chân** (board phổ biến trên Shopee/Lazada Việt Nam). Quay board sao cho **cổng USB-C ở phía trên**:

```
                    ┌─────[USB-C]─────┐
                BOOT├○                ○┤EN
                    ├─────────────────┤
   3V3 ●────────────┤●               ●├──────────── VIN  (5V từ USB)
   GND ●────────────┤●               ●├──────────── GND
       ●  D15  ─────┤●               ●├──── D13
       ●  D2   ─────┤●               ●├──── D12
👉     ●  D4   ─────┤● DHT11 DATA    ●├──── D14   ←LED_MAIN     👉
       ●  D16  ─────┤●               ●├──── D27   ←LED_RED      👉
       ●  D17  ─────┤●               ●├──── D26   ←LED_YELLOW   👉
       ●  D5   ─────┤●               ●├──── D25   ←LED_GREEN    👉
       ●  D18  ─────┤●  [ESP-WROOM-32]●├──── D33
       ●  D19  ─────┤●               ●├──── D32
       ●  D21  ─────┤●               ●├──── D35  (input-only)
       ●  RX0  ─────┤●               ●├──── D34  (input-only)
       ●  TX0  ─────┤●               ●├──── VN   (input-only)
       ●  D22  ─────┤●               ●├──── VP   (input-only)
       ●  D23  ─────┤●               ●├──── GND
                    └─────────────────┘
```

**Thuận tiện cho board HW-394**: tất cả 4 LED nằm gần nhau ở **bên PHẢI** (D14, D25, D26, D27), DHT11 dùng D4 ở **bên TRÁI** ngay cạnh 3V3 và GND → wiring rất ngắn, không phải kéo dây chéo qua board.

**Đèn báo trên board** (không liên quan đến code của đồ án):
- `PWR` (đỏ): sáng khi có nguồn USB.
- `D2` (xanh): nối với GPIO 2 — có thể nhấp nháy khi nạp code.

> Trên thực tế vị trí các chân có thể khác nhau giữa các phiên bản ESP32 — **luôn nhìn ký hiệu GPIO in trên board**, đừng đếm theo vị trí thứ tự.

## 3. Bảng nối dây ESP32_A (Sender + DHT11)

| # | Đầu A (nguồn tín hiệu) | Đầu B (đích) | Linh kiện trung gian |
|---|------------------------|--------------|----------------------|
| 1 | ESP32_A `3V3` | DHT11 `VCC` (chân `+` hoặc `VDD`) | — |
| 2 | ESP32_A `GND` | DHT11 `GND` (chân `-`) | — |
| 3 | ESP32_A `GPIO 4` | DHT11 `DATA` (chân giữa hoặc `S`) | — |
| 4 | ESP32_A `3V3` | DHT11 `DATA` | **R2 = 10kΩ** (pull-up) |
| 5 | ESP32_A `GPIO 25` | Chân (+) **LED xanh lá** | **R1 = 220Ω** nối tiếp |
| 6 | ESP32_A `GPIO 26` | Chân (+) **LED vàng** | R1 = 220Ω |
| 7 | ESP32_A `GPIO 27` | Chân (+) **LED đỏ** | R1 = 220Ω |
| 8 | Chân (-) của cả 3 LED | ESP32_A `GND` | — |

**Quan trọng**:
- DHT11 **PHẢI** dùng **3.3V** (không phải 5V) — vì ESP32 GPIO chỉ chịu 3.3V; nếu cấp 5V cho DHT11 thì signal DATA cũng sẽ 5V và làm hỏng GPIO ESP32.
- Pull-up 10kΩ (dây thứ 4) là bắt buộc — không có nó DHT11 đọc về `nan` liên tục.
- Chân (+) LED là chân **dài**; chân (-) là chân **ngắn** (hoặc nằm về phía vạt phẳng của thân LED).

## 4. Bảng nối dây ESP32_B (Receiver + 4 LED)

| # | Đầu A | Đầu B | Linh kiện trung gian |
|---|-------|-------|----------------------|
| 1 | ESP32_B `GPIO 14` | Chân (+) **LED_MAIN** (trắng/xanh dương) | R1 = 220Ω |
| 2 | ESP32_B `GPIO 25` | Chân (+) **LED xanh lá** | R1 = 220Ω |
| 3 | ESP32_B `GPIO 26` | Chân (+) **LED vàng** | R1 = 220Ω |
| 4 | ESP32_B `GPIO 27` | Chân (+) **LED đỏ** | R1 = 220Ω |
| 5 | Chân (-) của cả 4 LED | ESP32_B `GND` | — |

## 5. Sơ đồ breadboard gợi ý

### ESP32_A breadboard (Sender)

```
                  ┌────────────────────────────────────────────────────┐
   Rail (+) 3V3 ──┤ + + + + + + + + + + + + + + + + + + + + + + + + +  │
   Rail (-) GND ──┤ - - - - - - - - - - - - - - - - - - - - - - - - -  │
                  └────────────────────────────────────────────────────┘
                       │                                       
                       │  (ESP32 cắm dọc giữa breadboard)
                  ┌────┴───────────────────┐
                  │  ESP32_A                │
                  │  3V3 ──→ rail (+)       │
                  │  GND ──→ rail (-)       │
                  │  GPIO 4 ──→ hàng X      │   X───[10kΩ]───(+) rail
                  │                         │   X───────────── DHT11 DATA
                  │  GPIO 25 ── [220Ω] ── (+) LED xanh ──(-) rail
                  │  GPIO 26 ── [220Ω] ── (+) LED vàng ──(-) rail
                  │  GPIO 27 ── [220Ω] ── (+) LED đỏ   ──(-) rail
                  └─────────────────────────┘

   DHT11 cắm phía bên:
        ┌─────┐
        │DHT11│  VCC ──→ rail (+) 3V3
        │ □  │  DATA ──→ hàng X (chung với GPIO 4 + 10kΩ pull-up)
        └─────┘  GND ──→ rail (-)
```

### ESP32_B breadboard (Receiver)

```
                  ┌────────────────────────────────────────────────────┐
   Rail (+) 3V3 ──┤ + + + + + + + + + + + + + + + + + + + + + + + + +  │  (không dùng cho ESP32_B nhưng có cũng được)
   Rail (-) GND ──┤ - - - - - - - - - - - - - - - - - - - - - - - - -  │
                  └────────────────────────────────────────────────────┘

                  ┌────────────────────────┐
                  │  ESP32_B                │
                  │  GND ──→ rail (-)       │
                  │  GPIO 14 ── [220Ω] ── (+) LED_MAIN (trắng) ──(-) rail
                  │  GPIO 25 ── [220Ω] ── (+) LED xanh        ──(-) rail
                  │  GPIO 26 ── [220Ω] ── (+) LED vàng        ──(-) rail
                  │  GPIO 27 ── [220Ω] ── (+) LED đỏ          ──(-) rail
                  └─────────────────────────┘
```

> Mẹo: Sắp xếp 4 LED của B thành một hàng theo thứ tự **TRẮNG | XANH | VÀNG | ĐỎ** trên breadboard để dễ quan sát khi demo. Có thể dán nhãn nhỏ bên cạnh: `MAIN | OK | DUP | OOO`.

## 6. Sơ đồ kết hợp text-graphic (cho báo cáo)

### Module Sender ESP32_A

```
          3.3V ●──┬──────────────────────┬──── VCC DHT11
                  │                      │
              [10kΩ]                     │
                  │                      │
GPIO 4 ●──────────┴──────────────────────┴──── DATA DHT11
GPIO 25 ●─[220Ω]──┤▶├──── GND   (LED xanh - ACK OK)
GPIO 26 ●─[220Ω]──┤▶├──── GND   (LED vàng - RETRY)
GPIO 27 ●─[220Ω]──┤▶├──── GND   (LED đỏ  - FAILED)
GND   ●───────────────── GND DHT11

ESP32_A    Điện trở     LED      Cảm biến
```

### Module Receiver ESP32_B

```
GPIO 14 ●─[220Ω]──┤▶├──── GND   (LED chính - bật khi nóng)
GPIO 25 ●─[220Ω]──┤▶├──── GND   (LED xanh  - OK đúng thứ tự)
GPIO 26 ●─[220Ω]──┤▶├──── GND   (LED vàng  - DUPLICATE)
GPIO 27 ●─[220Ω]──┤▶├──── GND   (LED đỏ   - OUT-OF-ORDER)
GND   ●──────────────── (GND chung)

ESP32_B    Điện trở     LED
```

Ký hiệu LED `┤▶├`: tam giác là anode (+), vạch là cathode (-).

## 7. Thứ tự lắp ráp (làm theo từng bước)

### Bước 1 — Chuẩn bị (KHÔNG cắm điện)
- [ ] Đặt 2 breadboard cạnh nhau trên bàn.
- [ ] Cắm 2 ESP32 vào breadboard sao cho chân nằm 2 bên rãnh giữa của breadboard (chân không bị nối ngắn mạch).
- [ ] Chuẩn bị sẵn 7 điện trở 220Ω, 1 điện trở 10kΩ, các LED, DHT11.

### Bước 2 — Lắp module Sender (ESP32_A + DHT11)
- [ ] Nối dây **3V3** từ ESP32_A → rail (+) của breadboard.
- [ ] Nối dây **GND** từ ESP32_A → rail (-).
- [ ] Cắm DHT11 vào breadboard: `VCC → rail (+)`, `GND → rail (-)`, `DATA → 1 hàng riêng`.
- [ ] Cắm điện trở **10kΩ** giữa chân DATA của DHT11 và rail (+) (pull-up).
- [ ] Nối dây từ `GPIO 4` của ESP32_A → chân DATA của DHT11 (cùng hàng).
- [ ] Cắm 3 LED status:
  - LED xanh: cathode → rail (-); anode qua điện trở 220Ω → `GPIO 25`.
  - LED vàng: cathode → rail (-); anode qua 220Ω → `GPIO 26`.
  - LED đỏ: cathode → rail (-); anode qua 220Ω → `GPIO 27`.

### Bước 3 — Lắp module Receiver (ESP32_B + 4 LED)
- [ ] Nối dây **GND** từ ESP32_B → rail (-) của breadboard.
- [ ] Cắm 4 LED theo thứ tự (trái → phải):
  - LED_MAIN (trắng/xanh dương): cathode → rail (-); anode qua 220Ω → `GPIO 14`.
  - LED xanh: cathode → rail (-); anode qua 220Ω → `GPIO 25`.
  - LED vàng: cathode → rail (-); anode qua 220Ω → `GPIO 26`.
  - LED đỏ: cathode → rail (-); anode qua 220Ω → `GPIO 27`.

### Bước 4 — Kiểm tra trực quan trước khi cấp nguồn (RẤT QUAN TRỌNG)
- [ ] Nhìn lại từng dây: không có dây nào nối nhầm 3V3 với GND.
- [ ] Mỗi LED đều có điện trở (không dây nối trực tiếp GPIO → LED → GND mà không qua điện trở).
- [ ] Tất cả LED đúng chiều (anode = chân dài về phía GPIO; cathode = chân ngắn về phía GND).
- [ ] DHT11 đã có pull-up 10kΩ giữa DATA và VCC.
- [ ] DHT11 cấp nguồn 3.3V, **không phải** 5V.

### Bước 5 — Cấp nguồn và test cơ bản (chưa nạp code)
- [ ] Cắm USB ESP32_A → đèn nguồn trên board sáng. Không có khói, không có mùi khét.
- [ ] Cắm USB ESP32_B → tương tự.
- [ ] Nếu cả hai OK → chuyển sang bước nạp code (xem README.md mục 4.2).

## 8. Checklist test sau khi nạp code

Sau khi nạp code ESP32_A và mở Serial Monitor (115200 baud):

| Test | Kết quả mong đợi |
|------|------------------|
| Nguồn vào ESP32_A | Đèn power on; Serial in `=== ESP32_A Sender khởi động ===` |
| Kết nối WiFi | Serial in `[WiFi] OK. IP=...` |
| Kết nối MQTT | Serial in `[MQTT] Connecting OK` |
| Đọc DHT11 | Serial in `[SEND] seq=1 temp=XX.X humid=XX.X` (không phải `nan`) |
| Gửi → nhận ACK | Sau ~vài giây, Serial in `[ACK] seq=1 status=OK rtt=XXms` |
| LED xanh A | Nháy ngắn mỗi khi nhận ACK |

Sau khi nạp code ESP32_B:

| Test | Kết quả mong đợi |
|------|------------------|
| Khởi động | Serial in `=== ESP32_B Receiver khởi động ===` |
| Nhận message | Serial in `[OK] seq=N temp=XX.X humid=XX.X → process` |
| LED xanh B | Nháy mỗi khi nhận message hợp lệ |
| LED_MAIN | TẮT khi nhiệt độ < 30°C; bật khi áp tay/cốc nóng vào DHT11 |

Test mô phỏng lỗi (chạy thử ngay không cần Node-RED):
```bash
# Mở terminal mới
mosquitto_pub -h localhost -t control/drop_rate -m "0.5"
```
Sau 5–10s:

| Test | Kết quả mong đợi |
|------|------------------|
| Drop simulator | Serial A in `[DROP-SIM] seq=N bị drop` |
| Retry | Serial A in `[RETRY] seq=N attempt=1/3`; LED vàng A nháy |
| Reorder | Serial B in `[OUT_OF_ORDER]`; LED đỏ B nháy |
| Drain reorder | Serial B in `[DRAIN] xử lý seq=N từ reorder buffer` |

## 9. Lưu ý an toàn

| Tình huống | Nguyên nhân | Hậu quả | Cách phòng tránh |
|-----------|-------------|---------|------------------|
| LED không có điện trở | Quên cắm 220Ω | LED cháy / dòng kéo quá lớn → hỏng GPIO | Luôn kiểm tra trước khi cấp nguồn |
| Cấp 5V cho DHT11 | Nhầm dây nguồn | DHT11 vẫn hoạt động, NHƯNG signal DATA 5V làm hỏng GPIO ESP32 | DHT11 phải nối vào **3V3**, không phải VIN/5V |
| Đảo chiều LED | Anode/cathode nhầm | LED không sáng (nhưng không hỏng vì có điện trở) | Anode dài, cathode ngắn |
| Ngắn mạch 3V3 ↔ GND | Dây jumper sai | ESP32 reset liên tục hoặc nóng | Dùng đồng hồ vạn năng đo trở giữa 3V3 và GND trước khi cấp nguồn (phải lớn, >1kΩ) |
| Cắm/rút module khi đang có nguồn | Tránh hư phần cứng | Có thể gây surge | **Luôn tắt nguồn** (rút USB) trước khi thay đổi mạch |
| Dây jumper lỏng trên breadboard | Cắm không đủ sâu | Tín hiệu chập chờn, DHT11 đọc lỗi ngẫu nhiên | Ấn dây xuống chắc, không kéo căng dây |

## 10. Ảnh tham khảo (mô tả bằng lời)

Khi lắp xong hoàn chỉnh, hệ thống sẽ trông như sau:

```
   ┌─────────────────────────────────┐         ┌─────────────────────────────────┐
   │     BREADBOARD ESP32_A          │         │     BREADBOARD ESP32_B          │
   │                                 │         │                                 │
   │   ┌──────────┐                  │         │   ┌──────────┐                  │
   │   │ ESP32 _A │ ─── USB ─── PC   │         │   │ ESP32 _B │ ─── USB ─── PC   │
   │   └────┬─────┘                  │         │   └────┬─────┘                  │
   │        │                        │         │        │                        │
   │   [DHT11] [R10kΩ]               │         │   [TRẮNG][XANH][VÀNG][ĐỎ]      │
   │     ●●●                         │         │     LED   LED   LED   LED       │
   │                                 │         │                                 │
   │   [XANH][VÀNG][ĐỎ]              │         │   [R][R][R][R]                  │
   │    LED   LED   LED              │         │                                 │
   │                                 │         │                                 │
   │   [R][R][R]                     │         │                                 │
   │   220Ω điện trở                 │         │                                 │
   └─────────────────────────────────┘         └─────────────────────────────────┘
   
   Cả 2 breadboard có thể đặt cạnh nhau trên cùng một bàn,
   chỉ giao tiếp qua MQTT (qua WiFi) — không có dây vật lý nối chéo.
```

Bạn nên chụp lại ảnh mạch thật khi đã lắp xong để chèn vào báo cáo — thầy/cô đánh giá cao việc có ảnh thật bên cạnh sơ đồ ASCII.

## 11. Tham khảo nhanh các chân ESP32 khác (nếu cần đổi)

Nếu chân GPIO mặc định bị bận (ví dụ board của bạn dùng GPIO 4 cho gì đó khác), có thể đổi sang các chân an toàn sau và sửa lại trong code:

| Mục đích | Chân khuyến nghị | Chân **tránh dùng** |
|---------|-------------------|---------------------|
| DHT11 DATA | GPIO 4, 5, 16, 17, 18, 19, 21, 22, 23 | GPIO 0 (boot), GPIO 2 (LED nội), GPIO 6-11 (flash), GPIO 12, 15 (strapping) |
| LED bất kỳ | GPIO 14, 25, 26, 27, 32, 33 | GPIO 34-39 (chỉ input, không output) |

Sau khi đổi chân, sửa các macro `#define` ở đầu mỗi file `.ino`.
