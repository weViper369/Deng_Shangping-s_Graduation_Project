#ifndef PARKING_CLOUD_CONFIG_H
#define PARKING_CLOUD_CONFIG_H

// 烧录前请替换成你自己的 WiFi 名称和密码。
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// 烧录前请替换成后端分配的上传地址、设备 ID 和设备密钥。
#define CLOUD_UPLOAD_URL "CLOUD_UPLOAD_URL"
#define CLOUD_DEVICE_ID "CLOUD_DEVICE_ID"
#define CLOUD_DEVICE_KEY "CLOUD_DEVICE_KEY"

// 重连和重试间隔，单位都是毫秒。
#define WIFI_RECONNECT_INTERVAL_MS 10000UL
#define UPLOAD_RETRY_INTERVAL_MS 5000UL

#endif
