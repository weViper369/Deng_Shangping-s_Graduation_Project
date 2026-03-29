# ESP8266 联网上传程序

这个目录提供一个演示版 `ESP8266` 程序，用来完成下面这条链路：

- `STM32 USART3 -> ESP8266 SoftwareSerial`
- `ESP8266 -> 微信云开发 HTTP 服务`

## 目录说明

- [parking_cloud_client.ino] 主程序
- [config.h] 实际配置
- [config.example.h] 配置模板

## 使用方式

1. 将 `config.example.h` 复制为 `config.h`
2. 填写 WiFi、HTTP 服务地址、`device_id`、`device_key`
3. 用 Arduino IDE 打开 [parking_cloud_client.ino]
4. 烧录到 ESP8266
5. 重新编译并烧录 STM32 工程，确保 `USART3` 波特率已变为 `9600`

## 接线方式

- `STM32 PB10 (USART3_TX)` -> `ESP8266 D5 / GPIO14`
- `STM32 GND` -> `ESP8266 GND`
- `STM32 PB11 (USART3_RX)` 这一版可以先不接

说明：

- `ESP8266` 的 USB 硬件串口保留给串口监视器输出日志
- `STM32` 数据通过 `SoftwareSerial` 从 `D5(GPIO14)` 读入
- 板载蓝灯在 WiFi 连上后亮起

## 串口协议

STM32 会发送两类文本帧。

状态帧：

```text
TYPE=STATUS
TOTAL=20
ACTIVE=6
FREE=14
END
```

事件帧：

```text
TYPE=EVENT
EV=IN
PLATE=粤B12345
IN_TS=123456
END
```

或：

```text
TYPE=EVENT
EV=OUT
PLATE=粤B12345
IN_TS=120000
OUT_TS=123456
FEE=100
END
```

## 程序行为

- 自动连接 WiFi
- 连接后板子蓝色灯亮起
- 通过 `D5(GPIO14)` 逐行解析 STM32 串口文本帧
- 将数据组装成 JSON 后上传
- 上传失败时保留少量待重试消息

## 日志查看

Arduino 串口监视器设置为 `115200`，可看到：

- `[WIFI] connected`
- `[FRAME] STATUS parsed`
- `[HTTP] POST code=200`
- `[HTTP] upload success`

## 说明

- HTTPS 使用 `setInsecure()`，不做严格证书校验
- 为了开发快速所以使用了arduino开发，有能力的可以尝试改为ESP-IDF或者PIatformIO IDE

