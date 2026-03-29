# 微信小程序 + 云开发演示工程

这个目录包含两部分：

- `miniprogram/`：微信小程序前端
- `cloudfunctions/`：云函数

## 已实现功能

- 用户注册
- 用户登录
- 首页查看车位信息
- 创建预约
- 查询我的进出记录
- 设备上传状态和进出事件

## 目录说明

- [project.config.json]：本地开发时实际使用的微信开发者工具项目入口
- [project.config.example.json]：上传 GitHub 用的模板文件
- [miniprogram/]：小程序页面、样式、工具函数
- [cloudfunctions/]：云函数源码

## 配置文件模式

本项目采用和 `config.h / config.example.h` 类似的方式：

- 本地真实配置文件保留原名，供开发时直接运行
- 额外提供 `example` 模板文件用于上传 GitHub
- 真实配置文件已加入 `.gitignore`

例如：

- `project.config.json` + `project.config.example.json`
- `miniprogram/utils/config.js` + `config.example.js`
- 各云函数目录下的 `config.js` + `config.example.js`

## 使用步骤

1. 用微信开发者工具打开 [project.config.json]
2. 检查 [config.js] 和各云函数目录中的 `config.js` 是否为你的真实配置
3. 在云开发控制台创建下面 4 个集合：
   - `users`
   - `device_status`
   - `reservations`
   - `parking_records`
4. 上传并部署全部云函数
5. 如果要让 ESP8266 通过 HTTPS 上传数据，就给 `deviceUpload` 配置云开发 HTTP 访问入口，并把地址填到 ESP8266 的 `CLOUD_UPLOAD_URL`

## GitHub 上传说明

上传 GitHub 时，保留这些模板文件：

- `project.config.example.json`
- `miniprogram/utils/config.example.js`
- 各云函数目录下的 `config.example.js`

不要上传这些真实配置文件：

- `project.config.json`
- `miniprogram/utils/config.js`
- 各云函数目录下的 `config.js`
- `project.private.config.json`

## 演示链路

- `STM32` 周期发送 `STATUS` 帧到 `ESP8266`
- `STM32` 在成功入场、成功出场后发送 `EVENT` 帧到 `ESP8266`
- `ESP8266` 把串口帧转换成 JSON，上传给 `deviceUpload`
- 微信小程序通过云函数查询首页、创建预约、查看记录