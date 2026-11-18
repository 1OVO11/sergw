#!/usr/bin/env python3
"""生成 M1 测试用的字节流 test.bin"""

def crc8(data, poly=0x07, crc=0x00):
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc

def make_frame(seq, ftype, data):
    body = bytes([len(data), seq, ftype]) + bytes(data)
    return b'\xAA' + body + bytes([crc8(body)])

out = bytearray()

# 1) 三个噪声字节（不含 0xAA）
out += b'\x00\x11\x22'

# 2) 正常帧 seq=0：温湿度 26.3C / 48.9%
out += make_frame(0, 0x01, [0x01, 0x07, 0x01, 0xE9])

# 3) 正常帧 seq=1：心跳
out += make_frame(1, 0x02, [])

# 4) 非法帧：LEN = 0x21 = 33 > 32
out += b'\xAA\x21\x00\x01'

# 5) 坏帧 seq=2：CRC 故意改错
bad = bytearray(make_frame(2, 0x01, [0x01, 0x08, 0x01, 0xEB]))
bad[-1] ^= 0xFF
out += bad

# 6) 正常帧 seq=3：温湿度 26.4C / 49.1%
out += make_frame(3, 0x01, [0x01, 0x08, 0x01, 0xEB])

# 7) 残缺帧：EOF 时只有半个帧
out += b'\xAA\x04\x04\x01\x01\x07'

with open('test.bin', 'wb') as f:
    f.write(out)

print(f"生成 test.bin，{len(out)} 字节")
print(f"CRC8('123456789') = 0x{crc8(b'123456789'):02X}  (标准值应为 0xF4)")