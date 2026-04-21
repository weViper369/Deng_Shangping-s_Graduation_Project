const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
const _ = db.command
const { DEVICE_KEY } = require('./config')

function normalizePlate(plateNo) {
  return String(plateNo || '').trim().toUpperCase()
}

function normalizeEvent(event) {
  if (event.body) {
    if (typeof event.body === 'string') {
      return JSON.parse(event.body)
    }
    return event.body
  }
  return event
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
  const data = normalizeEvent(event)
  if (String(data.device_key || '') !== DEVICE_KEY) {
    return { ok: false, message: 'device_key 错误' }
  }

  const now = Date.now()
  await expireReservations(now)

  const reservationResult = await db.collection('reservations').where({
    status: 'active',
    expire_at: _.gt(now)
  }).orderBy('reserved_at', 'asc').get()

  const plates = Array.from(new Set(
    reservationResult.data
      .map((item) => normalizePlate(item.plate_no))
      .filter(Boolean)
  ))

  return {
    ok: true,
    updated_at: now,
    reservation_count: plates.length,
    plates_csv: plates.join(','),
    data: {
      updated_at: now,
      reservation_count: plates.length,
      plates_csv: plates.join(','),
      plates,
      reservations: reservationResult.data.map((item) => ({
        plate_no: normalizePlate(item.plate_no),
        expire_at: Number(item.expire_at || 0)
      }))
    }
  }
}
