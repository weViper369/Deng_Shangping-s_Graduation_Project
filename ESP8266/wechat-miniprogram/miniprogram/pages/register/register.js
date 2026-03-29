const auth = require('../../utils/auth')

Page({
  data: {
    username: '',
    password: '',
    plateNo: '',
    loading: false
  },

  onInput(e) {
    const { field } = e.currentTarget.dataset
    this.setData({ [field]: e.detail.value })
  },

  submit() {
    const { username, password, plateNo, loading } = this.data
    if (loading) return
    if (!username || !password || !plateNo) {
      wx.showToast({ title: '请完整填写注册信息', icon: 'none' })
      return
    }

    this.setData({ loading: true })

    // 注册时把车牌号一并绑定，后续停车记录靠它和设备事件关联。
    auth.callFunction('registerUser', { username, password, plate_no: plateNo }, false)
      .then((result) => {
        if (!result.ok) {
          throw new Error(result.message || '注册失败')
        }
        wx.showToast({ title: '注册成功', icon: 'success' })
        setTimeout(() => {
          wx.navigateBack()
        }, 500)
      })
      .catch((error) => {
        wx.showToast({ title: error.message || '注册失败', icon: 'none' })
      })
      .finally(() => {
        this.setData({ loading: false })
      })
  }
})
