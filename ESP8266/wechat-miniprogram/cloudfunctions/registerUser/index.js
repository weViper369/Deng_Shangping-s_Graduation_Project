const cloud = require('wx-server-sdk')
const crypto = require('crypto')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()

function normalizePlate(plateNo) {
  // 车牌统一转成大写并去掉首尾空格，方便后续和设备事件匹配。
  return String(plateNo || '').trim().toUpperCase()
}

function hashPassword(password) {
  return crypto.createHash('sha256').update(String(password)).digest('hex')
}

exports.main = async (event) => {
  const username = String(event.username || '').trim()
  const password = String(event.password || '')
  const plateNo = normalizePlate(event.plate_no)

  if (!username || !password || !plateNo) {
    return { ok: false, message: '注册信息不完整' }
  }

  const usernameExists = await db.collection('users').where({ username }).get()
  if (usernameExists.data.length > 0) {
    return { ok: false, message: '用户名已存在' }
  }

  const plateExists = await db.collection('users').where({ plate_no: plateNo }).get()
  if (plateExists.data.length > 0) {
    return { ok: false, message: '该车牌已被绑定' }
  }

  await db.collection('users').add({
    data: {
      username,
      password_hash: hashPassword(password),
      plate_no: plateNo,
      created_at: Date.now()
    }
  })

  return { ok: true }
}
