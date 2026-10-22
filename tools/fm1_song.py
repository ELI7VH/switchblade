#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Back up the FM-1's projects, or write the demo song (tools/make_demo.py's excerpt) onto it as a SONG, over the editor's SysEx.

  tools/fm1_song.py backup DIR           every backup object (0 the running project with its song, 1 the settings, 2..5 the
                                          project slots A..D, 6 / 7 the user preset banks) -> DIR/obj<id>.bin, checked by CRC
  tools/fm1_song.py restore DIR [IDS]    put them back (default the projects: 0 2 3 4 5)
  tools/fm1_song.py demo SONG.mid        the excerpt as patterns of 64 sixteenths (4 bars) in slots A..C, the sounds, BPM and
                                          the song's rows (a slot per track: lanes); slot D is left alone. Stops the transport.
  (needs mido + python-rtmidi; the FM-1 on USB)

The song: each track's 4-bar blocks of the excerpt, the same ones once: block i of a track is slot i (A, B, C), a block without
notes is that track's "-"; rows of equal blocks become one row with repeats. A step holds up to 4 notes starting on its sixteenth
(the few off the grid: the nearest), a note longer than a step its TIEs, the track's GATE the median length of its short notes.
"""
import statistics
import sys
import time
from pathlib import Path

import mido

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_demo import demo_notes  # noqa: E402

HDR = [0x7D, 0x46, 0x4C]
ED_SET, ED_PRESET, ED_PROJECT, ED_NAMES, ED_TRACK, ED_TRACK_STEP, ED_TRACK_PARAM, ED_SONG = 3, 8, 9, 10, 27, 30, 31, 33
ED_BACKUP_LIST, ED_BACKUP_GET, ED_BACKUP_PUT = 65, 66, 67
P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_VOICE, P_SQNT, G_BPM = 29, 30, 31, 32, 37, 94, 0
DIV_16, V_POLY, V_MONO, ST_NOTE, ST_TIE, ST_REST, SILENT = 2, 0, 1, 0, 1, 2, 4
# the demo's sounds, as web/emu/felucca_web.c web_demo: (engine, preset, voice or None to keep the preset's)
SOUNDS = [(0, "PLUCK", V_MONO), (1, "SERPENT", V_POLY), (2, "CAESIUM", None), (3, "DRIFT LEAD", V_POLY)]


def crc32(b):                                   # storage.c st_crc32: zlib's
    import zlib
    return zlib.crc32(bytes(b)) & 0xFFFFFFFF


def u32(n):
    return [(n >> (7 * i)) & 127 for i in range(5)]


def r32(p):
    return p[0] | p[1] << 7 | p[2] << 14 | p[3] << 21 | p[4] << 28


def v14(v):
    u = max(-8192, min(8191, v)) + 8192
    return [u & 127, u >> 7]


def pack7(b):
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 127 for x in g]
    return out


def unpack7(a):
    out, i = [], 0
    while i < len(a):
        m, g = a[i], a[i + 1:i + 8]
        out += [x | ((m >> j) & 1) << 7 for j, x in enumerate(g)]
        i += 1 + len(g)
    return out


class FM1:
    def __init__(self):
        pick = lambda names: next(n for n in names if any(k in n.lower() for k in ("fm-1", "felucca", "switch", "composite", "usb-midi")))
        self.i = mido.open_input(pick(mido.get_input_names()))
        self.o = mido.open_output(pick(mido.get_output_names()))

    def ask(self, cmd, args, timeout=2.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd} {args[:6]}")


def backup(dev, d):
    d.mkdir(parents=True, exist_ok=True)
    r = dev.ask(ED_BACKUP_LIST, [])
    if r[1]:
        sys.exit(f"backup: LIST refused (rc {r[1]}: 3 = stop the transport first)")
    for k in range(r[2]):
        e = r[3 + 11 * k:14 + 11 * k]
        oid, ln, crc = e[0], r32(e[1:6]), r32(e[6:11])
        data = []
        while len(data) < ln:
            c = min(256, ln - len(data))
            g = dev.ask(ED_BACKUP_GET, [oid] + u32(len(data)) + [c & 127, c >> 7])
            if g[1]:
                sys.exit(f"backup: GET {oid} at {len(data)} refused (rc {g[1]})")
            data += unpack7(g[9:])[:c]
        if crc32(data) != crc:
            sys.exit(f"backup: object {oid}: CRC mismatch")
        (d / f"obj{oid}.bin").write_bytes(bytes(data))
        print(f"backup: object {oid}: {ln} bytes, CRC {crc:08x} ok")


def restore(dev, d, ids):
    for oid in ids:
        data = list((d / f"obj{oid}.bin").read_bytes())
        r = dev.ask(ED_BACKUP_PUT, [0, oid] + u32(len(data)) + u32(crc32(data)))
        assert r[2] == 0, f"restore: begin {oid} rc {r[2]}"
        for p in range(0, len(data), 256):
            r = dev.ask(ED_BACKUP_PUT, [1, oid] + u32(p) + pack7(data[p:p + 256]))
            assert r[2] == 0, f"restore: data {oid} at {p} rc {r[2]}"
        r = dev.ask(ED_BACKUP_PUT, [2, oid], timeout=5.0)
        assert r[2] == 0, f"restore: commit {oid} rc {r[2]}"
        print(f"restore: object {oid}: {len(data)} bytes back")


def arrange(src):
    """-> per track: its distinct 64-step blocks [[step tuple]*64], the slot each 4-bar block plays, its GATE"""
    notes, bar, total, _ = demo_notes(src)
    q, blk = bar // 16, bar * 4
    nblk = total // blk
    out = []
    for t in range(4):
        mine = [n for n in notes if n[2] == t]
        short = [ln / q for _, ln, _, _, _ in mine if ln <= 1.25 * q]
        gate = max(16, min(127, round(128 * statistics.median(short)))) if short else 100
        blocks = []
        for b in range(nblk):
            st = [[] for _ in range(64)]
            for s, ln, _, note, vel in mine:
                k = round((s - b * blk) / q)
                if 0 <= k < 64:
                    st[k].append((note, vel, max(1, round(ln / q))))
            if not any(st):
                blocks.append(None)
                continue
            steps = [(0, [0, 0, 0, 0], ST_REST, 0)] * 64
            for k in range(64):
                if st[k]:
                    ns = sorted({n for n, _, _ in st[k]})[:4]
                    steps[k] = (len(ns), ns + [0] * (4 - len(ns)), ST_NOTE, max(v for _, v, _ in st[k]))
            for k in range(64):                                   # ties: up to the next note or the longest length
                if st[k]:
                    for j in range(k + 1, min(64, k + max(l for _, _, l in st[k]))):
                        if st[j]:
                            break
                        steps[j] = (0, [0, 0, 0, 0], ST_TIE, 0)
            blocks.append(tuple(steps))
        uniq = []
        for x in blocks:
            if x is not None and x not in uniq:
                uniq.append(x)
        assert len(uniq) <= 3, f"track {t + 1}: {len(uniq)} different blocks (slots A..C hold 3)"
        out.append((uniq, [SILENT if x is None else uniq.index(x) for x in blocks], gate))
    rows = []
    for b in range(nblk):
        lanes = [out[t][1][b] for t in range(4)]
        if rows and rows[-1][0] == lanes and rows[-1][1] < 16:
            rows[-1][1] += 1
        else:
            rows.append([lanes, 1])
    return out, rows, round(60000000 / demo_notes(src)[3])


def demo(dev, src):
    tracks, rows, bpm = arrange(src)
    print("demo: rows", " ".join("".join("ABC-"[min(s, 3)] if s != SILENT else "-" for s in l) + f"x{r}" for l, r in rows),
          "| GATE", [g for _, _, g in tracks], "| BPM", bpm)
    if dev.ask(ED_SONG, [3])[3]:                                   # (stop a running song)
        time.sleep(0.3)
    names = {}
    for t, (eng, name, voice) in enumerate(SOUNDS):
        if eng not in names:
            r = dev.ask(ED_NAMES, [eng])
            names[eng] = bytes(r[2:]).split(b"\0")[:r[1]]
        idx = [n.decode() for n in names[eng]].index(name)
        dev.ask(ED_TRACK, [t])
        r = dev.ask(ED_PRESET, [eng, idx], timeout=3.0)
        assert r[:2] == [eng, idx], f"track {t + 1}: preset {name} not set ({r[:2]})"
        for pid, v in [(P_SLEN, 64), (P_SDIV, DIV_16), (P_SSWING, 0), (P_SGATE, tracks[t][2]), (P_SQNT, 1)] + \
                      ([(P_VOICE, voice)] if voice is not None else []):
            r = dev.ask(ED_TRACK_PARAM, [t, pid] + v14(v))
            assert r[2] | r[3] << 7 == v + 8192, f"track {t + 1}: param {pid} -> {r}"
    r = dev.ask(ED_SET, [1, G_BPM] + v14(bpm))
    for slot in range(3):
        for t in range(4):
            uniq = tracks[t][0]
            steps = uniq[slot] if slot < len(uniq) else ((0, [0, 0, 0, 0], ST_REST, 0),) * 64
            for k, (n, ns, tm, vel) in enumerate(steps):
                want = [n] + ns + [tm, 0, vel, 0, 0, 0, 100, 1, 8, 0]
                r = dev.ask(ED_TRACK_STEP, [t, k] + want)
                assert r[2:2 + 8] == want[:8], f"slot {slot} track {t + 1} step {k}: {r}"
        r = dev.ask(ED_PROJECT, [1, slot], timeout=6.0)
        assert r == [1, slot, 1], f"save to slot {'ABC'[slot]}: {r}"
        print(f"demo: slot {'ABC'[slot]} saved")
    body = []
    for lanes, rep in rows:
        body += lanes + [rep]
    r = dev.ask(ED_SONG, [5, len(rows)] + body)
    assert r[1] == 0 and r[2] == len(rows), f"song rows: {r}"
    print(f"demo: song set, {len(rows)} rows; press SONG + PLAY on the FM-1")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("backup", "restore", "demo"):
        sys.exit(__doc__)
    dev = FM1()
    if sys.argv[1] == "backup":
        backup(dev, Path(sys.argv[2]))
    elif sys.argv[1] == "restore":
        restore(dev, Path(sys.argv[2]), [int(x) for x in sys.argv[3:]] or [0, 2, 3, 4, 5])
    else:
        demo(dev, Path(sys.argv[2]))


if __name__ == "__main__":
    main()
