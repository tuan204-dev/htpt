# Bảng số liệu đo được (template — điền sau khi chạy demo thật)

> **Cách đo**: mỗi cấu hình chạy đúng **3 phút** (90 message lý thuyết với chu kỳ 2s), đọc các counter trên Node-RED dashboard tại thời điểm cuối, ghi vào bảng dưới.

## 1. Bảng tổng hợp — ảnh hưởng của drop rate (chiều A → B)

| STT | Cấu hình | Unique seq (A) | Sent total (A) | Dropped sim (A) | Retries (A) | OK processed (B) | Duplicate (B) | OOO (B) | Failed (A) | **Reliability %** |
|----|---------|---------------:|---------------:|----------------:|------------:|----------------:|--------------:|-------:|----------:|------------------:|
| 1 | Drop=0, ACKdrop=0 | 90 | 90 | 0 | 0 | 90 | 0 | 0 | 0 | **100%** |
| 2 | Drop=0.1, ACKdrop=0 | | | | | | | | | |
| 3 | Drop=0.3, ACKdrop=0 | | | | | | | | | |
| 4 | Drop=0.5, ACKdrop=0 | | | | | | | | | |
| 5 | Drop=0.7, ACKdrop=0 | | | | | | | | | |
| 6 | Drop=0.9, ACKdrop=0 | | | | | | | | | |

**Công thức Reliability%** = `OK_processed (B) / Unique_seq (A) × 100`

Cách điền các ô:
- "Unique seq (A)": đọc trường `unique_seq` trong widget thống kê A.
- "Sent total (A)": tổng số lần publish thật, bao gồm cả retry.
- "Dropped sim (A)": số gói bị drop bởi simulator.
- "Retries (A)": tổng số lần retry.
- "OK processed (B)": số gói B đã xử lý qua nhánh OK + DRAIN.
- "Duplicate (B)": số gói bị nhận diện là duplicate.
- "OOO (B)": số gói lưu vào reorder buffer.
- "Failed (A)": số gói fail sau MAX_RETRY.

## 2. Bảng tổng hợp — ảnh hưởng của ACK drop rate (chiều B → A)

| STT | Cấu hình | Unique seq | Retries (A) | Duplicate (B) | OK processed (B) | Failed (A) | **Reliability %** |
|----|---------|------------:|------------:|--------------:|----------------:|----------:|------------------:|
| 7 | Drop=0, ACKdrop=0.2 | | | | | | |
| 8 | Drop=0, ACKdrop=0.4 | | | | | | |
| 9 | Drop=0, ACKdrop=0.6 | | | | | | |

**Quan sát mong đợi**:
- Khi ACK rớt nhiều, **Duplicate (B) tăng nhanh** (vì sender retry hoài cùng 1 seq) nhưng **OK processed (B) không tăng gấp đôi** → chứng minh idempotent.
- Reliability vẫn cao gần 100% miễn là ACKdrop không phá hết các retry.

## 3. Bảng so sánh "có vs không có cơ chế tin cậy"

> Để so sánh, tạm thời comment out cơ chế retry trong code A (đặt `MAX_RETRY = 0`) và disable duplicate detection ở B (luôn xử lý), rồi đo.

| Cấu hình | Có cơ chế tin cậy? | Drop rate | OK processed | Mất (count) | Reliability % |
|---------|:-----------------:|---------:|-------------:|------------:|--------------:|
| A | Có | 0.3 | | | |
| B | Không (MAX_RETRY=0) | 0.3 | | ≈ 30% × 90 = 27 | ≈ 70% |
| C | Có | 0.5 | | | |
| D | Không | 0.5 | | ≈ 50% × 90 = 45 | ≈ 50% |

**Kết luận viết vào báo cáo**:
> Với cùng tỉ lệ mất gói 30%, hệ thống **có cơ chế tin cậy đạt ~100% reliability**, trong khi không có cơ chế chỉ đạt **~70%**. Khoảng cách càng lớn khi mạng kém hơn.

## 4. Đo độ trễ RTT (Round-Trip Time)

> RTT được in trong log A: `[ACK] seq=N status=OK rtt=XXms`. Lấy ngẫu nhiên 30 mẫu trong điều kiện mạng ổn định.

| Cấu hình | Drop rate | RTT trung bình (ms) | RTT min (ms) | RTT max (ms) | Số lần retry trong mẫu |
|---------|---------:|-------------------:|------------:|------------:|----------------------:|
| Idle | 0 | | | | 0 |
| Drop 30% | 0.3 | | | | |
| Drop 50% | 0.5 | | | | |

Quan sát mong đợi:
- RTT idle thường < 50 ms với MQTT trên Wi-Fi LAN.
- Khi có retry, RTT của các gói retry sẽ ≥ 2000 ms (vì RETRY_TIMEOUT_MS = 2000).

## 5. Phân loại nguyên nhân thiếu/trùng (tổng kết)

Sau khi chạy bài 3 phút ở `Drop=0.3, ACKdrop=0.3`, điền:

| Phân loại | Số lượng | Tỉ lệ |
|----------|---------:|------:|
| Gói thành công ngay lần 1 | | |
| Gói thành công sau 1 lần retry | | |
| Gói thành công sau 2-3 lần retry | | |
| Gói duplicate được phát hiện | | |
| Gói out-of-order được sắp lại | | |
| Gói failed hẳn (sau MAX_RETRY) | | |

## 6. Mẫu biểu đồ cho slide

Có thể vẽ thêm 2 biểu đồ:

### Biểu đồ 1: Reliability vs Drop rate
- Trục X: drop rate (0%, 10%, 30%, 50%, 70%, 90%)
- Trục Y: reliability (%)
- 2 đường: "Có cơ chế" (gần phẳng ở 100%) vs "Không cơ chế" (giảm tuyến tính)

### Biểu đồ 2: RTT distribution
- Histogram RTT trong 100 mẫu, phân loại "no retry" vs "1 retry" vs "2+ retry"
- Sẽ thấy bimodal: nhóm 0-50ms (no retry) và nhóm ~2000ms (có retry)

## 7. Ghi chú khi đo

- Đảm bảo router Wi-Fi ít noise (tắt các thiết bị khác nếu được).
- Reset counter trước mỗi lần đo bằng cách restart ESP32_A và ESP32_B (rút cắm USB).
- Chạy đúng 3 phút bằng đồng hồ, không quá ngắn (sample nhỏ → nhiễu).
- Mỗi cấu hình đo **3 lần**, lấy giá trị trung bình → tăng độ tin cậy của số liệu.
