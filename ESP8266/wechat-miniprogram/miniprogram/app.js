const config = require('./utils/config')

App({
  onLaunch() {
    // 小程序启动时初始化云开发环境，后续所有云函数都会走这里配置的环境。
    if (!wx.cloud) {
      throw new Error('当前基础库不支持云开发，请升级微信后重试')
    }

    wx.cloud.init({
      env: config.cloudEnv,
      traceUser: true
    })
  }
})
