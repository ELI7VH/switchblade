# WAVE presets

**WAVE's factory bank is no longer converted from the VOID WAVE plugin.** Since Task 4.6 the 32 presets are original
material designed for the single-oscillator engine (`presets/four/wave.py`: ATMOS 8, LEAD 10, BASS 14). The conversion
tool of the Felucca port (`tools/void_presets.py`) is not part of FM1-Four, and nothing here is derived from the plugin's
`Resources/Presets/factory`.

Each preset declares a signature route (`sig`). `tools/four_presets.py` checks that the route holds in the preset's
routing and exits non-zero when two presets share (table, filter TYPE, signature).
