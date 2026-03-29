const cloud = require('wx-server-sdk')
const crypto = require('crypto')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
const { TOKEN_SECRET } = require('./config')

function hashPassword(password) {
  // 注册和登录都使用同一套 SHA-256 规则，避免明文存储密码。
  return crypto.createHash('sha256').update(String(password)).digest('hex')
}

function base64UrlEncode(value) {
  return Buffer.from(value).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '')
}

function signToken(payload) {
  // 这里实现的是一个轻量自定义 token：正文 base64url + HMAC 签名。
  const body = base64UrlEncode(JSON.stringify(payload))
  const sign = crypto.createHmac('sha256', TOKEN_SECRET).update(body).digest('hex')
  return `${body}.${sign}`
}

exports.main = async (event) => {
  const username = String(event.username || '').trim()
  const password = String(event.password || '')
  if (!username || !password) {
    return { ok: false, message: '请输入账号和密码' }
  }

  const result = await db.collection('users').where({ username }).get()
  const user = result.data[0]
  if (!user || user.password_hash !== hashPassword(password)) {
    return { ok: false, message: '用户名或密码错误' }
  }

  const token = signToken({
    userId: user._id,
    exp: Date.now() + 7 * 24 * 60 * 60 * 1000
  })

  return {
    ok: true,
    token,
    user: {
      _id: user._id,
      username: user.username,
      plate_no: user.plate_no
    }
  }
}
