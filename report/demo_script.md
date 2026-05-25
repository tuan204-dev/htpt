# Kịch bản demo chi tiết (cho buổi bảo vệ)

> Mục tiêu: trong 8–10 phút thể hiện đầy đủ 3 chủ đề (thứ tự + tin cậy, mất + trùng, ACK + retry) một cách trực quan.

## 0. Chuẩn bị trước khi thầy/cô vào phòng

| Mục | Kiểm tra |
|-----|---------|
| Laptop đã cắm sạc | ✅ |
| Mosquitto đã chạy (`mosquitto -c broker/mosquitto.conf -v`) | ✅ |
| Node-RED đã chạy + flow đã Deploy | ✅ |
| Dashboard mở sẵn ở `http://localhost:1880/ui` | ✅ |
| 2 ESP32 đã nạp đúng code, cắm USB, Serial Monitor mở (2 cửa sổ) | ✅ |
| DHT11 + LED nối đúng theo sơ đồ, không lỏng dây | ✅ |
| Drop rate ban đầu = 0, ACK drop rate = 0, threshold = 30 | ✅ |
| Có sẵn cốc nước nóng / máy sấy tóc để làm nóng DHT11 demo bật LED_MAIN | ✅ |

## 1. Slide mở đầu (1 phút)

- Giới thiệu đề bài: 3 chủ đề liên quan đến độ tin cậy của message trong hệ thống phân tán.
- Giới thiệu kiến trúc: 2 ESP32, MQTT broker trên laptop, tự cài tầng tin cậy trên QoS 0.
- **Câu chốt cần nói**: "Em chọn QoS 0 (kém tin cậy) làm nền, rồi tự xây cơ chế ACK + Retry + Sequence + Reorder ở tầng ứng dụng. Cách này thể hiện rõ nhất việc em hiểu và tự cài đặt các cơ chế chứ không dựa vào broker."

## 2. Demo step-by-step

### Bước 1 — Hoạt động bình thường (1 phút)
**Thao tác**:
- Đảm bảo slider Drop rate = 0, ACK drop rate = 0.
- Chỉ vào 2 ESP32 và dashboard.

**Điểm cần chỉ ra**:
- Gauge nhiệt độ + độ ẩm trên dashboard cập nhật mỗi 2s.
- LED_GREEN của **cả A lẫn B** nháy đều — chứng tỏ gửi → nhận → ACK → done.
- Trên Serial Monitor A: log `[SEND] seq=N` rồi `[ACK] seq=N status=OK rtt=XXms` xen kẽ.
- Counter "Sent total" và "OK processed" tăng đều bằng nhau.
- Counter "Retries", "Failed", "Duplicate" đều = 0.

**Lời thoại**: *"Đây là trạng thái không lỗi. Mỗi message có sequence number tăng dần, được ACK riêng từng cái. RTT đo được khoảng XX ms."*

### Bước 2 — Mô phỏng mất gói data (2 phút) — chủ đề "Cơ chế retry"
**Thao tác**:
- Kéo slider **Drop rate** lên **0.3** (30%).

**Điểm cần chỉ ra**:
- Trên A: log `[DROP-SIM] seq=N bị drop` thỉnh thoảng xuất hiện, ngay sau đó `[RETRY] seq=N attempt=1/3`.
- LED_YELLOW của A nháy mỗi lần retry.
- Trên B: ban đầu thấy gap (ví dụ nhận 12, 13, 15) → log `[OUT_OF_ORDER]` xen kẽ; LED_RED nháy.
- Sau khi retry, log `[OK] seq=14 ...` rồi `[DRAIN] xử lý seq=15 từ reorder buffer`.
- Counter "Retries" tăng, "Failed" vẫn = 0 → **hệ thống tự khắc phục, không mất dữ liệu**.

**Lời thoại**: *"Em đang giả lập 30% gói data bị drop ngay tại sender — như mạng không tin cậy. Sender tự retry sau 2s, và cuối cùng tất cả message vẫn đến đích đúng thứ tự nhờ reorder buffer ở receiver."*

### Bước 3 — Quá nhiều mất gói (1 phút) — chủ đề "Giới hạn của retry"
**Thao tác**:
- Kéo Drop rate lên **0.8** (80%).

**Điểm cần chỉ ra**:
- Sau ~10s, log A bắt đầu có `[FAILED] seq=N sau 3 lần retry`.
- LED_RED của A bật 2s mỗi lần fail.
- Counter "Failed" tăng → có những message *thực sự* mất.
- Trên B: log `[OUT_OF_ORDER]` xuất hiện liên tục với gap không đóng được — vì gói gốc đã mất hẳn.

**Lời thoại**: *"Khi drop rate quá cao, retry hữu hạn không đủ để bù — đây chính là Two Generals Problem: không có giao thức nào đảm bảo 100% trên kênh lossy. Trong thực tế, ứng dụng phải có chiến lược fallback ở tầng cao hơn."*

### Bước 4 — Demo Duplicate detection (1.5 phút) — chủ đề "Xử lý trùng message"
**Thao tác**:
- Drop rate về **0**.
- Kéo **ACK drop rate** lên **0.4** (B có 40% xác suất drop ACK).

**Điểm cần chỉ ra**:
- Trên B: log `[ACK-DROP-SIM] seq=N status=OK bị drop` xen kẽ.
- Trên A: pending buffer hết hạn → retry → `[RETRY] seq=N`.
- Trên B: log `[DUPLICATE-OLD] seq=N (expected=M) → gửi lại ACK`.
- **LED_YELLOW của B nháy** → đây là LED đại diện cho duplicate detection.
- Counter B "Duplicate" tăng đều, nhưng "OK processed" **không bị tăng gấp đôi** → chứng tỏ receiver idempotent.
- Counter A "Failed" vẫn = 0.

**Lời thoại**: *"Lần này em không drop data mà drop ACK — vẫn là tình huống thường gặp trong thực tế. Sender không nhận được ACK nên retry; receiver thấy seq cũ rồi và biết đây là duplicate. Receiver KHÔNG xử lý lại (không gọi process 2 lần) nhưng vẫn gửi ACK để sender unblock. Đây là tính idempotent — quan trọng vì retry là vô tránh."*

### Bước 5 — Hardware actuator (1 phút) — minh chứng phần cứng thật
**Thao tác**:
- Drop rate = 0, ACK drop rate = 0.
- Cầm cốc nước nóng / cốc cà phê nóng / máy sấy tóc áp gần DHT11.

**Điểm cần chỉ ra**:
- Gauge nhiệt độ trên dashboard tăng dần.
- Khi vượt ngưỡng (30°C mặc định) → **LED_MAIN của B bật**.
- Để xa ra, gauge giảm, LED_MAIN tắt.
- Bonus: kéo slider "Temp threshold" xuống 25°C → LED_MAIN bật ngay khi nhiệt độ phòng vượt 25.

**Lời thoại**: *"Phần actuator có thật — receiver thực sự điều khiển LED dựa trên dữ liệu sensor được truyền *tin cậy* qua MQTT. Slider threshold điều khiển từ dashboard cho thấy kênh control cũng dùng đúng cơ chế MQTT."*

### Bước 6 — Test chịu lỗi mất kết nối (1 phút) — bonus
**Thao tác**:
- Rút USB ESP32_B trong khi A vẫn chạy.

**Điểm cần chỉ ra**:
- Trên A: log retry liên tục, LED_YELLOW nháy nhiều, rồi LED_RED bắt đầu bật → các gói fail tích lũy.
- Cắm lại B → B kết nối lại broker → bắt đầu nhận message mới.
- Important: B **không xử lý lại** các seq cũ (đã mất hẳn) — tiếp tục từ seq mới.
- Sender không crash, vẫn gửi tiếp.

**Lời thoại**: *"Đây là test fault tolerance: receiver chết tạm, sender vẫn retry rồi đầu hàng, rồi hệ thống tự khôi phục khi receiver quay lại. Không crash, không deadlock."*

## 3. Slide kết luận (1 phút)

- Đã thực hiện được:
  - ✅ Sequence number → đảm bảo thứ tự
  - ✅ ACK + Retry → đảm bảo độ tin cậy
  - ✅ Duplicate detection → xử lý trùng message
  - ✅ Reorder buffer → xử lý sai thứ tự
  - ✅ Phần cứng thật (DHT11 + LED + 2 ESP32) → giáo viên yêu cầu
- Đã đo được (bảng số liệu — xem `measurements.md`):
  - Cùng drop rate 30%: có cơ chế → 0% mất; không có → 30% mất.
- Hạn chế / hướng phát triển:
  - Buffer hữu hạn — chưa giải quyết khi gap quá lớn vĩnh viễn.
  - Timeout cố định — có thể nâng cấp adaptive theo RTT.
  - Chưa demo trên >2 node — có thể mở rộng thành multicast.

## 4. Câu hỏi giáo viên thường hỏi (và gợi ý trả lời)

**Q: Vì sao không dùng MQTT QoS 1 hoặc 2 cho gọn?**
> Vì em muốn *thể hiện* việc em tự cài cơ chế. QoS 1/2 ẩn hết logic vào broker, em chỉ là client mù. Dùng QoS 0 + cơ chế tự xây giúp em hiểu sâu và demo trực quan.

**Q: Sequence number của em chạy đến đâu thì hết?**
> Em dùng `uint32_t` → ~4.3 tỷ. Với chu kỳ 2s/gói thì ~272 năm mới wrap-around. Đủ cho demo.

**Q: Nếu ACK bị mất nhiều lần liên tiếp thì sao?**
> Cùng 1 seq sẽ được retry thêm. Mỗi lần đến B đều bị nhận diện là duplicate (vì seq < expected). B vẫn gửi ACK lại. Nếu cuối cùng ACK đến được, A unblock; nếu không, sau MAX_RETRY A đầu hàng — message đó tính là *đã xử lý nhưng không xác nhận được*, đúng với Two Generals Problem.

**Q: Tại sao receiver phải ACK cả gói duplicate?**
> Vì sender đang chờ ACK để unblock pending buffer. Nếu B im lặng, A sẽ retry mãi → bùng nổ traffic. ACK lại là cách bảo cho sender "tao đã nhận và đã xử lý cái này rồi".

**Q: Tầng nào của OSI em đang làm?**
> MQTT là tầng ứng dụng (layer 7), QoS 0 không có reliability nội tại. Em đang xây *thêm một sub-layer tin cậy bên trong tầng ứng dụng*, giống như cách các giao thức như AMQP, Kafka tự cài tin cậy của riêng họ.

**Q: Có khả năng xảy ra deadlock không?**
> Không. Sender có timeout — luôn unblock sau RETRY_TIMEOUT * MAX_RETRY = 8 giây tối đa. Receiver xử lý mỗi callback độc lập, không chờ gì. Pending buffer hữu hạn — nếu đầy thì sender skip chu kỳ, không treo.

**Q: Có cải tiến gì không?**
> 1. Adaptive timeout dựa trên RTT đo được (kiểu TCP RTO).
> 2. Cumulative ACK cho throughput cao hơn.
> 3. Persistent storage trên SPIFFS để recovery sau crash.
> 4. Mở rộng nhiều subscriber (multicast) với group ID.
