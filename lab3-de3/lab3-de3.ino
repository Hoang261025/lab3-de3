
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ========== WIFI + MQTT ==========
const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "broker.emqx.io";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ========== SENSOR ==========
#define MQ2_PIN 34
#define DHT_PIN 15
#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);

// ========== ACTUATOR ==========
#define RELAY_FAN 26
#define RELAY_PUMP 27
#define BUZZER 25

#define GAS_LIMIT 2000
#define TEMP_LIMIT 50.0

// ========== DATA STRUCT ==========
struct SensorData {
  int gas;
  float temp;
  float hum;
};

// Lệnh điều khiển
struct Command {
  int device;  // 0: Fan, 1: Pump
  int mode;    // 0: AUTO, 1: OFF, 2: ON
};

// ========== QUEUE ==========
QueueHandle_t dataQueue;
QueueHandle_t dataAlert;
QueueHandle_t commandQueue;

// ========== MANUAL CONTROL ==========
bool manualFan = false;
bool manualPump = false;

bool fanManualState = false;
bool pumpManualState = false;

// ========== MQTT TOPIC ==========
const char* sensorTopic = "hoang/fire/sensor";
const char* fanTopic = "hoang/fire/cmd/fan";
const char* pumpTopic = "hoang/fire/cmd/pump";

// ========== MQTT CALLBACK ==========
void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {

  String msg = "";

  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Command cmd;

  if (strcmp(topic, fanTopic) == 0) {
    cmd.device = 0;
  }
  else if (strcmp(topic, pumpTopic) == 0) {
    cmd.device = 1;
  }
  else {
    return;
  }

  if (msg == "ON") cmd.mode = 2;
  else if (msg == "OFF") cmd.mode = 1;
  else if (msg == "AUTO") cmd.mode = 0;
  else return;

  xQueueSend(commandQueue, &cmd, 0);
}

// ========== MQTT CONNECT ==========
void reconnectMQTT() {

  while (!mqtt.connected()) {

    String clientID = "ESP32_Fire_";
    clientID += String((uint32_t)ESP.getEfuseMac(), HEX);

    Serial.println("Connecting MQTT...");

    if (mqtt.connect(clientID.c_str())) {

      Serial.println("MQTT Connected!");

      mqtt.subscribe(fanTopic);
      mqtt.subscribe(pumpTopic);

    } else {

      Serial.println("MQTT Failed!");

      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// =================================================
// TASK 1: WIFI + MQTT - CORE 0
// =================================================
void TaskTransmit(void *pvParameters) {

  WiFi.begin(ssid, password, 6);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  Serial.println("WiFi Connected!");

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(256);

  SensorData data;

  while (1) {

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!mqtt.connected()) {
      reconnectMQTT();
    }

    mqtt.loop();

    // Nhận dữ liệu từ dataQueue
    if (xQueueReceive(dataQueue, &data,
                      pdMS_TO_TICKS(100)) == pdTRUE) {

      char payload[150];

      snprintf(payload, sizeof(payload),
        "{\"gas\":%d,\"temp\":%.1f,\"hum\":%.1f}",
        data.gas, data.temp, data.hum);

      if (mqtt.connected()) {
        mqtt.publish(sensorTopic, payload);
        Serial.println("MQTT Sent: " + String(payload));
      }
    }
  }
}

// =================================================
// TASK 2: READ SENSOR - CORE 1
// =================================================
void TaskRead(void *pvParameters) {

  SensorData data;

  while (1) {

    // Đọc cảm biến MQ2
    data.gas = analogRead(MQ2_PIN);

    // Đọc nhiệt độ và độ ẩm DHT22
    data.temp = dht.readTemperature();
    data.hum = dht.readHumidity();

    // Kiểm tra lỗi cảm biến
    if (isnan(data.temp) || isnan(data.hum)) {

      Serial.println("DHT22 Read Error!");

      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // Hiển thị Serial
    Serial.println("\n===== FIRE ALARM =====");

    Serial.print("Gas ADC: ");
    Serial.println(data.gas);

    Serial.print("Temperature: ");
    Serial.print(data.temp);
    Serial.println(" *C");

    Serial.print("Humidity: ");
    Serial.print(data.hum);
    Serial.println(" %");

    // Gửi dữ liệu vào 2 Queue
    xQueueSend(dataQueue, &data, 0);
    xQueueSend(dataAlert, &data, 0);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// =================================================
// TASK 3: ALERT + CONTROL - CORE 1
// =================================================
void TaskAlert(void *pvParameters) {

  SensorData data;
  Command cmd;

  bool fanAuto = false;
  bool pumpAuto = false;

  while (1) {

    // Nhận dữ liệu cảnh báo
    if (xQueueReceive(dataAlert, &data,
                      pdMS_TO_TICKS(100)) == pdTRUE) {

      fanAuto = (data.gas > GAS_LIMIT);
      pumpAuto = (data.temp > TEMP_LIMIT);

      Serial.println("\n===== ALERT CHECK =====");

      Serial.print("Gas Alarm: ");
      Serial.println(fanAuto ? "ON" : "OFF");

      Serial.print("Temperature Alarm: ");
      Serial.println(pumpAuto ? "ON" : "OFF");
    }

    // Nhận lệnh từ Web MQTT
    while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {

      if (cmd.device == 0) {
        manualFan = (cmd.mode != 0);
        fanManualState = (cmd.mode == 2);
      }

      if (cmd.device == 1) {
        manualPump = (cmd.mode != 0);
        pumpManualState = (cmd.mode == 2);
      }
    }

    // Điều khiển quạt và máy bơm
    bool fanState = manualFan ? fanManualState : fanAuto;
    bool pumpState = manualPump ? pumpManualState : pumpAuto;

    digitalWrite(RELAY_FAN, fanState ? HIGH : LOW);
    digitalWrite(RELAY_PUMP, pumpState ? HIGH : LOW);

    // ===== ĐIỀU KHIỂN BUZZER =====
    // Gas hoặc nhiệt độ vượt ngưỡng -> còi kêu
    if (fanAuto || pumpAuto) {
      tone(BUZZER, 2000);
    }
    else {
      noTone(BUZZER);
    }

    // Tránh Task chiếm CPU liên tục
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =================================================
// SETUP
// =================================================
void setup() {

  Serial.begin(115200);

  dht.begin();

  pinMode(MQ2_PIN, INPUT);

  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RELAY_FAN, LOW);
  digitalWrite(RELAY_PUMP, LOW);

  noTone(BUZZER);

  // Tạo Queue
  dataQueue = xQueueCreate(10, sizeof(SensorData));
  dataAlert = xQueueCreate(10, sizeof(SensorData));
  commandQueue = xQueueCreate(10, sizeof(Command));

  if (!dataQueue || !dataAlert || !commandQueue) {

    Serial.println("Queue creation failed!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // Tạo Task 1: MQTT + WiFi - Core 0
  xTaskCreatePinnedToCore(
    TaskTransmit,
    "TaskTransmit",
    8192,
    NULL,
    2,
    NULL,
    0
  );

  // Tạo Task 2: Đọc cảm biến - Core 1
  xTaskCreatePinnedToCore(
    TaskRead,
    "TaskRead",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  // Tạo Task 3: Cảnh báo - Core 1
  xTaskCreatePinnedToCore(
    TaskAlert,
    "TaskAlert",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  Serial.println("FIRE ALARM SYSTEM STARTED!");
}

// =================================================
// LOOP
// =================================================
void loop() {

  vTaskDelay(portMAX_DELAY);
}
