const TOKEN_KEY = 'parking_demo_token'
const USER_KEY = 'parking_demo_user'

function getToken() {
  return wx.getStorageSync(TOKEN_KEY) || ''
}

function getUser() {
  return wx.getStorageSync(USER_KEY) || null
}

function setSession(token, user) {
  // 登录成功后把 token 和用户信息一起缓存到本地。
  wx.setStorageSync(TOKEN_KEY, token)
  wx.setStorageSync(USER_KEY, user)
}

function clearSession() {
  // 退出登录时清理本地会话。
  wx.removeStorageSync(TOKEN_KEY)
  wx.removeStorageSync(USER_KEY)
}

function ensureLogin() {
  // 页面进入前先检查本地是否还有有效登录态。
  if (getToken()) {
    return true
  }
  wx.redirectTo({ url: '/pages/login/login' })
  return false
}

function callFunction(name, data = {}, requireAuth = true) {
  const token = getToken()
  if (requireAuth && !token) {
    return Promise.reject(new Error('未登录'))
  }

  // 统一封装云函数调用。需要鉴权的接口会自动把 token 带上。
  return wx.cloud.callFunction({
    name,
    data: Object.assign({}, data, requireAuth ? { token } : {})
  }).then((res) => res.result)
}

module.exports = {
  getToken,
  getUser,
  setSession,
  clearSession,
  ensureLogin,
  callFunction
}
