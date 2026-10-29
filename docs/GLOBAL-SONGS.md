# Song shortcuts — v0.1-wl-glo10

Hold the physical **GLOBAL / GLO** button:

- Tap **OCT−**: previous song.
- Tap **OCT+**: next song.
- Hold **OCT+** for 0.7 seconds: copy into the next free song, once per hold.

These shortcuts work from HOME and ordinary pages while stopped. The current
song is kept before switching. While playing, STOP TO CHANGE SONG is shown.
Menus, naming and confirmation dialogs keep their own octave-button controls.

Without GLOBAL, HOME's octave buttons change pitch normally. Both octave
buttons together still reset pitch to zero. With GLOBAL held, both buttons
cancel the song gesture instead. Releasing GLOBAL before releasing the octave
button cancels an unfinished gesture; it cannot turn into a pitch edit, a
second song switch or a page tap.

GLOBAL's held overlay shows PREV and NEXT/COPY. A locked GLOBAL layer without
the physical button held retains its existing mix UNDO; locking is not the song
modifier. Sound, naming, metronome settings, sequence data and file formats are
unchanged by this routing update.

## Checks

The full native UI suite passes. Gesture checks cover repeat taps under one
GLOBAL hold, early modifier release, both buttons, one copy per long hold,
playback refusal, page preservation, ordinary HOME pitch and use away from HOME.
Existing song-name/copy/switch persistence checks pass (14 checks). Rendering has
zero lint or alignment failures across palettes, styles and font sizes.

Installed on stopped FM1-TEST on 2026-10-29. Current music restored after reboot;
all eight exposed backup objects match the fresh backup byte for byte. Receipt:
`build/install-glo10-2026-10-29.json`. Physical button audition remains for the user.
