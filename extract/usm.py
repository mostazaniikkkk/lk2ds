"""CRI USM demuxer + decryptor (standard CRI 64-bit key scheme).
LK2 key: 59406882471034052 (BtoCLK2.GameMain.SlotContentsManager, 'CPK_Crypt_Key').
Writes <name>.m1v (Sofdec.Prime / MPEG-1 video) and <name>_alpha.m1v (alpha channel) per USM."""
import struct, sys, os
import numpy as np

LK2_KEY = 59406882471034052

def make_masks(key):
    k1, k2 = key & 0xFFFFFFFF, (key >> 32) & 0xFFFFFFFF
    t = [0] * 0x20
    t[0x00] = k1 & 0xFF; t[0x01] = (k1 >> 8) & 0xFF; t[0x02] = (k1 >> 16) & 0xFF; t[0x03] = ((k1 >> 24) - 0x34) & 0xFF
    t[0x04] = ((k2 & 0xFF) + 0xF9) & 0xFF; t[0x05] = ((k2 >> 8) & 0xFF) ^ 0x13; t[0x06] = (((k2 >> 16) & 0xFF) + 0x61) & 0xFF
    t[0x07] = t[0x00] ^ 0xFF; t[0x08] = (t[0x02] + t[0x01]) & 0xFF; t[0x09] = (t[0x01] - t[0x07]) & 0xFF
    t[0x0A] = t[0x02] ^ 0xFF; t[0x0B] = t[0x01] ^ 0xFF; t[0x0C] = (t[0x0B] + t[0x09]) & 0xFF
    t[0x0D] = (t[0x08] - t[0x03]) & 0xFF; t[0x0E] = t[0x0D] ^ 0xFF; t[0x0F] = (t[0x0A] - t[0x0B]) & 0xFF
    t[0x10] = (t[0x08] - t[0x0F]) & 0xFF; t[0x11] = t[0x10] ^ t[0x07]; t[0x12] = t[0x0F] ^ 0xFF
    t[0x13] = t[0x03] ^ 0x10; t[0x14] = (t[0x04] - 0x32) & 0xFF; t[0x15] = (t[0x05] + 0xED) & 0xFF
    t[0x16] = t[0x06] ^ 0xF3; t[0x17] = (t[0x13] - t[0x0F]) & 0xFF; t[0x18] = (t[0x15] + t[0x07]) & 0xFF
    t[0x19] = (0x21 - t[0x13]) & 0xFF; t[0x1A] = t[0x14] ^ t[0x17]; t[0x1B] = (t[0x16] + t[0x16]) & 0xFF
    t[0x1C] = (t[0x17] + 0x44) & 0xFF; t[0x1D] = (t[0x03] + t[0x04]) & 0xFF; t[0x1E] = (t[0x05] - t[0x16]) & 0xFF
    t[0x1F] = t[0x1D] ^ t[0x13]
    v1 = bytes(t); v2 = bytes(x ^ 0xFF for x in t)
    return v1, v2

def unmask_video(d, v1, v2):
    """Vectorised form of the CRI loop:
         for i in 0x100..size: d[i] ^= m[i&31]; m[i&31] = d[i] ^ v2[i&31]   (m starts as v2)
         for i in 0..0x100:    m[i&31] ^= d[0x100+i]; d[i] ^= m[i&31]        (m starts as v1)
       (offsets relative to payload+0x40). Row k of the tail is XOR of cipher rows 0..k, ^v2 when k is even."""
    size = len(d) - 0x40
    if size < 0x200: return bytes(d)
    a = np.frombuffer(d, np.uint8).copy(); body = a[0x40:]
    V1 = np.frombuffer(v1, np.uint8); V2 = np.frombuffer(v2, np.uint8)
    n = size - 0x100; full = n // 32 * 32
    tail = body[0x100:0x100 + full].reshape(-1, 32)
    acc = np.bitwise_xor.accumulate(tail, axis=0)
    acc[0::2] ^= V2
    rem = n - full
    if rem:
        last = body[0x100 + full:0x100 + n] ^ (acc[-1][:rem] if len(acc) else 0) ^ V2[:rem]
    body[0x100:0x100 + full] = acc.reshape(-1)
    if rem: body[0x100 + full:0x100 + n] = last
    D = body[0x100:0x200].reshape(8, 32)
    m = np.bitwise_xor.accumulate(D, axis=0) ^ V1
    body[:0x100] ^= m.reshape(-1)
    return a.tobytes()

def demux(data, key=LK2_KEY):
    v1, v2 = make_masks(key); streams = {}
    p = 0
    while p + 16 <= len(data):
        sig = data[p:p + 4]; size, = struct.unpack(">I", data[p + 4:p + 8])
        hoff = data[p + 9]; pad, = struct.unpack(">H", data[p + 10:p + 12]); typ = data[p + 15] & 3
        if typ == 0 and sig in (b"@SFV", b"@ALP"):
            pl = data[p + 8 + hoff:p + 8 + size - pad]
            streams.setdefault(sig, bytearray()).extend(unmask_video(pl, v1, v2))
        p += 8 + size
    return streams

if __name__ == "__main__":
    src, out = sys.argv[1], sys.argv[2]
    s = demux(open(src, "rb").read()); base = os.path.join(out, os.path.splitext(os.path.basename(src))[0])
    if b"@SFV" in s: open(base + ".m1v", "wb").write(s[b"@SFV"])
    if b"@ALP" in s: open(base + "_alpha.m1v", "wb").write(s[b"@ALP"])

def decrypt_usm(data, key=LK2_KEY):
    """Return the same USM with @SFV/@ALP payloads unmasked in place (container bit-identical otherwise)."""
    v1, v2 = make_masks(key); out = bytearray(data); p = 0
    while p + 16 <= len(data):
        sig = data[p:p + 4]; size, = struct.unpack(">I", data[p + 4:p + 8])
        hoff = data[p + 9]; pad, = struct.unpack(">H", data[p + 10:p + 12]); typ = data[p + 15] & 3
        if typ == 0 and sig in (b"@SFV", b"@ALP"):
            a, b = p + 8 + hoff, p + 8 + size - pad
            out[a:b] = unmask_video(data[a:b], v1, v2)
        p += 8 + size
    return bytes(out)
