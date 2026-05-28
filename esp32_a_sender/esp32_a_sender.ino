/*
  =====================================================================
  ESP32_A - Sender Node (Publisher + DHT11)
  Đồ án Hệ thống phân tán - Reliable Message Delivery qua MQTT
  ---------------------------------------------------------------------
  Chức năng:
    - Đọc DHT11 (nhiệt độ + độ ẩm) mỗi SEND_INTERVAL_MS
    - Gắn sequence number tăng dần cho mỗi message
    - Publish lên topic `sensor/data` với MQTT QoS 0 (fire-and-forget)
    - Tự cài tầng tin cậy:
        * Chờ ACK trên topic `sensor/ack` với timeout
        * Retry tối đa MAX_RETRY lần
        * Drop simulator (giả lập mất gói) điều khiển từ Node-RED
    - LED báo trạng thái:
        * LED_GREEN  (GPIO 25): nhận được ACK đúng
        * LED_YELLOW (GPIO 26): đang retry
        * LED_RED    (GPIO 27): fail sau MAX_RETRY lần
  ---------------------------------------------------------------------
  Thư viện cần cài (Library Manager):
    - PubSubClient (Nick O'Leary)
    - DHT sensor library (Adafruit)
    - Adafruit Unified Sensor
    - ArduinoJson (Benoit Blanchon)
  ---------------------------------------------------------------------
  TRƯỚC KHI NẠP: sửa WIFI_SSID, WIFI_PASS, MQTT_BROKER (IP laptop)
  =====================================================================
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ===================== CẤU HÌNH (SỬA TRƯỚC KHI NẠP) =====================
const char*    WIFI_SSID   = "iPhone 3";
const char*    WIFI_PASS   = "22224444";
const char*    MQTT_BROKER = "172.20.10.3";   // IP laptop chạy Mosquitto
const uint16_t MQTT_PORT   = 1883;
const char*    CLIENT_ID   = "ESP32_A_Sender";
const char*    MQTT_USER   = "esp32_a";          // khớp với .env trong docker/
const char*    MQTT_PASS   = "esp32a_pass_2026";

// ===================== CHÂN PHẦN CỨNG =====================
#define DHT_PIN         4
#define DHT_TYPE        DHT11
#define LED_GREEN_PIN   25   // ACK OK
#define LED_YELLOW_PIN  26   // Retrying
#define LED_RED_PIN     27   // Failed

// ===================== THAM SỐ GIAO THỨC =====================
const unsigned long SEND_INTERVAL_MS  = 2000;   // mỗi 2s gửi 1 lần
const unsigned long RETRY_TIMEOUT_MS  = 2000;   // chờ ACK 2s
const uint8_t       MAX_RETRY         = 3;      // retry tối đa 3 lần
const unsigned long RETRY_CHECK_MS    = 200;    // tần suất quét pending
const unsigned long STATS_PUBLISH_MS  = 1000;   // tần suất gửi thống kê
#define PENDING_BUFFER_SIZE 16

// ===================== TOPICS MQTT =====================
const char* T_DATA          = "sensor/data";
const char* T_ACK           = "sensor/ack";
const char* T_MON_SENT      = "monitor/sent";
const char* T_MON_RETRY     = "monitor/retry";
const char* T_MON_FAILED    = "monitor/failed";
const char* T_MON_STATS_A   = "monitor/stats/A";
const char* T_CTL_DROP_RATE = "control/drop_rate";

// ===================== TRẠNG THÁI =====================
WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);
DHT           dht(DHT_PIN, DHT_TYPE);

uint32_t current_seq      = 0;
float    drop_probability = 0.0;   // 0.0 → 1.0, điều khiển từ Node-RED

struct PendingMsg {
  bool          active;
  uint32_t      seq;
  unsigned long send_time;       // thời điểm gửi gần nhất
  unsigned long first_send_time; // thời điểm gửi lần đầu (để tính RTT)
  uint8_t       retry_count;
  float         temp;
  float         humid;
};
PendingMsg pending[PENDING_BUFFER_SIZE];

// Thống kê tích lũy
uint32_t stat_sent_total = 0;   // số lần publish thật (kể cả retry)
uint32_t stat_dropped    = 0;   // bị drop bởi simulator
uint32_t stat_retries    = 0;
uint32_t stat_ack_ok     = 0;
uint32_t stat_failed     = 0;
uint32_t stat_unique_seq = 0;   // số seq khác nhau đã tạo

unsigned long last_send_time     = 0;
unsigned long last_retry_check   = 0;
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

int find_pending_by_seq(uint32_t seq) {
  for (int i = 0; i < PENDING_BUFFER_SIZE; i++)
    if (pending[i].active && pending[i].seq == seq) return i;
  return -1;
}

int find_free_slot() {
  for (int i = 0; i < PENDING_BUFFER_SIZE; i++)
    if (!pending[i].active) return i;
  return -1;
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
  // Topic 1: sensor/ack  → xử lý ACK
  if (strcmp(topic, T_ACK) == 0) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, length)) return;
    uint32_t ack_seq = doc["ack_seq"];
    const char* status = doc["status"] | "OK";

    int idx = find_pending_by_seq(ack_seq);
    if (idx >= 0) {
      unsigned long rtt = millis() - pending[idx].first_send_time;
      Serial.printf("[ACK] seq=%u status=%s rtt=%lums (retry=%u)\n",
                    ack_seq, status, rtt, pending[idx].retry_count);
      pending[idx].active = false;
      stat_ack_ok++;
      blink(led_green);
    } else {
      // ACK cho seq đã xóa khỏi buffer (đã ACK rồi hoặc đã fail)
      Serial.printf("[ACK] seq=%u (đã xử lý hoặc fail trước đó)\n", ack_seq);
    }
    return;
  }

  // Topic 2: control/drop_rate  → cập nhật drop probability
  if (strcmp(topic, T_CTL_DROP_RATE) == 0) {
    char buf[16];
    unsigned int n = length < 15 ? length : 15;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    float new_rate = atof(buf);
    if (new_rate < 0) new_rate = 0;
    if (new_rate > 1) new_rate = 1;
    drop_probability = new_rate;
    Serial.printf("[CTL] drop_probability = %.2f\n", drop_probability);
    return;
  }
}

void connect_mqtt() {
  while (!mqtt.connected()) {
    Serial.printf("[MQTT] Connecting to %s:%u ... ", MQTT_BROKER, MQTT_PORT);
    if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");
      mqtt.subscribe(T_ACK, 0);
      mqtt.subscribe(T_CTL_DROP_RATE, 0);
      Serial.printf("[MQTT] Subscribed: %s, %s\n", T_ACK, T_CTL_DROP_RATE);
    } else {
      Serial.printf("FAIL rc=%d, retry sau 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// ===================== LOGIC CHÍNH =====================
// Hàm publish có kèm "drop simulator" - giả lập mất gói ở tầng ứng dụng
bool publish_data_with_drop_sim(uint32_t seq, float temp, float humid) {
  StaticJsonDocument<256> doc;
  doc["seq"]      = seq;
  doc["ts"]       = millis();
  doc["sender"]   = CLIENT_ID;
  doc["temp"]     = temp;
  doc["humid"]    = humid;
  doc["checksum"] = compute_checksum(seq, temp, humid);

  char buf[256];
  size_t n = serializeJson(doc, buf);

  // Drop simulator: random < drop_probability → KHÔNG publish
  float r = (float)random(0, 10000) / 10000.0;
  if (r < drop_probability) {
    Serial.printf("[DROP-SIM] seq=%u bị drop (r=%.3f < p=%.2f)\n", seq, r, drop_probability);
    stat_dropped++;
    return false;
  }

  mqtt.publish(T_DATA, (const uint8_t*)buf, n, false);
  stat_sent_total++;
  return true;
}

void send_new_message() {
  float temp  = dht.readTemperature();
  float humid = dht.readHumidity();
  if (isnan(temp) || isnan(humid)) {
    Serial.println("[DHT11] Đọc lỗi, bỏ qua chu kỳ này");
    return;
  }

  int slot = find_free_slot();
  if (slot < 0) {
    Serial.println("[PENDING] Buffer đầy, bỏ qua chu kỳ này");
    return;
  }

  current_seq++;
  stat_unique_seq++;
  unsigned long now = millis();
  pending[slot] = { true, current_seq, now, now, 0, temp, humid };

  Serial.printf("[SEND] seq=%u temp=%.1f humid=%.1f\n", current_seq, temp, humid);
  publish_data_with_drop_sim(current_seq, temp, humid);

  // monitor log
  StaticJsonDocument<128> mon;
  mon["seq"]   = current_seq;
  mon["temp"]  = temp;
  mon["humid"] = humid;
  char mbuf[128];
  size_t mn = serializeJson(mon, mbuf);
  mqtt.publish(T_MON_SENT, (const uint8_t*)mbuf, mn, false);
}

void check_retries() {
  unsigned long now = millis();
  for (int i = 0; i < PENDING_BUFFER_SIZE; i++) {
    if (!pending[i].active) continue;
    if (now - pending[i].send_time < RETRY_TIMEOUT_MS) continue;

    if (pending[i].retry_count < MAX_RETRY) {
      pending[i].retry_count++;
      pending[i].send_time = now;
      stat_retries++;
      Serial.printf("[RETRY] seq=%u attempt=%u/%u\n",
                    pending[i].seq, pending[i].retry_count, MAX_RETRY);
      blink(led_yellow);
      publish_data_with_drop_sim(pending[i].seq, pending[i].temp, pending[i].humid);

      // monitor log
      StaticJsonDocument<128> mon;
      mon["seq"]   = pending[i].seq;
      mon["retry"] = pending[i].retry_count;
      char mbuf[128];
      size_t mn = serializeJson(mon, mbuf);
      mqtt.publish(T_MON_RETRY, (const uint8_t*)mbuf, mn, false);
    } else {
      Serial.printf("[FAILED] seq=%u sau %u lần retry\n",
                    pending[i].seq, MAX_RETRY);
      blink(led_red, 2000);
      stat_failed++;
      pending[i].active = false;

      StaticJsonDocument<128> mon;
      mon["seq"] = pending[i].seq;
      char mbuf[128];
      size_t mn = serializeJson(mon, mbuf);
      mqtt.publish(T_MON_FAILED, (const uint8_t*)mbuf, mn, false);
    }
  }
}

void publish_stats() {
  StaticJsonDocument<256> doc;
  doc["sent_total"] = stat_sent_total;
  doc["unique_seq"] = stat_unique_seq;
  doc["dropped"]    = stat_dropped;
  doc["retries"]    = stat_retries;
  doc["ack_ok"]     = stat_ack_ok;
  doc["failed"]     = stat_failed;
  doc["drop_rate"]  = drop_probability;
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(T_MON_STATS_A, (const uint8_t*)buf, n, false);
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32_A Sender khởi động ===");

  pinMode(LED_GREEN_PIN,  OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN,    OUTPUT);
  digitalWrite(LED_GREEN_PIN,  LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN,    LOW);

  randomSeed(esp_random());

  dht.begin();
  setup_wifi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  mqtt.setBufferSize(512);

  for (int i = 0; i < PENDING_BUFFER_SIZE; i++) pending[i].active = false;
}

void loop() {
  if (!mqtt.connected()) connect_mqtt();
  mqtt.loop();
  update_leds();

  unsigned long now = millis();

  if (now - last_send_time >= SEND_INTERVAL_MS) {
    last_send_time = now;
    send_new_message();
  }

  if (now - last_retry_check >= RETRY_CHECK_MS) {
    last_retry_check = now;
    check_retries();
  }

  if (now - last_stats_publish >= STATS_PUBLISH_MS) {
    last_stats_publish = now;
    publish_stats();
  }
}
