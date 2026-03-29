const cloud = require('wx-server-sdk')
const crypto = require('crypto')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
const _ = db.command
const { TOKEN_SECRET } = require('./config')

function decodeToken(token) {
  const parts = String(token || '').split('.')
  if (parts.length !== 2) return null
  const [body, sign] = parts
  const expected = crypto.createHmac('sha256', TOKEN_SECRET).update(body).digest('hex')
  if (expected !== sign) return null
  const payload = JSON.parse(Buffer.from(body.replace(/-/g, '+').replace(/_/g, '/'), 'base64').toString())
  if (!payload.exp || payload.exp < Date.now()) return null
  return payload
}

async function getUserFromToken(token) {
  const payload = decodeToken(token)
  if (!payload) return null
  const result = await db.collection('users').doc(payload.userId).get()
  return result.data || null
}

async function expireReservations(now) {
  const expired = await db.collection('reservations').where({
    status: 'active',
    expire_at: _.lte(now)
  }).get()

  await Promise.all(expired.data.map((item) => (
    db.collection('reservations').doc(item._id).update({
      data: {
        status: 'expired',
        released_at: now
      }
    })
  )))
}

exports.main = async (event) => {
  const user = await getUserFromToken(event.token)
  if (!user) {
    return { ok: false, message: '未登录或登录已失效' }
  }

  const now = Date.now()
  await expireReservations(now)

  const statusResult = await db.collection('device_status').orderBy('updated_at', 'desc').limit(1).get()
  const latestStatus = statusResult.data[0] || {}

  const activeReservations = await db.collection('reservations').where({
    status: 'active',
    expire_at: _.gt(now)
  }).get()

  const myReservationResult = await db.collection('reservations').where({
    user_id: user._id,
    status: 'active',
    expire_at: _.gt(now)
  }).limit(1).get()

  const totalSlots = Number(latestStatus.total_slots || 0)
  const deviceFree = Number(latestStatus.free_slots || 0)
  const activeReservationCount = activeReservations.data.length
  const reservableFree = Math.max(0, deviceFree - activeReservationCount)

  return {
    ok: true,
    data: {
      total_slots: totalSlots,
      device_free: deviceFree,
      active_reservation_count: activeReservationCount,
      reservable_free: reservableFree,
      updated_at: latestStatus.updated_at || 0,
      updated_at_text: latestStatus.updated_at ? new Date(latestStatus.updated_at).toLocaleString('zh-CN', { hour12: false }) : '暂无',
      my_active_reservation: myReservationResult.data[0] || null
    }
  }
}
