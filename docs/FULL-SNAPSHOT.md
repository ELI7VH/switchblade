# Full installation snapshot

`tools/fm1_snapshot.py` backs up a stopped FM-1 without changing firmware.
The current editor archive alone exposes eight objects and omits the twelve
setlist slots; the full snapshot also reads their storage through the read-only
USB serial console.

```sh
python3 tools/fm1_snapshot.py \
  --output /path/to/private/FM1-TEST-snapshot \
  --serial-port /dev/cu.usbmodem1104 \
  --firmware build/switchblade-v0.1-wl-glo10.fwsc \
  --install-receipt build/install-glo10-2026-10-29.json
```

Requires mido, python-rtmidi and pyserial. Connect only the source unit, stopped,
with USB serial and CLOCK OUT enabled. Use its actual serial port and the
verified package/receipt for the installed build. Output must be a new folder.

The archive contains:

- Current music, settings, project slots and preset banks from CRC-checked MIDI backup.
- Every byte from flash `0x93000..0xFFFFF`, read twice and compared exactly.
- CRC-validated native A/B objects, including every stored song slot. A corrupt
  newer copy falls back to the older valid copy, with wrap-aware generation order.
- Exact firmware package, installed-build receipt, matching Git source archive,
  MiniLab preset reference, live master-control state and SHA-256 manifest.

The snapshot command periodically holds autosave through the existing backup
protocol while reading storage. It performs no firmware or flash writes.
`logical/obj0.bin` is the authoritative running music and may be newer than its
stored song slot; the manifest records the current song number.

Before a show-unit transfer, identify and back up FM1-SHOW too. Decide whether
to retain its music/settings or copy the test unit's. Do not write the raw
storage image wholesale onto another device: it includes device storage outside
musical objects. The current editor restores logical IDs 0..7 only; exported
setlist objects require a deliberate restore path, not invented editor IDs.

Private snapshots belong outside Git. The source and show unit each retain their
own recovery archive. FM1-GHOUL remains excluded from flashing.

Validation: `python3 tests/fm1_snapshot_test.py` covers split-sector payloads,
corrupt/torn-copy fallback, generation wrap, empty records, old payload sizes,
all 34 distinct extension sectors and truncated snapshots.

## Song transfer bridge (xfr11)

xfr11 advertises explicit backup IDs 32..43 for the twelve setlist slots.
Each uses the existing validated project import, CRC and A/B storage commit;
zero-length records clear only that musical slot. No raw addresses are accepted.
LIST/GET preserve the captured runtime while reading stored songs. Runtime PUT
also selects its stored song number and saves the startup snapshot immediately
(RESTORE LAST must be ON). Restore settings, banks and slots first, then the
authoritative running object last. Read back every object before returning to
the exact glo10 package if that is the requested installation.

The SHOW recovery archive retains its older firmware name and last recorded
source separately; no matching original firmware binary was available.
