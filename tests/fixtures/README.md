# Synthetic legacy project

`for4-64.bin` is a 3840-byte FOR4 project generated with Switch Blade commit
`31d0af6` using its real `project_capture` / `proj_pack` functions. It contains
factory sounds, a 64-step drum track, hits/accents on step 64 and a level lock
on that step. No device or personal song data is included.

`seq_128_test.c` checks that FOR5 imports the legacy length, hit/accents and
track/step automation address correctly, and leaves steps 65–128 empty.
