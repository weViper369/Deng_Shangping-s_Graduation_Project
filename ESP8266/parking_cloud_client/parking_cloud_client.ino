#include "config.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <WiFiClientSecureBearSSL.h>

/**
 * @file parking_cloud_client.ino
 * @brief ESP8266 停车系统云端桥接程序。
 *
 * 通过 UART 接收 STM32 发送的键值帧数据，转换为 JSON 后上报云端；
 * 同时周期性从云端拉取预约车牌列表，并回传给 STM32。
 */
namespace {
// 本程序用于把 STM32 发来的停车场数据转发到云端 HTTP 接口。
// STM32 通过串口发送按行分隔的 key=value 文本帧，并用 END 表示一帧结束。
// ESP8266 负责解析串口帧、组装 JSON，并在 WiFi 或 HTTP 暂时异常时
// 先把数据缓存起来，等网络恢复后再重试上传。
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#ifndef CLOUD_RESERVATION_SYNC_URL
#define CLOUD_RESERVATION_SYNC_URL ""
#endif

#ifndef RESERVATION_SYNC_INTERVAL_MS
#define RESERVATION_SYNC_INTERVAL_MS 5000UL
#endif

// 考虑到 ESP8266 RAM 有限，这里的缓冲区和队列尺寸保持得比较小。
constexpr size_t kLineBufferSize = 96;
constexpr size_t kQueueSize = 8;
constexpr size_t kPayloadSize = 384;
constexpr size_t kReservationCsvSize = 96;

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

/**
 * @brief 单帧 STM32 数据的解析状态。
 */
// 保存一帧 STM32 串口数据中解析出来的字段。
struct FrameState {
  String type;
  String eventName;
  String plate;
  String entryMode;
  uint32_t total = 0;
  uint32_t active = 0;
  uint32_t freeSlots = 0;
  uint32_t normalTotal = 0;
  uint32_t normalActive = 0;
  uint32_t normalFree = 0;
  uint32_t reservedTotal = 0;
  uint32_t reservedActive = 0;
  uint32_t reservedFree = 0;
  uint32_t inTs = 0;
  uint32_t outTs = 0;
  uint32_t fee = 0;
};

/**
 * @brief 上传队列中的一个 JSON 负载条目。
 */
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
unsigned long g_lastReservationSyncMs = 0;
bool g_wifiWasConnected = false;
bool g_wifiConnectAttemptActive = false;
// 记录上一份状态快照，避免重复状态反复入队，占用带宽和队列空间。
String g_lastStatusPayload;
String g_lastReservationCsv;

/**
 * @brief 输出一条调试日志到串口控制台。
 * @param[in] message 日志文本。
 */
void logLine(const String &message) {
  Serial.println(message);
}

/**
 * @brief 设置 Wi-Fi 状态指示灯。
 * @param[in] connected 为 true 表示 Wi-Fi 已连接。
 */
void setWifiLed(bool connected) {
  digitalWrite(kWifiLedPin, connected ? kWifiLedOn : kWifiLedOff);
}

/**
 * @brief 重置当前正在组帧的缓存状态。
 */
void resetFrame() {
  // 开始解析下一帧前，先把上一帧留下的字段清空。
  g_frame = FrameState{};
}

/**
 * @brief 转义 JSON 敏感字符，并去掉回车/换行。
 * @param[in] input 原始输入字符串。
 * @return 可安全写入 JSON 的字符串。
 */
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

/**
 * @brief 判断负载是否为事件类型 JSON。
 * @param[in] payload 以 `\\0` 结尾的 JSON 字符串。
 * @return 包含 `"type":"event"` 时返回 true。
 */
bool isEventPayloadText(const char *payload) {
  return payload && strstr(payload, "\"type\":\"event\"") != nullptr;
}

/**
 * @brief 检查队列中是否已存在相同负载。
 * @param[in] payload 待检查的负载内容。
 * @return 队列中存在重复项时返回 true。
 */
bool queueContainsPayload(const String &payload) {
  for (size_t i = 0; i < kQueueSize; ++i) {
    if (g_queue[i].used && payload.equals(g_queue[i].payload)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 替换队列中第一条非事件状态负载。
 * @param[in] payload 新的状态负载。
 * @return 成功替换旧状态项时返回 true。
 */
bool replaceQueuedStatus(const String &payload) {
  // 队列满时优先保留事件消息，用最新状态覆盖较旧的状态快照。
  for (size_t i = 0; i < kQueueSize; ++i) {
    if (!g_queue[i].used || isEventPayloadText(g_queue[i].payload)) {
      continue;
    }
    payload.substring(0, kPayloadSize - 1).toCharArray(g_queue[i].payload, kPayloadSize);
    logLine("[QUEUE] replace stale status");
    logLine(payload);
    return true;
  }
  return false;
}

/**
 * @brief 将负载写入发送队列，并对状态数据做去重处理。
 * @param[in] payload JSON 负载字符串。
 * @param[in] isEvent 为 true 表示事件负载，false 表示状态负载。
 * @return 成功入队（或替换）返回 true，丢弃返回 false。
 */
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

/**
 * @brief 弹出队首元素，并将后续元素前移。
 */
void popQueueFront() {
  for (size_t i = 0; i + 1 < kQueueSize; ++i) {
    g_queue[i] = g_queue[i + 1];
  }
  g_queue[kQueueSize - 1] = PendingRequest{};
}

/**
 * @brief 判断上传队列是否为空。
 * @return 队首未使用时返回 true。
 */
bool queueEmpty() {
  return !g_queue[0].used;
}

/**
 * @brief 根据当前帧数据构造状态上报 JSON。
 * @return 序列化后的状态 JSON 字符串。
 */
String buildStatusPayload() {
  // 按云端要求的字段格式构造“停车场状态”JSON。
  const uint32_t totalSlots = g_frame.total != 0 ? g_frame.total : (g_frame.normalTotal + g_frame.reservedTotal);
  const uint32_t activeCount = g_frame.active != 0 ? g_frame.active : (g_frame.normalActive + g_frame.reservedActive);
  const uint32_t freeSlots = g_frame.freeSlots != 0 ? g_frame.freeSlots : (g_frame.normalFree + g_frame.reservedFree);

  String payload = "{";
  payload += "\"device_id\":\"" + escapeJson(String(CLOUD_DEVICE_ID)) + "\",";
  payload += "\"device_key\":\"" + escapeJson(String(CLOUD_DEVICE_KEY)) + "\",";
  payload += "\"type\":\"status\",";
  payload += "\"payload\":{";
  payload += "\"total_slots\":" + String(totalSlots) + ",";
  payload += "\"active_count\":" + String(activeCount) + ",";
  payload += "\"free_slots\":" + String(freeSlots) + ",";
  payload += "\"normal_total_slots\":" + String(g_frame.normalTotal) + ",";
  payload += "\"normal_active_count\":" + String(g_frame.normalActive) + ",";
  payload += "\"normal_free_slots\":" + String(g_frame.normalFree) + ",";
  payload += "\"reserved_total_slots\":" + String(g_frame.reservedTotal) + ",";
  payload += "\"reserved_active_count\":" + String(g_frame.reservedActive) + ",";
  payload += "\"reserved_free_slots\":" + String(g_frame.reservedFree);
  payload += "}}";
  return payload;
}

/**
 * @brief 根据当前帧数据构造事件上报 JSON。
 * @return 序列化后的事件 JSON 字符串。
 */
String buildEventPayload() {
  // 按云端要求的字段格式构造“车辆进出事件”JSON。
  String payload = "{";
  payload += "\"device_id\":\"" + escapeJson(String(CLOUD_DEVICE_ID)) + "\",";
  payload += "\"device_key\":\"" + escapeJson(String(CLOUD_DEVICE_KEY)) + "\",";
  payload += "\"type\":\"event\",";
  payload += "\"payload\":{";
  payload += "\"ev\":\"" + escapeJson(g_frame.eventName) + "\",";
  payload += "\"plate_no\":\"" + escapeJson(g_frame.plate) + "\",";
  payload += "\"entry_mode\":\"" + escapeJson(g_frame.entryMode) + "\",";
  payload += "\"in_time\":" + String(g_frame.inTs) + ",";
  payload += "\"out_time\":" + String(g_frame.outTs) + ",";
  payload += "\"fee_cents\":" + String(g_frame.fee);
  payload += "}}";
  return payload;
}

/**
 * @brief 完成当前帧解析，并在有效时写入上传队列。
 */
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

/**
 * @brief 解析一行 `KEY=VALUE` 格式的串口文本。
 * @param[in] line 不包含换行符的一行文本。
 */
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
  } else if (key == "NORMAL_TOTAL") {
    g_frame.normalTotal = static_cast<uint32_t>(value.toInt());
  } else if (key == "NORMAL_ACTIVE") {
    g_frame.normalActive = static_cast<uint32_t>(value.toInt());
  } else if (key == "NORMAL_FREE") {
    g_frame.normalFree = static_cast<uint32_t>(value.toInt());
  } else if (key == "RESERVED_TOTAL") {
    g_frame.reservedTotal = static_cast<uint32_t>(value.toInt());
  } else if (key == "RESERVED_ACTIVE") {
    g_frame.reservedActive = static_cast<uint32_t>(value.toInt());
  } else if (key == "RESERVED_FREE") {
    g_frame.reservedFree = static_cast<uint32_t>(value.toInt());
  } else if (key == "EV") {
    g_frame.eventName = value;
  } else if (key == "PLATE") {
    g_frame.plate = value;
  } else if (key == "MODE") {
    g_frame.entryMode = value;
    g_frame.entryMode.toLowerCase();
  } else if (key == "IN_TS") {
    g_frame.inTs = static_cast<uint32_t>(value.toInt());
  } else if (key == "OUT_TS") {
    g_frame.outTs = static_cast<uint32_t>(value.toInt());
  } else if (key == "FEE") {
    g_frame.fee = static_cast<uint32_t>(value.toInt());
  }
}

/**
 * @brief 读取 STM32 串口流，并按行完成帧解析。
 */
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

/**
 * @brief 维护 Wi-Fi 连接状态，并按间隔重连。
 */
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
    g_lastReservationSyncMs = 0;
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

/**
 * @brief 向云端接口上传一条 JSON 负载。
 * @param[in] payload 以 `\\0` 结尾的 JSON 字符串。
 * @return HTTP 返回 2xx 时返回 true。
 */
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

/**
 * @brief 按重试间隔尝试上传队首负载。
 */
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

/**
 * @brief 判断服务端响应是否表示成功。
 * @param[in] response HTTP 响应体。
 * @return 响应包含 `"ok":true` 时返回 true。
 */
bool responseLooksOk(const String &response) {
  return response.indexOf("\"ok\":true") >= 0 || response.indexOf("\"ok\": true") >= 0;
}

/**
 * @brief 从简化 JSON 文本中提取指定键的字符串值。
 * @param[in] json 原始 JSON 文本。
 * @param[in] key 需要提取的字段名。
 * @return 解码后的字符串；字段不存在时返回空串。
 */
String extractJsonStringValue(const String &json, const String &key) {
  const String token = "\"" + key + "\":\"";
  const int start = json.indexOf(token);
  if (start < 0) {
    return String();
  }

  String output;
  for (int i = start + token.length(); i < json.length(); ++i) {
    const char ch = json[i];
    if (ch == '\\') {
      if (i + 1 < json.length()) {
        output += json[i + 1];
        ++i;
      }
      continue;
    }
    if (ch == '"') {
      break;
    }
    output += ch;
  }
  return output;
}

/**
 * @brief 从云端同步接口拉取预约车牌 CSV。
 * @param[out] csvOut 成功时返回解析得到的 `plates_csv`。
 * @return 请求成功且响应校验通过时返回 true。
 */
bool fetchReservationCsv(String &csvOut) {
  if (String(CLOUD_RESERVATION_SYNC_URL).length() == 0) {
    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient http;
  if (!http.begin(*client, CLOUD_RESERVATION_SYNC_URL)) {
    logLine("[SYNC] begin failed");
    return false;
  }

  String requestBody = "{";
  requestBody += "\"device_id\":\"" + escapeJson(String(CLOUD_DEVICE_ID)) + "\",";
  requestBody += "\"device_key\":\"" + escapeJson(String(CLOUD_DEVICE_KEY)) + "\"}";

  http.addHeader("Content-Type", "application/json");
  const int statusCode = http.POST(reinterpret_cast<const uint8_t *>(requestBody.c_str()), requestBody.length());
  logLine("[SYNC] POST code=" + String(statusCode));
  if (statusCode <= 0) {
    logLine("[SYNC] error=" + http.errorToString(statusCode));
    http.end();
    return false;
  }

  const String response = http.getString();
  http.end();
  logLine("[SYNC] response=" + response);

  if (statusCode < 200 || statusCode >= 300 || !responseLooksOk(response)) {
    return false;
  }

  csvOut = extractJsonStringValue(response, "plates_csv");
  return true;
}

/**
 * @brief 按约定帧格式将预约 CSV 下发给 STM32。
 * @param[in] csv 逗号分隔的车牌列表。
 */
void sendReservationSyncFrame(const String &csv) {
  char line[kReservationCsvSize];
  csv.substring(0, kReservationCsvSize - 1).toCharArray(line, kReservationCsvSize);

  g_stm32Serial.print("TYPE=RESERVATION_SYNC\n");
  g_stm32Serial.print("PLATES=");
  g_stm32Serial.print(line);
  g_stm32Serial.print("\nEND\n");
}

/**
 * @brief 周期性拉取云端预约数据并转发给 STM32。
 */
void syncReservations() {
  if (WiFi.status() != WL_CONNECTED || String(CLOUD_RESERVATION_SYNC_URL).length() == 0) {
    return;
  }

  const unsigned long now = millis();
  if (g_lastReservationSyncMs != 0 && now - g_lastReservationSyncMs < RESERVATION_SYNC_INTERVAL_MS) {
    return;
  }

  g_lastReservationSyncMs = now;
  String reservationCsv;
  if (!fetchReservationCsv(reservationCsv)) {
    logLine("[SYNC] fetch failed");
    return;
  }

  if (reservationCsv != g_lastReservationCsv) {
    logLine("[SYNC] reservation list changed");
    logLine(reservationCsv);
    g_lastReservationCsv = reservationCsv;
  }

  sendReservationSyncFrame(reservationCsv);
}

}  // namespace

/**
 * @brief Arduino 初始化入口。
 */
void setup() {
  Serial.begin(kDebugBaudRate);
  delay(200);
  pinMode(kWifiLedPin, OUTPUT);
  setWifiLed(false);
  g_stm32Serial.begin(kStm32BaudRate);
  logLine("[BOOT] parking cloud client start");
  logLine("[UART] debug baud=" + String(kDebugBaudRate));
  logLine("[UART] stm32 rx pin=D5(GPIO14), tx pin=D6(GPIO12), baud=" + String(kStm32BaudRate));
  resetFrame();
  ensureWifiConnected();
}

/**
 * @brief Arduino 主循环入口。
 */
void loop() {
  // 主循环刻意保持精简：收串口、保活 WiFi、尝试上传一条队头消息。
  handleStm32Input();
  ensureWifiConnected();
  syncReservations();
  flushQueue();
}
