# HTPT - Docker setup cho Mosquitto (có Authentication)

Chạy MQTT broker qua Docker, có xác thực username/password và phân quyền ACL theo từng user.

## Yêu cầu

- **Docker Desktop** (macOS/Windows) hoặc **Docker Engine + Docker Compose** (Linux)
- Kiểm tra: `docker --version` và `docker compose version` chạy được

## Cấu trúc

```
docker/
├── docker-compose.yml      # service mosquitto + mount config/passwd/acl
├── mosquitto.conf          # broker config (paths theo container)
├── mosquitto.acl           # phân quyền publish/subscribe theo user
├── mosquitto.passwd        # password đã hash (TỰ TẠO, gitignored)
├── .env.example            # template credentials (commit lên git)
├── .env                    # credentials thật (TỰ TẠO, gitignored)
├── setup-auth.sh           # script sinh mosquitto.passwd từ .env
└── README.md               # file này
```

## Quy trình lần đầu (one-time setup)

```bash
cd docker

# 1. Tạo file credentials thật từ template
cp .env.example .env

# 2. (Tuỳ chọn) sửa password trong .env nếu muốn đổi mặc định
$EDITOR .env

# 3. Sinh mosquitto.passwd (đã hash) - script gọi docker run mosquitto_passwd
./setup-auth.sh

# 4. Khởi động broker
docker compose up -d
docker compose logs -f       # xem log để chắc auth load OK
```

Sau bước 3, file `mosquitto.passwd` được tạo với nội dung như:
```
esp32_a:$7$101$abc...
esp32_b:$7$101$def...
webserver:$7$101$ghi...
admin:$7$101$jkl...
```

## Các tài khoản mặc định

| Username | Password (sửa trong .env) | Quyền (ACL) |
|----------|---------------------------|-------------|
| `esp32_a` | `esp32a_pass_2026` | Publish `sensor/data`, `monitor/{sent,retry,failed,stats/A}` <br>Subscribe `sensor/ack`, `control/drop_rate` |
| `esp32_b` | `esp32b_pass_2026` | Publish `sensor/ack`, `monitor/{received,processed,stats/B}` <br>Subscribe `sensor/data`, `control/{threshold,ack_drop_rate}` |
| `webserver` | `web_pass_2026` | Subscribe `sensor/#`, `monitor/#` <br>Publish `control/{drop_rate,ack_drop_rate,threshold}` |
| `admin` | `admin_pass_2026` | Toàn quyền `#` (chỉ dùng debug) |

> Nếu đổi password trong `.env`, nhớ:
> 1. Chạy lại `./setup-auth.sh` để regenerate `mosquitto.passwd`
> 2. Sửa `MQTT_USER`/`MQTT_PASS` trong code ESP32 (file `.ino`)
> 3. Đổi biến môi trường `MQTT_USER`/`MQTT_PASS` khi chạy web server (nếu khác mặc định)
> 4. `docker compose restart` để broker load lại

## Test broker có auth chưa

```bash
# Sai password - phải bị từ chối
mosquitto_sub -h localhost -u admin -P wrong_pass -t '#' -v
# Expected: Connection error: Connection Refused: not authorised.

# Đúng password - chạy được
mosquitto_sub -h localhost -u admin -P admin_pass_2026 -t '#' -v
```

Hoặc dùng container có sẵn mosquitto_sub:
```bash
docker run --rm -it --network host eclipse-mosquitto:2.0 \
  mosquitto_sub -h localhost -u admin -P admin_pass_2026 -t '#' -v
```

Khi cả 2 ESP32 và web đã chạy, terminal sẽ in liên tục các message bay qua.

## Vận hành

```bash
docker compose up -d          # khởi động (nền)
docker compose logs -f        # xem log real-time
docker compose ps             # kiểm tra trạng thái container
docker compose restart        # restart sau khi sửa config/passwd
docker compose down           # dừng, data vẫn còn
docker compose down -v        # dừng + xoá data volume
```

## Cấu hình code ESP32 và web

Code đã được pre-config với password mặc định trong `.env.example`. Nếu bạn **không** đổi password thì không cần sửa gì. Nếu đổi:

**Code ESP32 (`esp32_a_sender.ino` / `esp32_b_receiver.ino`)** — sửa 2 dòng:
```cpp
const char* MQTT_USER = "esp32_a";          // hoặc esp32_b
const char* MQTT_PASS = "esp32a_pass_2026"; // hoặc password mới
```

**Web server** — đặt biến môi trường khi chạy:
```bash
MQTT_USER=webserver MQTT_PASS=web_pass_2026 npm start
```
Hoặc tạo file `web/.env` (đã gitignored) rồi load bằng `node --env-file=.env server.js`.

## Troubleshooting

| Triệu chứng | Xử lý |
|------------|-------|
| `Error: Unable to open password file` | Chưa chạy `./setup-auth.sh` hoặc file `mosquitto.passwd` không tồn tại |
| `Permissions on password file are insecure` | `chmod 0600 mosquitto.passwd` |
| ESP32 log `[MQTT] FAIL rc=5` | rc=5 = NOT_AUTHORIZED → sai username/password hoặc ACL chặn |
| ESP32 log `[MQTT] FAIL rc=4` | rc=4 = BAD_USERNAME_PASSWORD → credentials sai |
| `setup-auth.sh: docker daemon not running` | Mở Docker Desktop trước |
| Container restart liên tục | `docker compose logs` xem - thường do mosquitto.conf hoặc acl có syntax sai |

## So sánh trước và sau khi thêm auth

| | Không auth | Có auth (config hiện tại) |
|---|---|---|
| Bất kỳ ai có IP broker đều connect được | ✅ | ❌ |
| Bất kỳ user nào đều publish/subscribe mọi topic | ✅ | ❌ (ACL phân quyền) |
| Dễ debug | ✅ | Phải nhớ password |
| An toàn | ❌ | ✅ |
| Phù hợp cho | Bench test cá nhân | Bài tập + demo có giáo viên đánh giá |

ACL chặt giúp thể hiện thêm khía cạnh **security trong distributed messaging** cho báo cáo của bạn — có thể thêm 1 mục nhỏ trong `report/theory.md` về tầng xác thực.

## Quay về không auth (nếu cần)

Edit `mosquitto.conf`:
```
allow_anonymous true
# password_file ...    (comment dòng này)
# acl_file ...         (comment dòng này)
```
Trong code ESP32 đổi `mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)` thành `mqtt.connect(CLIENT_ID)`. Trong web bỏ `username`/`password` khỏi options.
