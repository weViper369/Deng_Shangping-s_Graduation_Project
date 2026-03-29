const auth = require('../../utils/auth')
const { formatDateTime } = require('../../utils/format')

Page({
  data: {
    summary: {},
    user: null,
    loading: false,
    reserving: false
  },

  onShow() {
    if (!auth.ensureLogin()) return
    this.setData({ user: auth.getUser() })
    this.loadSummary()
  },

  loadSummary() {
    if (this.data.loading) return
    this.setData({ loading: true })

    // 首页加载停车场总览、当前用户预约信息等汇总数据。
    auth.callFunction('getParkingSummary')
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '加载失败')
        }
        const summary = result.data || {}
        if (summary.my_active_reservation && summary.my_active_reservation.expire_at) {
          summary.my_active_reservation.expire_text = formatDateTime(summary.my_active_reservation.expire_at)
        }
        this.setData({ summary })
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '加载失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ loading: false })
      })
  },

  reserve() {
    if (this.data.reserving) return
    this.setData({ reserving: true })

    // 发起预约，请求成功后刷新首页摘要。
    auth.callFunction('createReservation')
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '预约失败')
        }
        wx.showToast({ title: '预约成功', icon: 'success' })
        this.loadSummary()
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '预约失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ reserving: false })
      })
  },

  goRecords() {
    wx.navigateTo({ url: '/pages/records/records' })
  },

  logout() {
    auth.clearSession()
    wx.redirectTo({ url: '/pages/login/login' })
  }
})
