#include "config.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <WiFiClientSecureBearSSL.h>

namespace {

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

constexpr size_t kLineBufferSize = 96;
constexpr size_t kQueueSize = 8;
constexpr size_t kPayloadSize = 320;
constexpr uint8_t kWifiLedPin = LED_BUILTIN;
constexpr uint8_t kWifiLedOn = LOW;
constexpr uint8_t kWifiLedOff = HIGH;
constexpr uint8_t kStm32RxPin = 14;  // D5 / GPIO14
constexpr uint8_t kStm32TxPin = 12;  // D6 / GPIO12, reserved
constexpr uint32_t kDebugBaudRate = 115200;
constexpr uint32_t kStm32BaudRate = 9600;

SoftwareSerial g_stm32Serial(kStm32RxPin, kStm32TxPin);

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

struct PendingRequest {
  bool used = false;
  char payload[kPayloadSize];
};

char g_lineBuffer[kLineBufferSize];
size_t g_lineLen = 0;
FrameState g_frame;
PendingRequest g_queue[kQueueSize];
unsigned long g_lastWifiAttemptMs = 0;
unsigned long g_lastUploadAttemptMs = 0;
bool g_wifiWasConnected = false;
bool g_wifiConnectAttemptActive = false;
String g_lastStatusPayload;

void logLine(const String &message) {
  Serial.println(message);
}

void setWifiLed(bool connected) {
  digitalWrite(kWifiLedPin, connected ? kWifiLedOn : kWifiLedOff);
}

void resetFrame() {
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
      g_lineLen = 0;
    }
  }
}

void ensureWifiConnected() {
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

  const unsigned long now = millis();
  if (g_lastWifiAttemptMs != 0 && now - g_lastWifiAttemptMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  g_lastWifiAttemptMs = now;
  WiFi.mode(WIFI_STA);
  if (!g_wifiConnectAttemptActive) {
    g_wifiConnectAttemptActive = true;
    logLine("[WIFI] connecting...");
    logLine("[WIFI] ssid=" + String(WIFI_SSID));
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool uploadPayload(const char *payload) {
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
  handleStm32Input();
  ensureWifiConnected();
  flushQueue();
}