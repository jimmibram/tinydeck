#!/usr/bin/env python3
"""Grab the device framebuffer over the USB serial port and save it as a PNG.

Usage: screenshot.py [port] [out.png]   (default /dev/ttyACM0, shot.png)
Sends Ctrl-\\ (0x1C); the firmware answers with SHOT_BEGIN/hex rows/SHOT_END.
"""
import struct, sys, time, zlib
import serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
out = sys.argv[2] if len(sys.argv) > 2 else 'shot.png'
W, H = 400, 300

ser = serial.Serial(port, 115200, timeout=0.5)
ser.reset_input_buffer()
ser.write(b'\x1c')
data = b''
deadline = time.time() + 8
while time.time() < deadline:
    data += ser.read(4096)
    if b'SHOT_END' in data:
        break
ser.close()
start = data.find(b'SHOT_BEGIN')
end = data.find(b'SHOT_END')
if start < 0 or end < 0:
    sys.exit('no screenshot received')
lines = data[start:end].split(b'\n')[1:]
fb = bytes.fromhex(b''.join(l.strip() for l in lines if l.strip()).decode())
assert len(fb) == W * H // 8, len(fb)

def pixel(x, y):
    iy = H - 1 - y
    idx = (x >> 1) * (H // 4) + (iy >> 2)
    mask = 0x80 >> (((iy & 3) << 1) | (x & 1))
    return 0 if (fb[idx] & mask) else 1   # 1 = ink (dark)

scale = 2
rows = []
for y in range(H):
    row = bytearray()
    for x in range(W):
        v = 0x20 if pixel(x, y) else 0xE8
        row += bytes([v, v, v]) * scale
    for _ in range(scale):
        rows.append(b'\x00' + bytes(row))

def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * scale, H * scale, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print('saved', out)
