const auth = require('../../utils/auth')
const { formatDateTime } = require('../../utils/format')

Page({
  data: {
    summary: {},
    user: null,
    loading: false,
    reserving: false,
    cancelling: false
  },

  onShow() {
    if (!auth.ensureLogin()) return
    this.setData({ user: auth.getUser() })
    this.loadSummary()
  },

  loadSummary() {
    if (this.data.loading) return
    this.setData({ loading: true })

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

  cancelReservation() {
    if (this.data.cancelling) return
    this.setData({ cancelling: true })

    auth.callFunction('cancelReservation')
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '取消失败')
        }
        wx.showToast({ title: '已取消', icon: 'success' })
        this.loadSummary()
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '取消失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ cancelling: false })
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
