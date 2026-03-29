function formatDateTime(timestamp) {
  if (!timestamp) return '暂无'
  const date = new Date(Number(timestamp))
  const pad = (value) => String(value).padStart(2, '0')
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
}

function formatFee(feeCents) {
  return `¥${(Number(feeCents || 0) / 100).toFixed(2)}`
}

module.exports = {
  formatDateTime,
  formatFee
}
