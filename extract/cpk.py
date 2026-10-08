"""Minimal CRI CPK reader: parses @UTF tables, lists/extracts TOC files (handles CRILAYLA)."""
import struct, sys, os

def parse_utf(b):
    assert b[:4] == b"@UTF", b[:4]
    size, rows_off, str_off, data_off, name_off, ncol, row_len, nrows = struct.unpack(">IHIIIHHI", b[4:32][:4] + b[8:10] + b[10:14] + b[14:18] + b[18:22] + b[22:24] + b[24:26] + b[26:30]) if False else (None,)*8
    t = b[8:]  # offsets are relative to byte 8
    rows_off, = struct.unpack(">H", b[10:12]); rows_off += 0  # unused hi
    rows_off = struct.unpack(">I", b[8:12])[0] & 0xFFFF
    str_off, data_off, name_off = struct.unpack(">III", b[12:24])
    ncol, row_len = struct.unpack(">HH", b[24:28]); nrows, = struct.unpack(">I", b[28:32])
    def cstr(o):
        o += str_off; e = t.index(b"\0", o); return t[o:e].decode("utf-8", "replace")
    p = 32 - 8; cols = []
    sizes = {0: (">B", 1), 1: (">b", 1), 2: (">H", 2), 3: (">h", 2), 4: (">I", 4), 5: (">i", 4),
             6: (">Q", 8), 7: (">q", 8), 8: (">f", 4), 9: (">d", 8)}
    def read(typ, p):
        if typ in sizes: f, n = sizes[typ]; return struct.unpack_from(f, t, p)[0], p + n
        if typ == 0xA: o, = struct.unpack_from(">I", t, p); return cstr(o), p + 4
        if typ == 0xB: o, n = struct.unpack_from(">II", t, p); return (data_off + o, n), p + 8
        raise ValueError(typ)
    for _ in range(ncol):
        flags = t[p]; p += 1
        name, = struct.unpack_from(">I", t, p); p += 4
        col = {"name": cstr(name), "type": flags & 0xF, "storage": flags & 0xF0}
        if col["storage"] == 0x30: col["const"], p = read(col["type"], p)
        cols.append(col)
    rows = []; p = rows_off
    for _ in range(nrows):
        r = {}
        for c in cols:
            if c["storage"] == 0x50: r[c["name"]], p = read(c["type"], p)
            elif c["storage"] == 0x30: r[c["name"]] = c["const"]
            else: r[c["name"]] = None
        rows.append(r)
    return rows, t

def table_at(f, off, magic):
    f.seek(off); h = f.read(16); assert h[:4] == magic, (h[:4], magic)
    size, = struct.unpack("<I", h[8:12])
    return parse_utf(f.read(size))

def decompress_layla(src):
    # CRILAYLA (reverse LZ), standard algorithm
    usize, hsize = struct.unpack_from("<II", src, 8)
    out = bytearray(usize + 0x100)
    out[:0x100] = src[0x10 + hsize:0x10 + hsize + 0x100]
    comp = src[0x10:0x10 + hsize]
    pos = len(comp) - 1; bitpool = 0; bits_left = 0; outp = 0x100 + usize - 1
    def get(n):
        nonlocal pos, bitpool, bits_left
        v = 0
        while n:
            if bits_left == 0:
                bitpool = comp[pos]; pos -= 1; bits_left = 8
            take = min(bits_left, n)
            v = (v << take) | ((bitpool >> (bits_left - take)) & ((1 << take) - 1))
            bits_left -= take; n -= take
        return v
    end = 0x100
    while outp >= end:
        if get(1):
            back = outp + 3 + get(13); ln = 3
            for lv in (2, 3, 5, 8):
                x = get(lv); ln += x
                if x != (1 << lv) - 1: break
            else:
                while True:
                    x = get(8); ln += x
                    if x != 0xFF: break
            for _ in range(ln):
                out[outp] = out[back]; outp -= 1; back -= 1
        else:
            out[outp] = get(8); outp -= 1
    return bytes(out[:0x100 + usize])

def open_cpk(path):
    f = open(path, "rb")
    hdr, _ = table_at(f, 0, b"CPK ")
    h = hdr[0]
    toc, _ = table_at(f, h["TocOffset"], b"TOC ")
    base = min(h["TocOffset"], h.get("ContentOffset") or h["TocOffset"])
    return f, h, toc, h["TocOffset"]

if __name__ == "__main__":
    path = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else None
    f, h, toc, base = open_cpk(path)
    print({k: v for k, v in h.items() if not isinstance(v, tuple)})
    for e in toc[:10]: print(e)
    print(len(toc), "files")
    if out:
        for e in toc:
            name = "/".join(x for x in (e.get("DirName") or "", e["FileName"]) if x)
            f.seek(base + e["FileOffset"]); data = f.read(e["FileSize"])
            if data[:8] == b"CRILAYLA": data = decompress_layla(data)
            dst = os.path.join(out, name); os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
            open(dst, "wb").write(data)
        print("extracted to", out)
