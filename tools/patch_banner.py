"""Write the banner titles as UTF-16 directly into the ROM (ndstool mangles non-ASCII like the © sign)
and fix the banner CRC.
Usage: patch_banner.py <rom.nds> "<line1>\\n<line2>\\n<line3>" ["<japanese title lines>"]
The optional Japanese title goes to the Japanese-language slot; every other language gets the first one."""
import struct, sys


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def main(rom, title, title_jp=None):
    with open(rom, 'r+b') as f:
        d = bytearray(f.read())
        off = struct.unpack_from('<I', d, 0x68)[0]
        for lang in range(6):  # JP, EN, FR, DE, IT, ES titles (banner version 1)
            t = title_jp if (lang == 0 and title_jp) else title
            text = t.encode('utf-16le')[:0x100 - 2]
            base = off + 0x240 + lang * 0x100
            d[base:base + 0x100] = text.ljust(0x100, b'\0')
        struct.pack_into('<H', d, off + 2, crc16(d[off + 0x20:off + 0x840]))
        f.seek(0)
        f.write(d)


if __name__ == '__main__':
    unescape = lambda s: s.replace('\\n', '\n')
    main(sys.argv[1], unescape(sys.argv[2]), unescape(sys.argv[3]) if len(sys.argv) > 3 else None)
