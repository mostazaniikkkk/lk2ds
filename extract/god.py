"""Parser for Konami KLU 'GOD' graphics ROM (format 7.03), layouts from GodEngine.Structures_703 (on-disk sizes verified)."""
import struct
NAMES = "GPJ EDA WK3 SCR SN2 TRG SQ2 MSK INS ANG AGI AN2 PTNTBL PTN PRMTBL PRM RSC ACG ACT PAR IMG IMA MVA MVI MAP POS ANC ZOM PIT COL".split()
FMT = {  # struct formats (little endian)
    "SN2": "<II",                 # TrigSize, Trig
    "TRG": "<15I",                # SeqName SeqWrk SeqWrkJdg SeqWrkNum SeqTime SeqDuration Inherit InheritFlags Pause MapX MapY Pri Target ReferTag Type
    "SQ2": "<10I",                # AreaX AreaY PreDur LoopDur PreInsSize LoopInsSize PreMask LoopMask PreIns LoopIns
    "INS": "<IIiIIIIIII",         # Time Duration Priority ColMode TagName ReferTag AngName AnmWrk AcgName ActWrk
    "MSK": "<IIiIIIIIII",
    "ANG": "<II",                 # AnmSize Anm
    "AGI": "<III",                # AnmName RscLstWrk SyncFlag
    "AN2": "<IIII",               # PtnSize Duration PtnType PtnID
    "PTNTBL": "<I", "PRMTBL": "<I", "IMG": "<I",
    "PTN": "<II",                 # PrimSize PrimID
    "PRM": "<IIIII",              # RscName RscLstIndex RscImgIndex ColMode Coef
    "RSC": "<III",                # LstSize ImgSize Lst
    "MVA": "<IHHHBBIIIIB3x",      # Ident W H LineSize Type Mode A32Sum Size Addr Duration Fps
    "IMA": "<IHHHBBI16s",
    "GPJ": "<5I",
}
class God:
    def __init__(self, path):
        self.d = open(path, "rb").read(); self.base = struct.unpack_from("<I", self.d, 0x70)[0]
        self.tab = {n: struct.unpack_from("<II", self.d, self.base + i * 8) for i, n in enumerate(NAMES)}
        self.cache = {}
    def table(self, name):
        if name not in self.cache:
            n, off = self.tab[name]; f = FMT[name]; sz = struct.calcsize(f)
            self.cache[name] = [struct.unpack_from(f, self.d, self.base + off + i * sz) for i in range(n)] if n else []
        return self.cache[name]

def scene_names(vm_path):
    """Scene names from the VM (main.bin): typed-data array of string refs (0x32xxxxxx), one per GOD scene, alphabetical."""
    import re
    v = open(vm_path, "rb").read()
    hdr = [struct.unpack_from(">I", v, 0x40 + i * 4)[0] for i in range(32)]
    sec = hdr[18:30]                                   # section offsets (relative to 0x40)
    strtab, pool, data0, data1 = 0x40 + sec[1], 0x40 + sec[11], 0x40 + sec[7], 0x40 + sec[8]
    def s(i):
        l, o = struct.unpack_from(">II", v, strtab + i * 8); return v[pool + o:pool + o + l].decode("latin1")
    W = [struct.unpack_from(">I", v, p)[0] for p in range(data0, data1, 4)]
    for i, w in enumerate(W):                          # first run of consecutive string refs that starts with a scene name
        if w >> 24 == 0x32 and s(w & 0xFFFFFF).startswith("Sn_") and W[i - 1] == 0x10FFFFFF:
            j = i
            while j < len(W) and W[j] >> 24 == 0x32: j += 1
            return [s(x & 0xFFFFFF) for x in W[i:j]]

def scene_movies(g, k):
    """All movie uses reachable from GOD scene k: list of dicts (movie, start frame within scene, trigger info)."""
    SN2, TRG, SQ2, INS, ANG, AGI, AN2 = (g.table(n) for n in ("SN2", "TRG", "SQ2", "INS", "ANG", "AGI", "AN2"))
    out = []
    tsize, t0 = SN2[k]
    for t in range(t0, t0 + tsize):
        tr = TRG[t]; seq, seqwrk, jdg, wrknum, seqtime, seqdur = tr[:6]
        if seq >= len(SQ2): continue
        sq = SQ2[seq]
        for part, (n, s0) in (("pre", (sq[4], sq[8])), ("loop", (sq[5], sq[9]))):
            if s0 == 0xFFFFFFFF: continue
            for r in range(s0, s0 + n):
                role = INS[r]; time, dur, ang, anmwrk = role[0], role[1], role[6], role[7]
                if ang >= len(ANG): continue
                asz, a0 = ANG[ang]
                for ai in range(a0, a0 + asz):
                    an = AGI[ai][0]
                    if an < len(AN2) and AN2[an][2] == 1:
                        out.append({"movie": AN2[an][3], "frames": AN2[an][1], "seq": seq, "part": part,
                                    "start": seqtime + time + (sq[2] if part == "loop" else 0), "role_dur": dur, "priority": role[2], "trig_pri": tr[11], "variant_wrk": anmwrk if asz > 1 else None,
                                    "cond": None if seqwrk == 0xFFFFFFFF else (seqwrk, jdg, wrknum), "trigger": t})
    return out


def scene_timeline(g, k):
    """Visible movie per frame for one pass of scene k (pre + one loop): list of segments (movie, in_frame, n_frames).
    The topmost active movie role wins (trigger priority, then role priority); a role shows min(role duration, movie length)."""
    uses = scene_movies(g, k)
    if not uses: return [], 0
    SQ2 = g.table("SQ2")
    length = max(SQ2[u["seq"]][2] + SQ2[u["seq"]][3] for u in uses)
    length = max(length, max(u["start"] + min(u["role_dur"] or u["frames"], u["frames"]) for u in uses if u["part"] == "pre") if any(u["part"] == "pre" for u in uses) else length)
    segs = []
    for t in range(length):
        act = [u for u in uses if u["start"] <= t < u["start"] + min(u["role_dur"] or u["frames"], u["frames"]) and u["cond"] is None and u["variant_wrk"] is None]
        if not act: act = [u for u in uses if u["start"] <= t < u["start"] + min(u["role_dur"] or u["frames"], u["frames"])]
        if not act: continue
        u = max(act, key=lambda u: (u["trig_pri"], u["priority"]))
        f = t - u["start"]
        if segs and segs[-1][0] == u["movie"] and segs[-1][1] + segs[-1][2] == f and segs[-1][3] == u["start"]: segs[-1][2] += 1
        else: segs.append([u["movie"], f, 1, u["start"]])
    return [(m, f, n) for m, f, n, _ in segs], length
