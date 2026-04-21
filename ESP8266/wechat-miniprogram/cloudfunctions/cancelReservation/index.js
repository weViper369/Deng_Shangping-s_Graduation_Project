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

  const reservationResult = await db.collection('reservations').where({
    user_id: user._id,
    status: 'active',
    expire_at: _.gt(now)
  }).orderBy('reserved_at', 'asc').limit(1).get()

  if (reservationResult.data.length === 0) {
    return { ok: false, message: '当前没有可取消的有效预约' }
  }

  const reservation = reservationResult.data[0]
  await db.collection('reservations').doc(reservation._id).update({
    data: {
      status: 'cancelled',
      cancelled_at: now,
      released_at: now
    }
  })

  return { ok: true, message: '已取消预约' }
}

