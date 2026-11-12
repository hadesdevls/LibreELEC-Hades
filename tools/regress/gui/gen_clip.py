# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Writes a short MJPEG AVI (video only) with PIL and the standard library: a moving test pattern, 1280x720, 24 fps.
import io, struct, sys, math
from PIL import Image, ImageDraw
out, secs = sys.argv[1], int(sys.argv[2])
W, H, FPS = 1280, 720, 24
N = secs * FPS
frames = []
BASE = Image.new('RGB', (W, H))
_d = ImageDraw.Draw(BASE)
for x in range(0, W, 8):
    v = int(255 * x / W)
    _d.rectangle([x, 0, x + 8, H], fill=(v, 120, 255 - v))
for i in range(N):
    img = BASE.copy()
    d = ImageDraw.Draw(img)
    cx = int((W - 200) * (0.5 + 0.5 * math.sin(i / 20.0)))
    d.rectangle([cx, 260, cx + 200, 460], fill=(235, 235, 235))
    d.text((40, 40), 'Hades player information test clip, frame %d' % i, fill=(255, 255, 255))
    b = io.BytesIO(); img.save(b, 'JPEG', quality=70); frames.append(b.getvalue())
def chunk(t, data):
    pad = b'\0' if len(data) % 2 else b''
    return t + struct.pack('<I', len(data)) + data + pad
def lst(t, data): return b'LIST' + struct.pack('<I', len(data) + 4) + t + data
maxb = max(len(f) for f in frames)
avih = struct.pack('<IIIIIIIIIIIIII', 1000000 // FPS, maxb * FPS, 0, 0x10, N, 0, 1, maxb, W, H, 0, 0, 0, 0)
strh = b'vidsMJPG' + struct.pack('<IHHIIIIIIIIhhhh', 0, 0, 0, 0, 1, FPS, 0, N, maxb, 0xFFFFFFFF, 0, 0, 0, W, H)
strf = struct.pack('<IiiHH4sIiiII', 40, W, H, 1, 24, b'MJPG', W * H * 3, 0, 0, 0, 0)
hdrl = lst(b'hdrl', chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', strh) + chunk(b'strf', strf)))
movi = b''; idx = b''; off = 4
for f in frames:
    c = chunk(b'00dc', f)
    idx += b'00dc' + struct.pack('<III', 0x10, off, len(f))
    movi += c; off += len(c)
data = b'AVI ' + hdrl + lst(b'movi', movi) + chunk(b'idx1', idx)
open(out, 'wb').write(b'RIFF' + struct.pack('<I', len(data)) + data)
print(out, len(data), 'bytes', N, 'frames')
