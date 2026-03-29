const auth = require('../../utils/auth')

Page({
  data: {
    username: '',
    password: '',
    loading: false
  },

  onShow() {
    if (auth.getToken()) {
      wx.redirectTo({ url: '/pages/home/home' })
    }
  },

  onInput(e) {
    const { field } = e.currentTarget.dataset
    this.setData({ [field]: e.detail.value })
  },

  submit() {
    const { username, password, loading } = this.data
    if (loading) return
    if (!username || !password) {
      wx.showToast({ title: '请填写账号和密码', icon: 'none' })
      return
    }

    this.setData({ loading: true })
    auth.callFunction('loginUser', { username, password }, false)
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '登录失败')
        }
        auth.setSession(result.token, result.user)
        wx.redirectTo({ url: '/pages/home/home' })
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '登录失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ loading: false })
      })
  },

  toRegister() {
    wx.navigateTo({ url: '/pages/register/register' })
  }
})