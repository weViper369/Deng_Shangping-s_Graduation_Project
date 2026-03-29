# 停车场管理系统毕业设计项目

这是一个面向毕业设计演示的智能停车场系统，整体方案由 `STM32 + MaixCAM + ESP8266 + 微信小程序 + 微信云开发` 组成。

系统已经实现以下完整链路：

- `MaixCAM` 识别车牌并通过串口发送给 `STM32`
- `STM32` 完成本地停车状态机、进出记录、车位统计和道闸控制
- `ESP8266` 通过 WiFi 将车位状态和进出事件上传到微信云开发
- `微信小程序` 提供注册、登录、查看车位、预约车位、查看我的进出记录

## 项目结构

- [STM32F103C8T6/]：STM32 工程，负责现场控制逻辑
- [MaixCAM/]：MaixCAM 车牌识别脚本与模型相关代码
- [ESP8266/parking_cloud_client/]：ESP8266 联网上传程序
- [ESP8266/wechat-miniprogram/]：微信小程序与云函数工程
- [工程接线图.md]：当前硬件接线说明

## 核心功能

- 车牌识别触发入场、出场流程
- 首页显示总车位、现场剩余车位、有效预约数、可预约车位
- 用户账号注册与登录
- 一个账号绑定一个车牌
- 小程序预约车位
- 查询当前账号绑定车牌的进出记录、时间和费用
- ESP8266 上传 `STATUS` 和 `EVENT` 数据到微信云开发

## 当前系统架构

```text
MaixCAM --> STM32 --> ESP8266 --> 微信云开发 --> 微信小程序
```

其中：

- `MaixCAM -> STM32`：车牌识别串口通信
- `STM32 -> ESP8266`：停车场状态帧与事件帧上传前的串口通信
- `ESP8266 -> 云开发`：HTTPS 上传设备数据
- `小程序 -> 云函数`：登录、预约、记录查询

## 串口协议

STM32 会通过 `USART3` 周期发送状态帧：

```text
TYPE=STATUS
TOTAL=20
ACTIVE=6
FREE=14
END
```

入场和出场事件帧格式如下：

```text
TYPE=EVENT
EV=IN
PLATE=京N8P8F8
IN_TS=123456
END
```

```text
TYPE=EVENT
EV=OUT
PLATE=京N8P8F8
IN_TS=120000
OUT_TS=123456
FEE=100
END
```

## 小程序功能页面

- `pages/login/login`：登录页
- `pages/register/register`：注册页
- `pages/home/home`：首页，显示车位和预约入口
- `pages/records/records`：我的进出记录页

## 演示流程建议

1. 注册账号并绑定演示车牌
2. 登录小程序查看首页车位数据
3. 触发车辆入场，观察首页状态变化
4. 查看 `parking_records` 和小程序记录页
5. 触发车辆出场并完成支付，确认记录变为 `finished`
6. 演示预约车位功能

## GitHub 上传说明

以下配置文件中的值为模板占位值，拉取后需要自行替换：

- `ESP8266/wechat-miniprogram/project.config.json` 中的 `appid`
- `ESP8266/wechat-miniprogram/miniprogram/utils/config.js` 中的 `cloudEnv`
- `ESP8266/wechat-miniprogram/cloudfunctions/deviceUpload/index.js` 中的 `DEVICE_KEY`
- 鉴权云函数中的 `TOKEN_SECRET`

## 说明

本项目优先面向毕业设计演示，强调“可跑通、可展示、成本低”。
如果后续需要扩展管理员后台、取消预约、多车牌绑定、支付联动等功能，可以在当前结构上继续迭代。