const auth = require('../../utils/auth')
const { formatDateTime, formatFee } = require('../../utils/format')

Page({
  data: {
    records: [],
    loading: false
  },

  onShow() {
    if (!auth.ensureLogin()) return
    this.loadRecords()
  },

  loadRecords() {
    if (this.data.loading) return
    this.setData({ loading: true })

    auth.callFunction('getMyRecords')
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '加载失败')
        }

        const records = (result.data || []).map((item) => ({
          ...item,
          in_text: formatDateTime(item.in_wall_time || item.in_time),
          out_text: formatDateTime(item.out_wall_time || item.out_time),
          fee_text: formatFee(item.fee_cents)
        }))
        this.setData({ records })
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '加载失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ loading: false })
      })
  }
})
