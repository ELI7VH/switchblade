# Parameter popup — card12

While a knob/fader parameter popup is active, only its rectangle is drawn.
The underlying HOME or parameter page stays frozen. After 800 ms without another
movement, the popup closes and the page redraws completely with current values
and playheads. Audio, sequencing and MIDI continue running. Menus, dialogs,
name entry, count-in and held-button layers retain screen priority.

This removes the underlying animation's erase/redraw between LCD strip writes,
which could visibly wipe through the popup during playback. It adds no framebuffer.

The UI suite checks LCD write boundaries during playback and repeated controller
moves, HOME and parameter pages, fader/ADSR changes, idle timeout and full redraw.
The 150,000-frame soak tests background consistency when the popup is absent;
its quiet comparison uses the hot timer actually rendered, before end-of-frame
decrement. Full UI tests pass; target build passes; renderer reports zero lint,
wrong-palette and alignment failures.

MiniLab K1–4 ADSR is a separate controller preset change (CC73,75,79,72).
Re-import controllers/fm1-sb-live.minilab3 in MIDI Control Center and Store To
the desired MiniLab User memory. K5–8 remain repeat, dirt, delay and DJ filter.
