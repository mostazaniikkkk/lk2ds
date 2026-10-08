"""QA save: everything unlocked (all gallery items, Klein date), lots of credits.
Layout = save_t in game/arm9/source/save.h (version 1, 92 bytes), written into a 4 KB file.
usage: make_qa_save.py <out.sav> [credits]
"""
import struct, sys

SAVE_ITEMS = 256


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def main():
    out = sys.argv[1]
    credits = int(sys.argv[2]) if len(sys.argv) > 2 else 999999
    owned = [0xFFFFFFFF] * (SAVE_ITEMS // 32)  # every item, the confession flags (Klein unlock) included
    body = struct.pack('<IHHi', 0x56534B4C, 1, 92, credits)
    body += struct.pack('<8I', *owned)
    body += struct.pack('<6I', 0, 0, 0, 0, 0, 0)   # games, big, reg, art, dates, confessions
    body += struct.pack('<2I', 0, 0)               # medals in / out
    body += struct.pack('<I', 0)                   # best ART
    body += struct.pack('<6B', 6, 0, 0, 0, 0, 10)  # settings: "?", L>C>R, wait cut off, auto aim off, guide off, vol 10
    body += b'\0' * 2                              # alignment of sum
    assert len(body) == 88
    data = body + struct.pack('<I', fnv(body))
    with open(out, 'wb') as f:
        f.write(data + b'\0' * (4096 - len(data)))
    print(out, len(data), 'bytes, credits', credits)


if __name__ == '__main__':
    main()
