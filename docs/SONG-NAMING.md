# Song names and save repair — v0.1-wl-save7

SAVE's former PROJECT A–D page now opens **SONG NAME** directly.
Type with the keys, use K1 to move, K2 to change a character, or K3 for a
random name. After three seconds without edits, the current setlist song and
its startup snapshot are saved (with RESTORE LAST enabled). The editor stays open. BACK or SAVE also
finishes pending edits before leaving.

The footer shows AUTO initially, EDITING while changed, PENDING during
playback, and SAVED only after the write succeeds. Writes wait for a stopped,
quiet instrument. A failed save keeps the draft and retries after another
idle interval. Song names are at most 12 characters. Existing internal A–D
slots and song-chain references remain compatible; physical SAVE navigation
uses the song-name editor instead of the slot screen.

HOME's OCT−/OCT+ still switch setlist songs, and holding OCT+ copies the current
song into the next free slot. Naming saves the current song, without needing
an A–D slot. RESTORE LAST remains the startup/autosave preference in the menu.

## Save failure

The original 128-step update added extension sectors at `0xB8000..0xD9FFF`
but omitted them from the hardware flash write guard. Thus project, song
and autosave writes of 6400 bytes failed when they reached an extension.
The old host emulator did not enforce this hardware guard, so its tests passed.

The target and host tests now share `hal/fm1_flash_regions.h`. It explicitly
allows the 34 extension sectors while excluding `0xDA000..0xDBFFF`, the old
unused `0x9F000` sector, and application writes into OTA staging. No project
format, layout or existing data address changes in this repair.

## Verification

- Old target guard reproduces the failed 6400-byte song save.
- With the fix, all 17 extended objects save through both A/B copies using
  the actual target write guard. All 6432 interrupted-write cases preserve
  the previous song; corrupt-extension CRC falls back correctly.
- Song-name UI debounce/draft checks and durable naming, playback refusal,
  write-failure rollback, duplication and song-switch tests pass.
- Full UI suite, persistence, backup, editor, MIDI, played holds and K1 stretch
  tests pass. UI rendering: zero lint findings and zero alignment failures
  across 1,744,720 measurements.
- Installed on FM1-TEST on 2026-10-29 after a fresh stopped-unit backup.
  A real 6400-byte save was acknowledged and read back exactly; the temporary
  test slot was returned to empty. The current music matches the canonical
  FOR5-to-FOR6 conversion and all other exposed backup objects match byte for
  byte. Physical typing and long-OCT audition remain for the user.
