# ESP8266 联网上传程序

这个目录提供 ESP8266 程序，用于完成下面这条链路：

- `STM32 USART3 <-> ESP8266 SoftwareSerial`
- `ESP8266 -> 微信云开发 HTTP 服务`

## 目录说明

- `parking_cloud_client.ino`：主程序
- `config.h`：本地真实配置
- `config.example.h`：配置模板

## 配置项

在 `config.h` 中至少配置下面几项：

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `CLOUD_UPLOAD_URL`
- `CLOUD_RESERVATION_SYNC_URL`
- `CLOUD_DEVICE_ID`
- `CLOUD_DEVICE_KEY`

其中：

- `CLOUD_UPLOAD_URL` 用于设备状态和进出场事件上报
- `CLOUD_RESERVATION_SYNC_URL` 用于拉取当前有效预约名单并同步给 STM32

## 使用方式

1. 将 `config.example.h` 复制为 `config.h`
2. 填写 WiFi、HTTP 地址、`device_id`、`device_key`
3. 用 Arduino IDE 打开 `parking_cloud_client.ino`
4. 烧录到 ESP8266
5. 重新编译并烧录 STM32，确保 `USART3` 为 `9600`

## 接线方式

- `STM32 PB10 (USART3_TX)` -> `ESP8266 D5 / GPIO14`
- `STM32 PB11 (USART3_RX)` -> `ESP8266 D6 / GPIO12`
- `STM32 GND` -> `ESP8266 GND`

说明：

- `D5(GPIO14)` 负责接收 STM32 上报的状态帧和事件帧
- `D6(GPIO12)` 负责把预约名单同步回 STM32
- 板载蓝灯在 WiFi 连上后点亮

## 串口协议

STM32 -> ESP8266：

```text
TYPE=STATUS
TOTAL=25
ACTIVE=6
FREE=19
NORMAL_TOTAL=20
NORMAL_ACTIVE=4
NORMAL_FREE=16
RESERVED_TOTAL=5
RESERVED_ACTIVE=2
RESERVED_FREE=3
END
```

```text
TYPE=EVENT
EV=IN
PLATE=粤A12345
MODE=RESERVED
IN_TS=123456
END
```

```text
TYPE=EVENT
EV=OUT
PLATE=粤A12345
MODE=RESERVED
IN_TS=120000
OUT_TS=123456
FEE=100
END
```

ESP8266 -> STM32：

```text
TYPE=RESERVATION_SYNC
PLATES=粤A12345,粤B88888
END
```

## 程序行为

- 自动连接 WiFi
- 自动上传停车场双池状态和进出场事件
- 周期拉取云端有效预约名单
- 成功拉取后通过 `USART3` 回传给 STM32
- 上传失败时保留少量待重试消息

## 日志查看

Arduino 串口监视器设置为 `115200`，可看到类似日志：

- `[WIFI] connected`
- `[FRAME] STATUS parsed`
- `[SYNC] reservation list changed`
- `[HTTP] upload success`
