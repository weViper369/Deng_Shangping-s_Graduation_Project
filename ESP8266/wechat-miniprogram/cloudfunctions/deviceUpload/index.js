const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

const db = cloud.database()
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

async function upsertDeviceStatus(data) {
  const existing = await db.collection('device_status').where({ device_id: data.device_id }).limit(1).get()
  const payload = {
    device_id: data.device_id,
    total_slots: Number(data.payload.total_slots || 0),
    active_count: Number(data.payload.active_count || 0),
    free_slots: Number(data.payload.free_slots || 0),
    updated_at: Date.now()
  }

  if (existing.data.length > 0) {
    await db.collection('device_status').doc(existing.data[0]._id).update({ data: payload })
  } else {
    await db.collection('device_status').add({ data: payload })
  }
}

async function handleInEvent(data, user) {
  const now = Number(data.payload.in_time || Date.now())
  await db.collection('parking_records').add({
    data: {
      plate_no: normalizePlate(data.payload.plate_no),
      user_id: user ? user._id : null,
      in_time: now,
      out_time: 0,
      duration_s: 0,
      fee_cents: 0,
      status: 'in_progress'
    }
  })
}

async function handleOutEvent(data, user) {
  const plateNo = normalizePlate(data.payload.plate_no)
  const outTime = Number(data.payload.out_time || Date.now())
  const feeCents = Number(data.payload.fee_cents || 0)
  const activeRecord = await db.collection('parking_records')
    .where({ plate_no: plateNo, status: 'in_progress' })
    .orderBy('in_time', 'desc')
    .limit(1)
    .get()

  if (activeRecord.data.length > 0) {
    const record = activeRecord.data[0]
    const durationS = record.in_time && outTime >= record.in_time
      ? Math.floor((outTime - record.in_time) / 1000)
      : 0

    await db.collection('parking_records').doc(record._id).update({
      data: {
        user_id: user ? user._id : record.user_id,
        out_time: outTime,
        duration_s: durationS,
        fee_cents: feeCents,
        status: 'finished'
      }
    })
    return
  }

  await db.collection('parking_records').add({
    data: {
      plate_no: plateNo,
      user_id: user ? user._id : null,
      in_time: Number(data.payload.in_time || 0),
      out_time: outTime,
      duration_s: 0,
      fee_cents: feeCents,
      status: 'finished'
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
