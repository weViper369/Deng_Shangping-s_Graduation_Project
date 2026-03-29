const config = require('./utils/config')

App({
  onLaunch() {
    if (!wx.cloud) {
      throw new Error('当前基础库不支持云开发')
    }

    wx.cloud.init({
      env: config.cloudEnv,
      traceUser: true
    })
  }
})
