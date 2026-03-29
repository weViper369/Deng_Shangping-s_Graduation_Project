const cloud = require('wx-server-sdk')
const crypto = require('crypto')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
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

exports.main = async (event) => {
  const user = await getUserFromToken(event.token)
  if (!user) {
    return { ok: false, message: '未登录或登录已失效' }
  }

  const records = await db.collection('parking_records')
    .where({ plate_no: user.plate_no })
    .orderBy('in_time', 'desc')
    .get()

  return {
    ok: true,
    data: records.data
  }
}
