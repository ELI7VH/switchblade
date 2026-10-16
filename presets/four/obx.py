# OB-Z: 32 factory presets (BRASS 8, STRINGS 8, KEYS 8, LEAD 8).
# e = WAVE (0 SAW 1 SQR 2 TRI 3 SIN 4 PWM 5 SYNC 6 SUB), DTN, MIX, NOIS, CUT, RES, DRV, KTR
# env = ATK DEC SUS REL; fenv = ENV -> FILTER (-64..63); fx = DIST CHORUS DELAY REVERB sends
PRESETS = [
    # --- LEAD ---
    dict(name="SAW LEAD", cat="LEAD", e=[0, 12, 64, 0, 90, 30, 10, 64], env=[4, 70, 100, 50], fenv=20, mono=1, fx=(0, 10, 45, 30), pat=4),
    dict(name="SQR LEAD", cat="LEAD", e=[1, 0, 0, 0, 80, 50, 40, 64], env=[0, 60, 90, 30], fenv=30, mono=1, fx=(5, 0, 35, 20), pat=4),
    dict(name="ACID LEAD", cat="LEAD", e=[0, 0, 0, 0, 50, 100, 25, 64], env=[0, 55, 20, 30], fenv=48, mono=1, fx=(20, 0, 45, 15), pat=1),
    dict(name="RAVE", cat="LEAD", e=[4, 30, 64, 0, 85, 20, 30, 64], env=[20, 80, 110, 60], fenv=10, mono=1, fx=(30, 40, 30, 30), pat=9),
    dict(name="SUB LEAD", cat="LEAD", e=[3, 0, 0, 0, 90, 0, 20, 0], env=[0, 60, 100, 25], fenv=0, mono=1, fx=(0, 0, 25, 20), pat=4),
    dict(name="SYNC LEAD", cat="LEAD", e=[5, 40, 100, 0, 95, 25, 15, 64], env=[2, 70, 100, 40], fenv=36, mono=1, fx=(8, 0, 40, 25), pat=4),
    dict(name="PWM LEAD", cat="LEAD", e=[4, 16, 70, 0, 88, 30, 5, 64], env=[6, 70, 100, 45], fenv=18, mono=1, fx=(0, 25, 40, 30), pat=4),
    dict(name="FAT LEAD", cat="LEAD", e=[0, 34, 100, 0, 76, 40, 35, 64], env=[3, 70, 105, 50], fenv=26, mono=1, fx=(12, 20, 45, 35), pat=4),
    # --- BRASS ---
    dict(name="BRASS", cat="BRASS", e=[0, 10, 64, 0, 45, 20, 10, 64], env=[35, 70, 90, 45], fenv=40, mono=0, fx=(0, 20, 20, 40), pat=6),
    dict(name="ROYAL HORNS", cat="BRASS", e=[0, 12, 64, 0, 78, 34, 10, 64], env=[18, 70, 96, 50], fenv=28, mono=0, fx=(0, 24, 20, 40), pat=6),
    dict(name="WIND", cat="BRASS", e=[0, 0, 0, 90, 30, 90, 0, 0], env=[60, 90, 60, 80], fenv=50, mono=0, fx=(0, 30, 30, 70), pat=5),
    dict(name="SOLO TRUMPET", cat="BRASS", e=[0, 6, 40, 0, 70, 25, 15, 80], env=[10, 60, 100, 35], fenv=45, mono=1, fx=(0, 8, 25, 30), pat=4),
    dict(name="MELLOW BONE", cat="BRASS", e=[0, 8, 64, 0, 36, 12, 6, 64], env=[30, 70, 95, 40], fenv=30, mono=0, fx=(0, 16, 20, 40), pat=6),
    dict(name="SYNTH BRASS", cat="BRASS", e=[0, 18, 90, 0, 60, 15, 20, 64], env=[14, 75, 100, 45], fenv=34, mono=0, fx=(0, 30, 15, 35), pat=6),
    dict(name="FANFARE", cat="BRASS", e=[4, 14, 80, 0, 66, 20, 25, 64], env=[8, 65, 100, 45], fenv=50, mono=0, fx=(0, 20, 20, 50), pat=6),
    dict(name="JUMP STAB", cat="BRASS", e=[0, 8, 64, 0, 85, 15, 30, 64], env=[0, 50, 40, 30], fenv=55, mono=0, fx=(5, 10, 30, 30), pat=6),
    # --- STRINGS ---
    dict(name="STRINGS", cat="STRINGS", e=[0, 25, 64, 0, 70, 10, 0, 32], env=[70, 90, 115, 90], fenv=5, mono=0, fx=(0, 60, 20, 70), pat=5),
    dict(name="PWM STR", cat="STRINGS", e=[4, 8, 40, 0, 75, 20, 0, 48], env=[60, 80, 110, 85], fenv=8, mono=0, fx=(0, 50, 20, 60), pat=5),
    dict(name="SOFT PAD", cat="STRINGS", e=[0, 20, 64, 4, 60, 10, 0, 32], env=[80, 90, 110, 95], fenv=10, mono=0, fx=(0, 60, 20, 70), pat=5),
    dict(name="SOLINA", cat="STRINGS", e=[0, 30, 90, 0, 82, 5, 0, 40], env=[50, 85, 118, 80], fenv=0, mono=0, fx=(0, 90, 15, 50), pat=5),
    dict(name="SLOW BOW", cat="STRINGS", e=[0, 14, 64, 0, 52, 15, 0, 48], env=[100, 90, 118, 100], fenv=22, mono=0, fx=(0, 40, 20, 80), pat=5),
    dict(name="CELLO", cat="STRINGS", e=[0, 6, 40, 0, 40, 20, 10, 64], env=[45, 80, 110, 70], fenv=26, mono=0, fx=(0, 20, 10, 50), pat=5),
    dict(name="VIOLINS", cat="STRINGS", e=[0, 22, 64, 0, 100, 8, 0, 90], env=[60, 85, 115, 75], fenv=4, mono=0, fx=(0, 70, 25, 70), pat=5),
    dict(name="TAPE STR", cat="STRINGS", e=[4, 20, 70, 6, 56, 14, 18, 40], env=[55, 85, 112, 85], fenv=12, mono=0, fx=(8, 80, 30, 60), pat=5),
    # --- KEYS ---
    dict(name="SINE KEY", cat="KEYS", e=[3, 6, 50, 0, 127, 0, 0, 0], env=[2, 80, 30, 70], fenv=0, mono=0, fx=(0, 30, 25, 40), pat=6),
    dict(name="PLUCK", cat="KEYS", e=[0, 8, 50, 0, 30, 40, 0, 64], env=[0, 88, 0, 60], fenv=55, mono=0, fx=(0, 20, 50, 30), pat=3),
    dict(name="E PIANO", cat="KEYS", e=[2, 4, 50, 0, 80, 5, 0, 80], env=[1, 90, 20, 65], fenv=20, mono=0, fx=(0, 50, 25, 45), pat=6),
    dict(name="CLAV", cat="KEYS", e=[1, 3, 30, 0, 62, 35, 8, 100], env=[0, 55, 0, 30], fenv=48, mono=0, fx=(0, 0, 20, 20), pat=6),
    dict(name="ORGAN", cat="KEYS", e=[6, 6, 100, 0, 110, 0, 0, 0], env=[1, 100, 120, 20], fenv=0, mono=0, fx=(0, 60, 0, 40), pat=6),
    dict(name="VIBES", cat="KEYS", e=[3, 12, 80, 0, 120, 0, 0, 20], env=[0, 96, 0, 85], fenv=0, mono=0, fx=(0, 40, 40, 55), pat=7),
    dict(name="SOFT BELL", cat="KEYS", e=[3, 25, 100, 0, 100, 10, 0, 40], env=[0, 100, 0, 95], fenv=10, mono=0, fx=(0, 30, 45, 70), pat=7),
    dict(name="WURLY", cat="KEYS", e=[2, 8, 70, 0, 66, 10, 25, 70], env=[1, 85, 40, 60], fenv=24, mono=0, fx=(5, 55, 20, 35), pat=6),
]
