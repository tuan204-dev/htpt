#!/usr/bin/env bash
# =====================================================================
# setup-auth.sh - sinh mosquitto.passwd cho broker MQTT
# ---------------------------------------------------------------------
# Đọc credentials từ .env và dùng image eclipse-mosquitto để hash
# password bằng mosquitto_passwd. Output: mosquitto.passwd
# =====================================================================
set -euo pipefail

cd "$(dirname "$0")"

ENV_FILE=".env"
PASSWD_FILE="mosquitto.passwd"
IMAGE="eclipse-mosquitto:2.0"

# 1. Kiểm tra .env
if [ ! -f "$ENV_FILE" ]; then
  echo "[!] Không tìm thấy $ENV_FILE"
  echo "    Tạo trước bằng lệnh:  cp .env.example .env  rồi sửa password"
  exit 1
fi

# 2. Load credentials
set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

required_vars=(MQTT_USER_ESP32_A MQTT_PASS_ESP32_A
               MQTT_USER_ESP32_B MQTT_PASS_ESP32_B
               MQTT_USER_WEB     MQTT_PASS_WEB
               MQTT_USER_ADMIN   MQTT_PASS_ADMIN)
for v in "${required_vars[@]}"; do
  if [ -z "${!v:-}" ]; then
    echo "[!] Thiếu biến $v trong $ENV_FILE"
    exit 1
  fi
done

# 3. Đảm bảo Docker chạy
if ! docker info >/dev/null 2>&1; then
  echo "[!] Docker daemon không chạy. Mở Docker Desktop trước."
  exit 1
fi

# 4. Xoá file cũ + tạo trống
rm -f "$PASSWD_FILE"
touch "$PASSWD_FILE"

# 5. Sinh từng user
add_user() {
  local user="$1" pass="$2"
  echo "  + $user"
  docker run --rm -v "$(pwd):/data" -w /data --user 0:0 "$IMAGE" \
    mosquitto_passwd -b "$PASSWD_FILE" "$user" "$pass" >/dev/null
}

echo "[*] Sinh $PASSWD_FILE với user:"
add_user "$MQTT_USER_ESP32_A" "$MQTT_PASS_ESP32_A"
add_user "$MQTT_USER_ESP32_B" "$MQTT_PASS_ESP32_B"
add_user "$MQTT_USER_WEB"     "$MQTT_PASS_WEB"
add_user "$MQTT_USER_ADMIN"   "$MQTT_PASS_ADMIN"

# 6. Set quyền - mosquitto yêu cầu file không world-readable
chmod 0600 "$PASSWD_FILE"

echo
echo "[✓] Đã tạo $(pwd)/$PASSWD_FILE"
echo "[i] Restart broker để load: docker compose restart"
echo "[i] Test:"
echo "    docker run --rm -it --network host $IMAGE \\"
echo "      mosquitto_sub -h localhost -u $MQTT_USER_ADMIN -P '<password>' -t '#' -v"
