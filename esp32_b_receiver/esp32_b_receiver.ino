/*
  =====================================================================
  ESP32_B - Receiver Node (Subscriber + LED Actuator)
  Đồ án Hệ thống phân tán - Reliable Message Delivery qua MQTT
  ---------------------------------------------------------------------
  Chức năng:
    - Subscribe topic `sensor/data` để nhận message từ ESP32_A
    - Kiểm tra checksum + sequence number → phân loại:
        * OK            : đúng thứ tự, xử lý + ACK
        * DUPLICATE     : đã thấy, KHÔNG xử lý lại nhưng vẫn ACK (idempotent)
        * OUT_OF_ORDER  : seq > expected, lưu vào reorder buffer
        * OLD/LATE      : seq < expected, coi như duplicate
    - Gửi ACK lên topic `sensor/ack` (có thể giả lập mất ACK)
    - Điều khiển LED_MAIN theo ngưỡng nhiệt độ
    - LED báo trạng thái:
        * LED_MAIN   (GPIO 14): bật khi temp > TEMP_THRESHOLD
        * LED_GREEN  (GPIO 25): message hợp lệ đúng thứ tự
        * LED_YELLOW (GPIO 26): duplicate
        * LED_RED    (GPIO 27): out-of-order / missing
  ---------------------------------------------------------------------
  Thư viện cần cài (Library Manager):
    - PubSubClient (Nick O'Leary)
    - ArduinoJson  (Benoit Blanchon)
  ---------------------------------------------------------------------
  TRƯỚC KHI NẠP: sửa WIFI_SSID, WIFI_PASS, MQTT_BROKER (IP laptop)
  =====================================================================
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===================== CẤU HÌNH (SỬA TRƯỚC KHI NẠP) =====================
const char*    WIFI_SSID   = "YOUR_WIFI_SSID";
const char*    WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char*    MQTT_BROKER = "192.168.1.100";   // IP laptop chạy Mosquitto
const uint16_t MQTT_PORT   = 1883;
const char*    CLIENT_ID   = "ESP32_B_Receiver";

// ===================== CHÂN PHẦN CỨNG =====================
#define LED_MAIN_PIN    14   // Actuator chính - bật khi nóng
#define LED_GREEN_PIN   25   // OK đúng thứ tự
#define LED_YELLOW_PIN  26   // Duplicate
#define LED_RED_PIN     27   // Out-of-order / missing

// ===================== THAM SỐ =====================
float    TEMP_THRESHOLD     = 30.0;   // có thể điều khiển qua Node-RED
float    ack_drop_probability = 0.0;  // giả lập mất ACK
#define  REORDER_BUFFER_SIZE 16

const unsigned long STATS_PUBLISH_MS = 1000;

// ===================== TOPICS MQTT =====================
const char* T_DATA            = "sensor/data";
const char* T_ACK             = "sensor/ack";
const char* T_MON_RECEIVED    = "monitor/received";
const char* T_MON_PROCESSED   = "monitor/processed";
const char* T_MON_STATS_B     = "monitor/stats/B";
const char* T_CTL_THRESHOLD   = "control/threshold";
const char* T_CTL_ACK_DROP    = "control/ack_drop_rate";

// ===================== TRẠNG THÁI =====================
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

uint32_t expected_seq = 1;   // seq tiếp theo cần xử lý

struct ReorderEntry {
  bool     active;
  uint32_t seq;
  float    temp;
  float    humid;
};
ReorderEntry reorder[REORDER_BUFFER_SIZE];

// Thống kê
uint32_t stat_received_total = 0;
uint32_t stat_ok             = 0;
uint32_t stat_duplicate      = 0;
uint32_t stat_out_of_order   = 0;
uint32_t stat_corrupt        = 0;
uint32_t stat_ack_sent       = 0;
uint32_t stat_ack_dropped    = 0;

unsigned long last_stats_publish = 0;

// LED non-blocking
struct LedState { int pin; unsigned long off_time; };
LedState led_green  = {LED_GREEN_PIN,  0};
LedState led_yellow = {LED_YELLOW_PIN, 0};
LedState led_red    = {LED_RED_PIN,    0};

// ===================== HÀM TIỆN ÍCH =====================
void blink(LedState &led, unsigned long duration_ms = 150) {
  digitalWrite(led.pin, HIGH);
  led.off_time = millis() + duration_ms;
}

void update_leds() {
  unsigned long now = millis();
  if (led_green.off_time  && now >= led_green.off_time)  { digitalWrite(led_green.pin,  LOW); led_green.off_time  = 0; }
  if (led_yellow.off_time && now >= led_yellow.off_time) { digitalWrite(led_yellow.pin, LOW); led_yellow.off_time = 0; }
  if (led_red.off_time    && now >= led_red.off_time)    { digitalWrite(led_red.pin,    LOW); led_red.off_time    = 0; }
}

uint16_t compute_checksum(uint32_t seq, float temp, float humid) {
  uint16_t cs = (uint16_t)(seq & 0xFFFF);
  cs ^= (uint16_t)(temp * 10);
  cs ^= (uint16_t)(humid * 10);
  return cs;
}

int find_reorder_slot_by_seq(uint32_t seq) {
  for (int i = 0; i < REORDER_BUFFER_SIZE; i++)
    if (reorder[i].active && reorder[i].seq == seq) return i;
  return -1;
}

int find_free_reorder_slot() {
  for (int i = 0; i < REORDER_BUFFER_SIZE; i++)
    if (!reorder[i].active) return i;
  return -1;
}

// ===================== ACK + xử lý nhiệt độ =====================
void send_ack(uint32_t seq, const char* status) {
  StaticJsonDocument<128> doc;
  doc["ack_seq"]  = seq;
  doc["receiver"] = CLIENT_ID;
  doc["status"]   = status;
  doc["ts"]       = millis();
  char buf[128];
  size_t n = serializeJson(doc, buf);

  // ACK drop simulator (để demo trường hợp ACK bị mất)
  float r = (float)random(0, 10000) / 10000.0;
  if (r < ack_drop_probability) {
    Serial.printf("[ACK-DROP-SIM] seq=%u status=%s bị drop\n", seq, status);
    stat_ack_dropped++;
    return;
  }

  mqtt.publish(T_ACK, (const uint8_t*)buf, n, false);
  stat_ack_sent++;
}

void apply_actuator(float temp, float humid) {
  if (temp > TEMP_THRESHOLD) {
    digitalWrite(LED_MAIN_PIN, HIGH);
  } else {
    digitalWrite(LED_MAIN_PIN, LOW);
  }

  // log lên monitor/processed
  StaticJsonDocument<128> doc;
  doc["expected_seq"] = expected_seq;
  doc["temp"]         = temp;
  doc["humid"]        = humid;
  doc["led_main"]     = (temp > TEMP_THRESHOLD) ? "ON" : "OFF";
  doc["threshold"]    = TEMP_THRESHOLD;
  char buf[128];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(T_MON_PROCESSED, (const uint8_t*)buf, n, false);
}

// Khi đã process seq=expected_seq, tăng expected_seq lên
// rồi kéo các seq tiếp theo (nếu có) ra khỏi reorder buffer
void drain_reorder_buffer() {
  while (true) {
    int idx = find_reorder_slot_by_seq(expected_seq);
    if (idx < 0) break;
    Serial.printf("[DRAIN] xử lý seq=%u từ reorder buffer\n", expected_seq);
    apply_actuator(reorder[idx].temp, reorder[idx].humid);
    blink(led_green);
    send_ack(reorder[idx].seq, "OK");
    stat_ok++;
    reorder[idx].active = false;
    expected_seq++;
  }
}

// ===================== XỬ LÝ DATA NHẬN ĐƯỢC =====================
void handle_data_message(const byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println("[DATA] JSON sai cú pháp, bỏ qua");
    return;
  }

  uint32_t seq      = doc["seq"];
  float    temp     = doc["temp"];
  float    humid    = doc["humid"];
  uint16_t got_cs   = doc["checksum"];
  uint16_t calc_cs  = compute_checksum(seq, temp, humid);

  stat_received_total++;

  StaticJsonDocument<128> mon;
  mon["seq"]      = seq;
  mon["expected"] = expected_seq;

  // 1) Checksum sai → CORRUPT, không ACK (sender sẽ retry)
  if (got_cs != calc_cs) {
    Serial.printf("[CORRUPT] seq=%u checksum got=%u calc=%u\n", seq, got_cs, calc_cs);
    stat_corrupt++;
    blink(led_red);
    mon["status"] = "CORRUPT";
    char b[128]; size_t n = serializeJson(mon, b);
    mqtt.publish(T_MON_RECEIVED, (const uint8_t*)b, n, false);
    return;
  }

  // 2) seq < expected → đã xử lý → DUPLICATE (gửi lại ACK để sender unblock)
  if (seq < expected_seq) {
    Serial.printf("[DUPLICATE-OLD] seq=%u (expected=%u) → gửi lại ACK\n", seq, expected_seq);
    stat_duplicate++;
    blink(led_yellow);
    send_ack(seq, "DUPLICATE");
    mon["status"] = "DUPLICATE_OLD";
    char b[128]; size_t n = serializeJson(mon, b);
    mqtt.publish(T_MON_RECEIVED, (const uint8_t*)b, n, false);
    return;
  }

  // 3) seq trong reorder buffer → cũng là DUPLICATE (sender retry vì ACK trước bị mất)
  if (find_reorder_slot_by_seq(seq) >= 0) {
    Serial.printf("[DUPLICATE-BUF] seq=%u đã có trong reorder buffer → gửi lại ACK\n", seq);
    stat_duplicate++;
    blink(led_yellow);
    send_ack(seq, "DUPLICATE");
    mon["status"] = "DUPLICATE_BUF";
    char b[128]; size_t n = serializeJson(mon, b);
    mqtt.publish(T_MON_RECEIVED, (const uint8_t*)b, n, false);
    return;
  }

  // 4) seq == expected → xử lý ngay, ACK, rồi kéo các seq tiếp theo từ reorder
  if (seq == expected_seq) {
    Serial.printf("[OK] seq=%u temp=%.1f humid=%.1f → process\n", seq, temp, humid);
    apply_actuator(temp, humid);
    stat_ok++;
    blink(led_green);
    send_ack(seq, "OK");
    expected_seq++;
    drain_reorder_buffer();
    mon["status"] = "OK";
    char b[128]; size_t n = serializeJson(mon, b);
    mqtt.publish(T_MON_RECEIVED, (const uint8_t*)b, n, false);
    return;
  }

  // 5) seq > expected → OUT_OF_ORDER, lưu lại chờ seq trước đến
  Serial.printf("[OUT_OF_ORDER] seq=%u (expected=%u) → lưu reorder buffer\n",
                seq, expected_seq);
  stat_out_of_order++;
  blink(led_red);
  int slot = find_free_reorder_slot();
  if (slot >= 0) {
    reorder[slot] = { true, seq, temp, humid };
  } else {
    Serial.println("[REORDER] Buffer đầy, drop msg out-of-order này");
  }
  // Vẫn ACK để sender không retry vô ích — ACK xác nhận đã nhận, không xác nhận đã xử lý
  send_ack(seq, "OUT_OF_ORDER");
  mon["status"] = "OUT_OF_ORDER";
  char b[128]; size_t n = serializeJson(mon, b);
  mqtt.publish(T_MON_RECEIVED, (const uint8_t*)b, n, false);
}

// ===================== WIFI + MQTT =====================
void setup_wifi() {
  Serial.printf("[WiFi] Connecting to %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] OK. IP=%s\n", WiFi.localIP().toString().c_str());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, T_DATA) == 0) {
    handle_data_message(payload, length);
    return;
  }
  if (strcmp(topic, T_CTL_THRESHOLD) == 0) {
    char buf[16];
    unsigned int n = length < 15 ? length : 15;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    TEMP_THRESHOLD = atof(buf);
    Serial.printf("[CTL] TEMP_THRESHOLD = %.1f\n", TEMP_THRESHOLD);
    return;
  }
  if (strcmp(topic, T_CTL_ACK_DROP) == 0) {
    char buf[16];
    unsigned int n = length < 15 ? length : 15;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    float v = atof(buf);
    if (v < 0) v = 0; if (v > 1) v = 1;
    ack_drop_probability = v;
    Serial.printf("[CTL] ack_drop_probability = %.2f\n", ack_drop_probability);
    return;
  }
}

void connect_mqtt() {
  while (!mqtt.connected()) {
    Serial.printf("[MQTT] Connecting to %s:%u ... ", MQTT_BROKER, MQTT_PORT);
    if (mqtt.connect(CLIENT_ID)) {
      Serial.println("OK");
      mqtt.subscribe(T_DATA,           0);
      mqtt.subscribe(T_CTL_THRESHOLD,  0);
      mqtt.subscribe(T_CTL_ACK_DROP,   0);
      Serial.printf("[MQTT] Subscribed: %s, %s, %s\n",
                    T_DATA, T_CTL_THRESHOLD, T_CTL_ACK_DROP);
    } else {
      Serial.printf("FAIL rc=%d, retry sau 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

void publish_stats() {
  StaticJsonDocument<256> doc;
  doc["expected_seq"]    = expected_seq;
  doc["received_total"]  = stat_received_total;
  doc["ok"]              = stat_ok;
  doc["duplicate"]       = stat_duplicate;
  doc["out_of_order"]    = stat_out_of_order;
  doc["corrupt"]         = stat_corrupt;
  doc["ack_sent"]        = stat_ack_sent;
  doc["ack_dropped"]     = stat_ack_dropped;
  doc["ack_drop_rate"]   = ack_drop_probability;
  doc["threshold"]       = TEMP_THRESHOLD;
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(T_MON_STATS_B, (const uint8_t*)buf, n, false);
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32_B Receiver khởi động ===");

  pinMode(LED_MAIN_PIN,   OUTPUT);
  pinMode(LED_GREEN_PIN,  OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN,    OUTPUT);
  digitalWrite(LED_MAIN_PIN,   LOW);
  digitalWrite(LED_GREEN_PIN,  LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN,    LOW);

  randomSeed(esp_random());

  setup_wifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  mqtt.setBufferSize(512);

  for (int i = 0; i < REORDER_BUFFER_SIZE; i++) reorder[i].active = false;
}

void loop() {
  if (!mqtt.connected()) connect_mqtt();
  mqtt.loop();
  update_leds();

  unsigned long now = millis();
  if (now - last_stats_publish >= STATS_PUBLISH_MS) {
    last_stats_publish = now;
    publish_stats();
  }
}
