const TOKEN_KEY = 'parking_demo_token'
const USER_KEY = 'parking_demo_user'

function getToken() {
  return wx.getStorageSync(TOKEN_KEY) || ''
}

function getUser() {
  return wx.getStorageSync(USER_KEY) || null
}

function setSession(token, user) {
  wx.setStorageSync(TOKEN_KEY, token)
  wx.setStorageSync(USER_KEY, user)
}

function clearSession() {
  wx.removeStorageSync(TOKEN_KEY)
  wx.removeStorageSync(USER_KEY)
}

function ensureLogin() {
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
