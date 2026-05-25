# Phần lý thuyết — Đảm bảo tin cậy trong truyền message phân tán

## 1. Vấn đề căn bản

Trong hệ thống phân tán, các thành phần (node) giao tiếp với nhau qua mạng. Kênh truyền giữa các node là **không tin cậy về bản chất**:

- **Message có thể bị mất** (packet loss): do nhiễu, router rớt gói, buffer đầy, đường truyền chập chờn.
- **Message có thể bị trùng** (duplication): do retransmission hoặc routing đi nhiều đường khác nhau.
- **Message có thể đến không đúng thứ tự** (out-of-order): các gói đi qua route khác nhau, gói sau đến trước gói trước.
- **Message có thể bị hỏng** (corruption): bit-error trong truyền dẫn.
- **Message có thể bị trễ vô thời hạn** (delay): không phân biệt được giữa "mất" và "rất chậm".

Hệ quả: ứng dụng *không thể* giả định "gửi đi là chắc chắn đến" hoặc "đến đúng thứ tự đã gửi". Phải có cơ chế bảo đảm rõ ràng.

## 2. Các mức bảo đảm giao hàng (Delivery semantics)

### 2.1. At-most-once (≤ 1 lần)
- Gửi rồi quên (fire-and-forget). Không retry.
- Mất gói → message bị mất luôn.
- Ưu điểm: đơn giản, nhanh, không trùng.
- Nhược điểm: có thể mất.
- **Ứng dụng đồ án**: MQTT QoS 0 dùng cơ chế này.

### 2.2. At-least-once (≥ 1 lần)
- Gửi, chờ ACK, không có ACK thì retry.
- Mất gói data → sẽ retry → đến đích.
- Mất gói ACK → sender retry → receiver thấy **duplicate** → cần xử lý duplicate.
- **Ứng dụng đồ án**: chính là cơ chế tự xây trên QoS 0.

### 2.3. Exactly-once (đúng 1 lần)
- Đến đúng 1 lần, không thừa không thiếu.
- Cần phối hợp duplicate detection chặt chẽ ở receiver + persistence ở sender.
- Đắt nhất về tài nguyên.
- **Ứng dụng**: MQTT QoS 2, hệ thống thanh toán, financial transactions.

> Đồ án này thực hiện **at-least-once** ở tầng truyền + **idempotent processing** ở tầng ứng dụng → hành vi quan sát được là **exactly-once**.

## 3. Sequence Number (Số thứ tự)

**Sequence number** (gọi tắt là *seq*) là số nguyên đơn điệu tăng gắn vào mỗi message.

### Mục đích
1. **Phát hiện duplicate**: nếu receiver thấy seq nó đã xử lý → là gói trùng.
2. **Phát hiện missing**: nếu receiver thấy gap (ví dụ vừa nhận seq=5 sau seq=3) → biết có gói bị mất.
3. **Khôi phục thứ tự**: receiver buffer các gói đến sớm, chờ gói thiếu rồi xử lý lại theo thứ tự.
4. **Phục vụ ACK định danh**: ACK chỉ rõ đang xác nhận gói nào.

### Trong đồ án
- Sender (`current_seq`) tăng đều từ 1, gắn vào mỗi data message.
- Receiver (`expected_seq`) bắt đầu từ 1, tăng sau mỗi lần xử lý thành công.
- Receiver phân loại theo so sánh seq nhận được với `expected_seq`:
  ```
  seq < expected_seq         → DUPLICATE (đã xử lý rồi)
  seq == expected_seq        → OK, xử lý, expected_seq++
  seq > expected_seq         → OUT_OF_ORDER, lưu reorder buffer
  seq trong reorder buffer   → DUPLICATE (retry của gói đã nhận)
  ```

## 4. ACK / NACK

### Positive ACK
- Receiver gửi ACK *xác nhận đã nhận được* message X.
- Sender chỉ "yên tâm" sau khi nhận ACK.
- Trong đồ án: topic `sensor/ack` chứa `{ack_seq, status}`.

### Negative ACK (NACK)
- Receiver gửi NACK khi phát hiện *thiếu* gói (ví dụ thấy gap seq).
- Cho phép sender retry sớm hơn timeout.
- Đồ án không dùng NACK riêng — thay vào đó dùng ACK có trường `status=OUT_OF_ORDER` để báo cho sender biết.

### Cumulative ACK vs. Selective ACK
- **Cumulative ACK**: 1 ACK xác nhận tất cả seq ≤ N (như TCP cũ).
- **Selective ACK (SACK)**: ACK xác nhận đúng những seq cụ thể.
- Đồ án dùng **selective ACK**: mỗi ACK gắn đúng 1 seq → đơn giản, đủ minh họa.

## 5. Retry với Timeout

### Cơ chế
- Sau khi gửi gói, sender lưu vào *pending buffer* kèm timestamp.
- Nếu sau `RETRY_TIMEOUT_MS` mà chưa có ACK → gửi lại.
- Lặp tối đa `MAX_RETRY` lần. Vượt quá → coi là failed (trả lỗi lên tầng trên hoặc bỏ).

### Trade-off chọn timeout
- **Timeout quá ngắn**: sender retry trong khi gói gốc vẫn đang trên đường → tốn băng thông + tạo nhiều duplicate.
- **Timeout quá dài**: phát hiện mất gói chậm → throughput thấp.
- Thực tế nên *adaptive* dựa trên RTT đo được. Đồ án dùng giá trị cố định `2000ms` đủ minh họa.

### Exponential Backoff
- Mỗi lần retry tăng gấp đôi timeout: 2s → 4s → 8s.
- Tránh "thundering herd" khi mạng tắc.
- Đồ án dùng timeout cố định để demo rõ và đơn giản; có thể nâng cấp dễ dàng.

## 6. Idempotency (Khả năng thực hiện lặp lại an toàn)

Vì retry có thể tạo duplicate, **receiver phải xử lý các duplicate một cách an toàn** — gọi nhiều lần với cùng input phải có hiệu ứng giống gọi 1 lần.

### Hai cách đạt idempotency
1. **Bản chất idempotent**: ví dụ "đặt LED về trạng thái X" — gọi 2 lần cũng cho kết quả X.
2. **Deduplication chủ động**: lưu lại seq đã xử lý, gặp lại thì bỏ qua.

Đồ án dùng kết hợp:
- Hành vi *bật/tắt LED theo nhiệt độ* tự nó là idempotent.
- Receiver vẫn check `seq < expected_seq` để **bỏ xử lý lại** + gửi lại ACK cho sender unblock.

## 7. Out-of-order và Reorder Buffer

### Khi nào xảy ra
- Trong đồ án, chủ yếu do **retry**: gói seq=4 bị drop, sender retry sau 2s; trong khoảng đó gói seq=5 mới đến trước.

### Reorder Buffer
- Receiver lưu các gói có seq > expected_seq tạm thời.
- Khi seq thiếu cuối cùng đến, xử lý nó → kéo các gói liên tiếp sau ra khỏi buffer (drain).
- Đảm bảo *application layer* luôn xử lý theo đúng thứ tự ban đầu.

### Giới hạn buffer
- Buffer hữu hạn → nếu gap quá lớn (gói cũ thực sự mất sau MAX_RETRY) → phải có cơ chế giảm/đẩy expected_seq lên. Đồ án giữ đơn giản: buffer 16 slot, đủ cho demo.

## 8. Checksum / Integrity

- Mỗi data message kèm `checksum` đơn giản (XOR của seq + temp*10 + humid*10).
- Receiver tính lại và so sánh — không khớp → coi là CORRUPT, **không ACK** → sender retry.
- Trong môi trường thực, MQTT chạy trên TCP nên đã có checksum tầng dưới; ở đây thêm checksum tầng ứng dụng để minh họa khái niệm và phòng trường hợp có bug ở các tầng trên.

## 9. So sánh ngắn với MQTT QoS

| QoS | Tên | Cơ chế broker | Có thể trùng? | Đảm bảo |
|-----|-----|---------------|---------------|---------|
| 0 | At most once | Gửi 1 lần, không xác nhận | Không | Có thể mất |
| 1 | At least once | Có PUBACK; retry nếu thiếu | **Có** | Đến ≥ 1 lần |
| 2 | Exactly once | 4-way handshake (PUBREC/PUBREL/PUBCOMP) | Không | Đúng 1 lần |

> **Lý do đồ án dùng QoS 0**: nếu chọn QoS 1/2, broker đã làm sẵn tất cả → ứng dụng chỉ là client mù tịt. Dùng QoS 0 + tự xây ACK/Retry/Sequence/Reorder là cách *thể hiện rõ nhất* việc sinh viên **hiểu và tự cài** các cơ chế nền tảng.

## 10. Two Generals Problem

> Không tồn tại giao thức nào trên kênh **lossy** mà **đảm bảo cả hai bên** đều biết chắc trạng thái thống nhất sau hữu hạn vòng trao đổi.

Hệ quả thực tế:
- Sender không bao giờ chắc chắn 100% receiver đã *xử lý* message (ACK có thể mất trên đường về).
- Cách giải quyết thực dụng: chấp nhận xác suất sai sót thấp, dùng ACK + retry hữu hạn + timeout.
- Đó chính xác là những gì đồ án này làm.

## 11. Bảng tóm tắt các kỹ thuật được dùng trong đồ án

| Kỹ thuật | File | Vai trò |
|----------|------|--------|
| Sequence number | A: `current_seq`, B: `expected_seq` | Định danh + sắp xếp + phát hiện duplicate/missing |
| Pending buffer ở sender | A: `pending[]` | Lưu message chưa nhận ACK để retry |
| Timeout + retry | A: `RETRY_TIMEOUT_MS`, `MAX_RETRY` | Đảm bảo at-least-once |
| Positive ACK selective | B: `send_ack()` | Xác nhận từng seq |
| Duplicate detection | B: so sánh seq < expected, kiểm reorder buffer | Idempotent xử lý |
| Reorder buffer | B: `reorder[]` + `drain_reorder_buffer()` | Khôi phục thứ tự |
| Checksum tầng ứng dụng | A, B: `compute_checksum()` | Phát hiện corruption |
| Drop simulator | A: `drop_probability`; B: `ack_drop_probability` | Giả lập môi trường lossy để demo |
| Telemetry | `monitor/*` topics | Thống kê real-time qua Node-RED |

## 12. Tài liệu tham khảo

1. A. S. Tanenbaum, M. van Steen, *Distributed Systems: Principles and Paradigms*, 2nd ed., 2007, Ch. 4 Communication.
2. M. van Steen, A. S. Tanenbaum, *Distributed Systems*, 3rd ed., 2017, Ch. 8 Fault Tolerance.
3. *MQTT Version 3.1.1 OASIS Standard*, 2014. https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
4. RFC 793 — Transmission Control Protocol, J. Postel, 1981.
5. L. Lamport, "Time, Clocks, and the Ordering of Events in a Distributed System", *Communications of the ACM*, 21(7), 1978.
6. *Two Generals' Problem* — overview tại https://en.wikipedia.org/wiki/Two_Generals%27_Problem
