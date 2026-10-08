"""GBA/DS BIOS LZ77 (type 0x10) compressor, VRAM-safe (no back-reference at distance 1, so the
halfword-writing decompressor never reads a byte it hasn't stored yet)."""


def compress(data: bytes) -> bytes:
    n = len(data)
    out = bytearray([0x10, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    heads = {}  # 3-byte key -> list of recent positions
    i = 0
    while i < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            best_len, best_dist = 0, 0
            if i + 3 <= n:
                key = data[i:i + 3]
                cands = heads.get(key, ())
                for p in reversed(cands[-48:]):
                    dist = i - p
                    if dist > 4096:
                        break
                    if dist < 2:
                        continue
                    l = 3
                    while l < 18 and i + l < n and data[p + l] == data[i + l]:
                        l += 1
                    if l > best_len:
                        best_len, best_dist = l, dist
                        if l == 18:
                            break
            if best_len >= 3:
                flags |= 0x80 >> bit
                d = best_dist - 1
                out.append(((best_len - 3) << 4) | (d >> 8))
                out.append(d & 0xFF)
                step = best_len
            else:
                out.append(data[i])
                step = 1
            for k in range(i, min(i + step, n - 2)):
                heads.setdefault(data[k:k + 3], []).append(k)
            i += step
        out[flag_pos] = flags
    while len(out) & 3:
        out.append(0)
    return bytes(out)


def decompress(src: bytes) -> bytes:
    n = src[1] | (src[2] << 8) | (src[3] << 16)
    out = bytearray()
    i = 4
    while len(out) < n:
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= n:
                break
            if flags & (0x80 >> bit):
                l = (src[i] >> 4) + 3
                d = (((src[i] & 15) << 8) | src[i + 1]) + 1
                i += 2
                for _ in range(l):
                    out.append(out[-d])
            else:
                out.append(src[i])
                i += 1
    return bytes(out[:n])
