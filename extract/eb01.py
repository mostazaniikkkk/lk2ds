"""Decryptor for Konami LibCryptStream 'EB01' files (LK2 CDN assets).

Layout: b'EB01' + uint32 BE salt + ciphertext.
  table   = 256 bytes from xorshift64, state = (salt^appKey) | (salt^libKey) << 32,
            2 warm-up rounds, then 4 bytes (BE of low 32 bits) per round.
  plain[p] = ror8(cipher[p], p % 7) ^ table[p % 251]      (p = offset after header)
Reversed from libil2cpp.so: LibCryptStream.CryptStream_EB01 (initCrypt/initShuffle/decryptByte).
"""
import sys, os, numpy as np

APP_KEY = 0x539DCBF9   # BtoCLK2.ResourceLoaderResolverLK2._appKey
LIB_KEY = 0xB2BAF62C   # CryptStream_EB01.libKey
M64 = (1 << 64) - 1

def _step(x):
    x ^= (x << 13) & M64; x ^= x >> 7; x ^= (x << 17) & M64
    return x

def make_table(salt, app_key=APP_KEY):
    x = ((salt ^ app_key) & 0xFFFFFFFF) | (((salt ^ LIB_KEY) & 0xFFFFFFFF) << 32)
    x = _step(_step(x))
    t = bytearray()
    for _ in range(64):
        x = _step(x)
        t += (x & 0xFFFFFFFF).to_bytes(4, "big")
    return np.frombuffer(bytes(t), np.uint8)

def decrypt_file(src, dst, app_key=APP_KEY, chunk=7 * 251 * 4096):
    with open(src, "rb") as f, open(dst, "wb") as o:
        hdr = f.read(8)
        if hdr[:4] != b"EB01": raise ValueError(f"{src}: not EB01 ({hdr[:4]!r})")
        table = make_table(int.from_bytes(hdr[4:8], "big"), app_key)
        # keystream period is lcm(7, 251) = 1757; chunk is a multiple of it
        p = np.arange(chunk, dtype=np.int64)
        rot = (p % 7).astype(np.uint8); key = table[p % 251]
        while True:
            b = np.frombuffer(f.read(chunk), np.uint8)
            if not len(b): break
            r = rot[:len(b)]
            o.write((((b >> r) | (b << ((8 - r) & 7))) ^ key[:len(b)]).astype(np.uint8).tobytes())

if __name__ == "__main__":
    for src in sys.argv[1:]:
        dst = src[:-6] + ".dec" + src[-6:] if src.endswith(".asset") else src + ".dec"
        decrypt_file(src, dst); print(src, "->", dst)
