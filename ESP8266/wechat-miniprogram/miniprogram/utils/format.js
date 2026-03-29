function formatDateTime(timestamp) {
  // 没有时间戳时统一显示“暂无”，避免页面出现空白。
  if (!timestamp) return '暂无'
  const date = new Date(Number(timestamp))
  const pad = (value) => String(value).padStart(2, '0')
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
}

function formatFee(feeCents) {
  // 后端和设备统一使用“分”为单位，前端展示时再换算成元。
  return `￥${(Number(feeCents || 0) / 100).toFixed(2)}`
}

module.exports = {
  formatDateTime,
  formatFee
}
