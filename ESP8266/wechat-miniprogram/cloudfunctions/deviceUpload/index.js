const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
const _ = db.command
const { DEVICE_KEY } = require('./config')

const DEFAULT_NORMAL_TOTAL_SLOTS = 20
const DEFAULT_RESERVED_TOTAL_SLOTS = 5

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

async function upsertDeviceStatus(data) {
  const existing = await db.collection('device_status').where({ device_id: data.device_id }).limit(1).get()
  const payload = {
    device_id: data.device_id,
    total_slots: Number(data.payload.total_slots || 0),
    active_count: Number(data.payload.active_count || 0),
    free_slots: Number(data.payload.free_slots || 0),
    normal_total_slots: Number(data.payload.normal_total_slots || data.payload.total_slots || DEFAULT_NORMAL_TOTAL_SLOTS),
    normal_active_count: Number(data.payload.normal_active_count || data.payload.active_count || 0),
    normal_free_slots: Number(data.payload.normal_free_slots || data.payload.free_slots || 0),
    reserved_total_slots: Number(data.payload.reserved_total_slots || DEFAULT_RESERVED_TOTAL_SLOTS),
    reserved_active_count: Number(data.payload.reserved_active_count || 0),
    reserved_free_slots: Number(data.payload.reserved_free_slots || 0),
    updated_at: Date.now()
  }

  if (existing.data.length > 0) {
    await db.collection('device_status').doc(existing.data[0]._id).update({ data: payload })
  } else {
    await db.collection('device_status').add({ data: payload })
  }
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

async function useReservationIfMatched(plateNo, now) {
  const reservationResult = await db.collection('reservations').where({
    plate_no: plateNo,
    status: 'active',
    expire_at: _.gt(now)
  }).orderBy('reserved_at', 'asc').limit(1).get()

  if (reservationResult.data.length === 0) {
    return false
  }

  const reservation = reservationResult.data[0]
  await db.collection('reservations').doc(reservation._id).update({
    data: {
      status: 'used',
      used_at: now,
      used_in_plate: plateNo,
      released_at: now
    }
  })
  return true
}

async function handleInEvent(data, user) {
  const plateNo = normalizePlate(data.payload.plate_no)
  const deviceInTime = Number(data.payload.in_time || 0)
  const serverNow = Date.now()
  const entryMode = String(data.payload.entry_mode || '').toLowerCase() === 'reserved'
    ? 'reserved'
    : 'normal'

  await expireReservations(serverNow)
  if (entryMode === 'reserved') {
    await useReservationIfMatched(plateNo, serverNow)
  }

  await db.collection('parking_records').add({
    data: {
      plate_no: plateNo,
      user_id: user ? user._id : null,
      in_time: deviceInTime,
      in_wall_time: serverNow,
      out_time: 0,
      out_wall_time: 0,
      duration_s: 0,
      fee_cents: 0,
      status: 'in_progress',
      entry_mode: entryMode
    }
  })
}

async function handleOutEvent(data, user) {
  const plateNo = normalizePlate(data.payload.plate_no)
  const deviceOutTime = Number(data.payload.out_time || 0)
  const serverNow = Date.now()
  const feeCents = Number(data.payload.fee_cents || 0)
  const fallbackEntryMode = String(data.payload.entry_mode || '').toLowerCase() === 'reserved'
    ? 'reserved'
    : 'normal'
  const activeRecord = await db.collection('parking_records')
    .where({ plate_no: plateNo, status: 'in_progress' })
    .orderBy('in_wall_time', 'desc')
    .limit(1)
    .get()

  if (activeRecord.data.length > 0) {
    const record = activeRecord.data[0]
    const durationS = record.in_time && deviceOutTime >= record.in_time
      ? Math.floor((deviceOutTime - record.in_time) / 1000)
      : 0

    await db.collection('parking_records').doc(record._id).update({
      data: {
        user_id: user ? user._id : record.user_id,
        out_time: deviceOutTime,
        out_wall_time: serverNow,
        duration_s: durationS,
        fee_cents: feeCents,
        status: 'finished',
        entry_mode: record.entry_mode || fallbackEntryMode
      }
    })
    return
  }

  await db.collection('parking_records').add({
    data: {
      plate_no: plateNo,
      user_id: user ? user._id : null,
      in_time: Number(data.payload.in_time || 0),
      in_wall_time: 0,
      out_time: deviceOutTime,
      out_wall_time: serverNow,
      duration_s: 0,
      fee_cents: feeCents,
      status: 'finished',
      entry_mode: fallbackEntryMode
    }
  })
}

exports.main = async (event) => {
  const data = normalizeEvent(event)
  if (String(data.device_key || '') !== DEVICE_KEY) {
    return { ok: false, message: 'device_key 错误' }
  }

  if (data.type === 'status') {
    await upsertDeviceStatus(data)
    return { ok: true }
  }

  if (data.type === 'event') {
    const plateNo = normalizePlate(data.payload.plate_no)
    const userResult = await db.collection('users').where({ plate_no: plateNo }).limit(1).get()
    const user = userResult.data[0] || null
    const ev = String(data.payload.ev || '').toUpperCase()

    if (ev === 'IN') {
      await handleInEvent(data, user)
    } else if (ev === 'OUT') {
      await handleOutEvent(data, user)
    } else {
      return { ok: false, message: '未知事件类型' }
    }

    return { ok: true }
  }

  return { ok: false, message: '未知上传类型' }
}
