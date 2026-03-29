#include "config.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <WiFiClientSecureBearSSL.h>

namespace {
// 本程序用于把 STM32 发来的停车场数据转发到云端 HTTP 接口。
// STM32 通过串口发送按行分隔的 key=value 文本帧，并用 END 表示一帧结束。
// ESP8266 负责解析串口帧、组装 JSON，并在 WiFi 或 HTTP 暂时异常时
// 先把数据缓存起来，等网络恢复后再重试上传。

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// 考虑到 ESP8266 RAM 有限，这里的缓冲区和队列尺寸保持得比较小。
constexpr size_t kLineBufferSize = 96;
constexpr size_t kQueueSize = 8;
constexpr size_t kPayloadSize = 320;

// 大多数 ESP8266 开发板的板载 LED 都是低电平点亮。
constexpr uint8_t kWifiLedPin = LED_BUILTIN;
constexpr uint8_t kWifiLedOn = LOW;
constexpr uint8_t kWifiLedOff = HIGH;

// 当前方案只使用 STM32 -> ESP8266 的单向串口通信。
constexpr uint8_t kStm32RxPin = 14;  // D5 / GPIO14
constexpr uint8_t kStm32TxPin = 12;  // D6 / GPIO12, reserved
constexpr uint32_t kDebugBaudRate = 115200;
constexpr uint32_t kStm32BaudRate = 9600;

SoftwareSerial g_stm32Serial(kStm32RxPin, kStm32TxPin);

// 保存一帧 STM32 串口数据中解析出来的字段。
struct FrameState {
  String type;
  String eventName;
  String plate;
  uint32_t total = 0;
  uint32_t active = 0;
  uint32_t freeSlots = 0;
  uint32_t inTs = 0;
  uint32_t outTs = 0;
  uint32_t fee = 0;
};

// 固定长度的待上传请求项，避免频繁动态分配内存。
struct PendingRequest {
  bool used = false;
  char payload[kPayloadSize];
};

// 串口接收到的数据先按“单行文本”方式拼接到缓冲区里。
char g_lineBuffer[kLineBufferSize];
size_t g_lineLen = 0;

// 当前正在从 STM32 输入流中解析的那一帧数据。
FrameState g_frame;

// 上传队列的队头固定放在 g_queue[0]。
PendingRequest g_queue[kQueueSize];

// 通过 millis() 记录重试时间，避免主循环被阻塞。
unsigned long g_lastWifiAttemptMs = 0;
unsigned long g_lastUploadAttemptMs = 0;
bool g_wifiWasConnected = false;
bool g_wifiConnectAttemptActive = false;

// 记录上一份状态快照，避免重复状态反复入队，占用带宽和队列空间。
String g_lastStatusPayload;

void logLine(const String &message) {
  Serial.println(message);
}

void setWifiLed(bool connected) {
  digitalWrite(kWifiLedPin, connected ? kWifiLedOn : kWifiLedOff);
}

void resetFrame() {
  // 开始解析下一帧前，先把上一帧留下的字段清空。
  g_frame = FrameState{};
}

String escapeJson(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == '\\' || c == '"') {
      output += '\\';
    }
    if (c == '\r' || c == '\n') {
      continue;
    }
    output += c;
  }
  return output;
}

bool isEventPayloadText(const char *payload) {
  return payload && strstr(payload, "\"type\":\"event\"") != nullptr;
}

bool queueContainsPayload(const String &payload) {
  for (size_t i = 0; i < kQueueSize; ++i) {
    if (g_queue[i].used && payload.equals(g_queue[i].payload)) {
      return true;
    }
  }
  return false;
}

bool replaceQueuedStatus(const String &payload) {
  // 队列满时优先保留事件消息，用最新状态覆盖较旧的状态快照。
  for (size_t i = 0; i < kQueueSize; ++i) {
    if (!g_queue[i].used) {
      continue;
    }
    if (isEventPayloadText(g_queue[i].payload)) {
      continue;
    }
    payload.substring(0, kPayloadSize - 1).toCharArray(g_queue[i].payload, kPayloadSize);
    logLine("[QUEUE] replace stale status");
    logLine(payload);
    return true;
  }
  return false;
}

bool enqueuePayload(const String &payload, bool isEvent) {
  // 状态帧本质上是快照，重复内容没有必要再次入队。
  if (!isEvent) {
    if (payload == g_lastStatusPayload || queueContainsPayload(payload)) {
      return false;
    }
  }

  for (size_t i = 0; i < kQueueSize; ++i) {
    if (!g_queue[i].used) {
      g_queue[i].used = true;
      payload.substring(0, kPayloadSize - 1).toCharArray(g_queue[i].payload, kPayloadSize);
      logLine("[QUEUE] enqueue");
      logLine(payload);
      if (!isEvent) {
        g_lastStatusPayload = payload;
      }
      return true;
    }
  }

  if (isEvent && replaceQueuedStatus(payload)) {
    return true;
  }

  logLine(isEvent ? "[QUEUE] full, drop event" : "[QUEUE] full, drop payload");
  return false;
}

void popQueueFront() {
  for (size_t i = 0; i + 1 < kQueueSize; ++i) {
    g_queue[i] = g_queue[i + 1];
  }
  g_queue[kQueueSize - 1] = PendingRequest{};
}

bool queueEmpty() {
  return !g_queue[0].used;
}

String buildStatusPayload() {
  // 按云端要求的字段格式构造“停车场状态”JSON。
  String payload = "{";
  payload += "\"device_id\":\"" + escapeJson(String(CLOUD_DEVICE_ID)) + "\",";
  payload += "\"device_key\":\"" + escapeJson(String(CLOUD_DEVICE_KEY)) + "\",";
  payload += "\"type\":\"status\",";
  payload += "\"payload\":{";
  payload += "\"total_slots\":" + String(g_frame.total) + ",";
  payload += "\"active_count\":" + String(g_frame.active) + ",";
  payload += "\"free_slots\":" + String(g_frame.freeSlots);
  payload += "}}";
  return payload;
}

String buildEventPayload() {
  // 按云端要求的字段格式构造“车辆进出事件”JSON。
  String payload = "{";
  payload += "\"device_id\":\"" + escapeJson(String(CLOUD_DEVICE_ID)) + "\",";
  payload += "\"device_key\":\"" + escapeJson(String(CLOUD_DEVICE_KEY)) + "\",";
  payload += "\"type\":\"event\",";
  payload += "\"payload\":{";
  payload += "\"ev\":\"" + escapeJson(g_frame.eventName) + "\",";
  payload += "\"plate_no\":\"" + escapeJson(g_frame.plate) + "\",";
  payload += "\"in_time\":" + String(g_frame.inTs) + ",";
  payload += "\"out_time\":" + String(g_frame.outTs) + ",";
  payload += "\"fee_cents\":" + String(g_frame.fee);
  payload += "}}";
  return payload;
}

void finalizeFrame() {
  // STM32 用 END 表示一帧业务数据已经发送完成。
  String payload;
  bool isEvent = false;
  if (g_frame.type == "STATUS") {
    logLine("[FRAME] STATUS parsed");
    payload = buildStatusPayload();
  } else if (g_frame.type == "EVENT") {
    logLine("[FRAME] EVENT parsed");
    payload = buildEventPayload();
    isEvent = true;
  }

  if (payload.length() > 0) {
    enqueuePayload(payload, isEvent);
  }
  resetFrame();
}

void parseLine(const char *line) {
  if (strcmp(line, "END") == 0) {
    finalizeFrame();
    return;
  }

  const char *sep = strchr(line, '=');
  if (!sep) {
    return;
  }

  String key = String(line).substring(0, sep - line);
  String value = String(sep + 1);

  // 解析 STM32 发来的简易 key=value 文本协议。
  if (key == "TYPE") {
    g_frame.type = value;
  } else if (key == "TOTAL") {
    g_frame.total = static_cast<uint32_t>(value.toInt());
  } else if (key == "ACTIVE") {
    g_frame.active = static_cast<uint32_t>(value.toInt());
  } else if (key == "FREE") {
    g_frame.freeSlots = static_cast<uint32_t>(value.toInt());
  } else if (key == "EV") {
    g_frame.eventName = value;
  } else if (key == "PLATE") {
    g_frame.plate = value;
  } else if (key == "IN_TS") {
    g_frame.inTs = static_cast<uint32_t>(value.toInt());
  } else if (key == "OUT_TS") {
    g_frame.outTs = static_cast<uint32_t>(value.toInt());
  } else if (key == "FEE") {
    g_frame.fee = static_cast<uint32_t>(value.toInt());
  }
}

void handleStm32Input() {
  // 持续读取，直到 SoftwareSerial 接收缓冲区被清空。
  while (g_stm32Serial.available() > 0) {
    const char ch = static_cast<char>(g_stm32Serial.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      g_lineBuffer[g_lineLen] = '\0';
      if (g_lineLen > 0) {
        parseLine(g_lineBuffer);
      }
      g_lineLen = 0;
      continue;
    }

    if (g_lineLen + 1 < kLineBufferSize) {
      g_lineBuffer[g_lineLen++] = ch;
    } else {
      // 单行超长时直接丢弃，避免解析状态被破坏，等待下一行重新同步。
      g_lineLen = 0;
    }
  }
}

void ensureWifiConnected() {
  // WiFi 连接维护采用非阻塞方式，避免影响串口收数。
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_wifiWasConnected) {
      g_wifiWasConnected = true;
      g_wifiConnectAttemptActive = false;
      setWifiLed(true);
      logLine("[WIFI] connected");
      logLine("[WIFI] ip=" + WiFi.localIP().toString());
    }
    return;
  }

  if (g_wifiWasConnected) {
    g_wifiWasConnected = false;
    setWifiLed(false);
    logLine("[WIFI] disconnected");
  }

  // 控制重试频率，避免上传失败后过于频繁地打接口。
  const unsigned long now = millis();
  if (g_lastWifiAttemptMs != 0 && now - g_lastWifiAttemptMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  g_lastWifiAttemptMs = now;
  WiFi.mode(WIFI_STA);
  if (!g_wifiConnectAttemptActive) {
    // 避免在每次 loop 中重复打印相同的连接提示。
    g_wifiConnectAttemptActive = true;
    logLine("[WIFI] connecting...");
    logLine("[WIFI] ssid=" + String(WIFI_SSID));
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool uploadPayload(const char *payload) {
  // 这里虽然走的是 HTTPS，但为了调试方便关闭了证书校验。
  // 如果后续要正式部署，建议改成证书校验或证书指纹校验。
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient http;
  if (!http.begin(*client, CLOUD_UPLOAD_URL)) {
    logLine("[HTTP] begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const int statusCode = http.POST(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  logLine("[HTTP] POST code=" + String(statusCode));
  if (statusCode > 0) {
    logLine("[HTTP] response=" + http.getString());
  } else {
    logLine("[HTTP] error=" + http.errorToString(statusCode));
  }
  http.end();
  return statusCode >= 200 && statusCode < 300;
}

void flushQueue() {
  if (queueEmpty() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  // 控制重试频率，避免上传失败后过于频繁地打接口。
  const unsigned long now = millis();
  if (now - g_lastUploadAttemptMs < UPLOAD_RETRY_INTERVAL_MS) {
    return;
  }

  g_lastUploadAttemptMs = now;
  logLine("[HTTP] uploading queue head");
  if (uploadPayload(g_queue[0].payload)) {
    logLine("[HTTP] upload success");
    if (!isEventPayloadText(g_queue[0].payload)) {
      g_lastStatusPayload = String(g_queue[0].payload);
    }
    popQueueFront();
  } else {
    logLine("[HTTP] upload failed, will retry");
  }
}

}  // namespace

void setup() {
  Serial.begin(kDebugBaudRate);
  delay(200);
  pinMode(kWifiLedPin, OUTPUT);
  setWifiLed(false);
  g_stm32Serial.begin(kStm32BaudRate);
  logLine("[BOOT] parking cloud client start");
  logLine("[UART] debug baud=" + String(kDebugBaudRate));
  logLine("[UART] stm32 rx pin=D5(GPIO14), baud=" + String(kStm32BaudRate));
  resetFrame();
  ensureWifiConnected();
}

void loop() {
  // 主循环刻意保持精简：收串口、保活 WiFi、尝试上传一条队头消息。
  handleStm32Input();
  ensureWifiConnected();
  flushQueue();
}