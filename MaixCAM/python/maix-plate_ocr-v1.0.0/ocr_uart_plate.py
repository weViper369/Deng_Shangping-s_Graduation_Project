#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MaixCAM (SG2002) - 车牌识别端

【目标】
- 平时不做 OCR(省算力、少误识别)
- STM32 检测到红外遮挡后，发送 REQ_OCR 触发帧给 Cam
- Cam 在 2 秒识别窗口内运行 OCR, 识别成功立即回发车牌帧给 STM32

【通信协议】
帧格式：
AA 55 | VER | TYPE | SEQ | LEN_L LEN_H | PAYLOAD | CRC8

CRC:
CRC-8/MAXIM, 计算范围: VER..PAYLOAD(不含 AA55, 不含 CRC)

1) Cam -> STM32
TYPE=0x01: 识别结果
PAYLOAD = [CONF][PLATE_LEN][PLATE_UTF8_BYTES...]
- CONF: 0~100
- PLATE_LEN: UTF-8 字节长度
- PLATE_UTF8_BYTES: UTF-8 编码车牌（可含中文省份）

2) STM32 -> Cam
TYPE=0x10: REQ_OCR(请求识别一次)
PAYLOAD = [LANE][RESERVED],LEN 固定为 2
- LANE: 0x00=IN, 0x01=OUT
- RESERVED: 0x00(预留扩展)
"""

import re
import time
from maix import app, camera, display, nn, image, uart, pinmap, touchscreen


# ============================================================
# 1. UART 配置
# ============================================================
# UART1: A18=RX, A19=TX
pinmap.set_pin_function("A18", "UART1_RX")
pinmap.set_pin_function("A19", "UART1_TX")
ser = uart.UART("/dev/ttyS1", 115200)


# ============================================================
# 2. CRC8/MAXIM（Dallas）实现
#    - poly: 0x31 (反射实现使用 0x8C)
#    - init: 0x00
#    - refin/refout: True
#    - xorout: 0x00
# ============================================================
def crc8_maxim(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x01:
                crc = (crc >> 1) ^ 0x8C
            else:
                crc >>= 1
    return crc & 0xFF


# ============================================================
# 3. Cam -> STM32：发送车牌结果帧（TYPE=0x01）
# ============================================================
_tx_seq = 0  # Cam 发出的序号

def send_plate_frame(plate: str, conf: int = 90, lane: int = 0) -> None:
    """
    发送车牌识别结果给 STM32
    Frame:
      AA55 | VER(01) | TYPE(01) | SEQ | LEN | PAYLOAD | CRC8
    PAYLOAD:
      [LANE][CONF][PLATE_LEN][PLATE_UTF8_BYTES...]

    注意: CRC8 计算范围不含 AA55, 不含 CRC
    """
    global _tx_seq

    head = bytes([0xAA, 0x55])
    ver  = 0x01
    typ  = 0x01
    seq  = _tx_seq & 0xFF

    # 车牌用 UTF-8 编码（支持中文省份）
    plate_b = plate.encode("utf-8")
    if len(plate_b) > 200:
        # 防止异常超长（一般不可能）
        plate_b = plate_b[:200]

    lane_b = lane & 0xFF
    payload = bytes([lane_b, conf & 0xFF, len(plate_b) & 0xFF]) + plate_b
    ln = len(payload)

    print("SENDPAY:", lane, conf, len(plate_b))

    # body = VER..PAYLOAD
    body = bytes([ver, typ, seq, ln & 0xFF, (ln >> 8) & 0xFF]) + payload
    crc = crc8_maxim(body)

    frame = head + body + bytes([crc])
    ser.write(frame)

    _tx_seq = (_tx_seq + 1) & 0xFF


# ============================================================
# 4. STM32 -> Cam：接收 REQ_OCR 触发帧（TYPE=0x10）
# ============================================================
_rx_buf = bytearray()

def _read_uart_nonblock():
    """
    尝试非阻塞读取串口数据。
    不同固件 read() 参数略有不同，做兼容处理。
    """
    try:
        data = ser.read(timeout=0)  # 没数据一般返回 b'' 或 None
    except TypeError:
        # 有些版本没有 timeout 参数
        data = ser.read()
    if not data:
        return b""
    if isinstance(data, str):
        return data.encode()
    return bytes(data)


def poll_req_ocr_lane():
    """
    轮询串口，解析 AA55 帧，提取 REQ_OCR 的 LANE。
    返回：
      - None:没收到有效 REQ_OCR
      - 0: IN(入口)
      - 1: OUT(出口)

    REQ_OCR 帧定义：
      TYPE=0x10
      LEN=2
      PAYLOAD=[LANE][RESERVED]
      LANE: 0x00=IN, 0x01=OUT
      RESERVED: 0x00
    """
    data = _read_uart_nonblock()
    if data:
        _rx_buf.extend(data)

    while True:
        if len(_rx_buf) < 2:
            return None

        # 找帧头 AA55
        idx = _rx_buf.find(b"\xAA\x55")
        if idx < 0:
            _rx_buf.clear()
            return None
        if idx > 0:
            del _rx_buf[:idx]

        # 至少要有：AA55 + VER TYPE SEQ LEN_L LEN_H + CRC(最短)
        if len(_rx_buf) < 2 + 5 + 1:
            return None

        ver = _rx_buf[2]
        typ = _rx_buf[3]
        seq = _rx_buf[4]  # 目前 Cam 侧不用，但保留
        ln  = _rx_buf[5] | (_rx_buf[6] << 8)

        frame_len = 2 + 5 + ln + 1  # AA55 + header5 + payload + crc
        if len(_rx_buf) < frame_len:
            return None

        frame = bytes(_rx_buf[:frame_len])
        del _rx_buf[:frame_len]

        # CRC 校验：对 body(VER..PAYLOAD)
        body = frame[2:-1]
        crc  = frame[-1]
        if crc8_maxim(body) != crc:
            # CRC 错：丢掉该帧，继续解析后续数据
            continue

        # 只处理 REQ_OCR
        if ver == 0x01 and typ == 0x10 and ln == 2:
            lane = frame[2 + 5 + 0]      # payload[0]
            reserved = frame[2 + 5 + 1]  # payload[1]

            if reserved == 0x00 and lane in (0x00, 0x01):
                return lane

        # 其他帧忽略
        continue


# ============================================================
# 5. OCR 模型与车牌过滤
# ============================================================
# 使用系统自带 PP_OCR（检测 + 识别）
ocr = nn.PP_OCR("/root/models/pp_ocr.mud")

# 车牌过滤：
# - 第 1 位：中文省份
# - 后面 6~7 位：字母/数字（放宽以兼容新能源/少量误识别）
PLATE_RE = re.compile(r"^[\u4e00-\u9fff][A-Z0-9]{6,7}$")

def normalize_plate(s: str) -> str:
    """
    清理 OCR 输出：
    - 去空格
    - 大写
    - 常见纠错: O->0, I->1
    """
    s = s.strip().replace(" ", "").upper()
    s = s.replace("O", "0").replace("I", "1")
    return s


# ============================================================
# 6. 相机/显示初始化
# ============================================================
cam  = camera.Camera()
disp = display.Display()
ts = touchscreen.TouchScreen()

# ============================================================
# 7. 事件触发识别逻辑参数（4秒窗口）
# ============================================================
ARM_WINDOW_MS = 4000    # 触发后识别窗口：4秒
FRAME_SKIP    = 3       # 窗口内：每 3 帧跑一次 OCR（提速）
DUP_MS        = 3000    # 同一车牌 3 秒内不重复上报（去抖）
RES_COOLDOWN  = 800     # 识别成功后短暂冷却（避免刚成功就又触发一次）

armed = False
armed_until = 0
lane_cur = 0            # 0=IN, 1=OUT（用于显示/日志）

cooldown_until = 0

last_plate = None
last_time  = 0
frame_i = 0


# ============================================================
# 8. 主循环
# ============================================================
while not app.need_exit():
    now = int(time.time() * 1000)

    # 8.1 轮询是否收到 STM32 的 REQ_OCR 触发帧
    lane = poll_req_ocr_lane()
    if lane is not None:
        armed = True
        lane_cur = lane
        armed_until = now + ARM_WINDOW_MS
        cooldown_until = 0  # 触发后立即允许识别
        print("TRIG:", "IN" if lane == 0 else "OUT")

    # 8.2 采集画面并缩放到 OCR 模型输入尺寸（320x224）
    img = cam.read()
    img = img.resize(320, 224)

    # 8.3 转 BGR（PP_OCR 要求 BGR888）
    # 固件支持 to_format 就直接用
    if hasattr(img, "to_format"):
        try:
            img = img.to_format(image.Format.FMT_BGR888)
        except:
            pass

    # 8.4 显示当前状态（IDLE / ARMED IN/OUT）
    if armed and now < armed_until:
        img.draw_string(5, 5, f"ARMED {'IN' if lane_cur==0 else 'OUT'}",
                        color=image.COLOR_RED, scale=1)
    else:
        img.draw_string(5, 5, "IDLE", color=image.COLOR_RED, scale=1)

    # ===== > 退出按钮 =====
    BTN_SIZE = 32
    BTN_X = 320 - BTN_SIZE - 8 # 按钮区域在右上角
    BTN_Y = 8

    # 只画符号（居中到“隐形区域”里）
    img.draw_string(
        BTN_X + BTN_SIZE // 2 - 6,
        BTN_Y + BTN_SIZE // 2 - 12,
        ">",
        color=image.COLOR_WHITE,
        scale=2
    )

    # ===== 读取触摸 =====
    tp = None
    try:
        tp = ts.read()
    except:
        tp = None

    if tp and isinstance(tp, (list, tuple)) and len(tp) >= 3:
        tx, ty, st = int(tp[0]), int(tp[1]), int(tp[2])

    if st == 1:
        # 映射到 320x224 坐标
        try:
            sw, sh = disp.width(), disp.height()
        except:
            sw, sh = 520, 320  # 兜底

        x = int(tx * 320 / sw)
        y = int(ty * 224 / sh)

        # 命中“隐形区域”就退出
        if BTN_X <= x <= BTN_X + BTN_SIZE and BTN_Y <= y <= BTN_Y + BTN_SIZE:
            print("Touch > -> quit app")
            break
    # 8.5 不在识别窗口：不跑 OCR
    if (not armed) or (now >= armed_until):
        armed = False
        disp.show(img)
        continue

    # 8.6 成功识别后短暂冷却（减少重复计算）
    if now < cooldown_until:
        disp.show(img)
        continue

    # 8.7 跳帧：降低 OCR 运行频率
    frame_i += 1
    if frame_i % FRAME_SKIP != 0:
        disp.show(img)
        continue

    # 8.8 执行 OCR（窗口内）
    objs = ocr.detect(img)

    found = None
    for obj in objs:
        text = normalize_plate(obj.char_str())
        if PLATE_RE.match(text):
            found = text
            break

    # 8.9 若识别到车牌：立刻回传并退出窗口
    if found:
        img.draw_string(5, 25, found, color=image.COLOR_RED, scale=2)

        # 去抖：同一车牌 3 秒内不重复上报
        if not (found == last_plate and (now - last_time) < DUP_MS):
            print("SEND:", found)
            send_plate_frame(found, conf=90, lane=lane_cur)
            last_plate = found
            last_time = now

        # 识别成功：退出 armed（事件完成）
        armed = False
        cooldown_until = now + RES_COOLDOWN

    disp.show(img)