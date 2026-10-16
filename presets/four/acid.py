# ACID: 32 seeds, four styles of 8 (CLASSIC, SQUELCH, DISTORTED, DEEP; the category of all is ACID).
# e = WAVE (0 SAW 1 SQR), TUNE, DECY, ACC, CUT, RES, DRV, SQL. Placeholders until the real ACID DSP (Phase 5)
PRESETS = [
    dict(name="ACID TRAX", cat="ACID", e=[0, 0, 60, 80, 50, 90, 0, 0], env=[0, 70, 15, 30], fenv=48, mono=1,
         fx=(10, 0, 40, 15), pat=1),   # CLASSIC
    dict(name="PHUTURE", cat="ACID", e=[0, 0, 55, 90, 44, 100, 0, 0], env=[0, 65, 15, 30], fenv=52, mono=1,
         fx=(10, 0, 45, 15), pat=1),   # CLASSIC
    dict(name="BASSLINE", cat="ACID", e=[1, 0, 50, 70, 56, 85, 0, 0], env=[0, 60, 15, 30], fenv=40, mono=1,
         fx=(5, 0, 35, 10), pat=1),   # CLASSIC
    dict(name="ROLAND", cat="ACID", e=[0, 0, 64, 85, 60, 80, 5, 0], env=[0, 74, 15, 30], fenv=45, mono=1,
         fx=(0, 0, 40, 20), pat=1),   # CLASSIC
    dict(name="BLEEP", cat="ACID", e=[1, 12, 35, 100, 70, 70, 0, 0], env=[0, 45, 15, 30], fenv=50, mono=1,
         fx=(0, 0, 50, 25), pat=1),   # CLASSIC
    dict(name="HOUSE ACID", cat="ACID", e=[0, 0, 45, 60, 64, 75, 10, 0], env=[0, 55, 15, 30], fenv=36, mono=1,
         fx=(5, 10, 35, 25), pat=1),   # CLASSIC
    dict(name="TB BASS", cat="ACID", e=[1, -12, 58, 75, 46, 92, 8, 0], env=[0, 68, 15, 30], fenv=44, mono=1,
         fx=(5, 0, 30, 10), pat=1),   # CLASSIC
    dict(name="WAREHOUSE", cat="ACID", e=[0, 0, 72, 95, 40, 105, 15, 0], env=[0, 82, 15, 30], fenv=55, mono=1,
         fx=(15, 0, 45, 20), pat=1),   # CLASSIC
    dict(name="SQUELCHER", cat="ACID", e=[0, 0, 50, 100, 34, 118, 0, 0], env=[0, 60, 15, 30], fenv=60, mono=1,
         fx=(5, 0, 45, 15), pat=1),   # SQUELCH
    dict(name="RUBBER", cat="ACID", e=[1, 0, 40, 110, 38, 112, 0, 0], env=[0, 50, 15, 30], fenv=58, mono=1,
         fx=(5, 0, 40, 15), pat=1),   # SQUELCH
    dict(name="WOW", cat="ACID", e=[0, 0, 80, 90, 28, 115, 10, 0], env=[0, 90, 15, 30], fenv=63, mono=1,
         fx=(0, 0, 50, 25), pat=1),   # SQUELCH
    dict(name="RESO FREAK", cat="ACID", e=[0, 0, 45, 85, 42, 122, 0, 0], env=[0, 55, 15, 30], fenv=56, mono=1,
         fx=(10, 0, 40, 10), pat=1),   # SQUELCH
    dict(name="LIQUID", cat="ACID", e=[1, 0, 70, 95, 36, 108, 0, 0], env=[0, 80, 15, 30], fenv=52, mono=1,
         fx=(0, 15, 45, 35), pat=1),   # SQUELCH
    dict(name="BUBBLES", cat="ACID", e=[0, 7, 38, 105, 48, 116, 5, 0], env=[0, 48, 15, 30], fenv=60, mono=1,
         fx=(5, 0, 55, 30), pat=1),   # SQUELCH
    dict(name="ZAP SQUELCH", cat="ACID", e=[1, 12, 28, 120, 60, 110, 0, 0], env=[0, 38, 15, 30], fenv=63, mono=1,
         fx=(10, 0, 50, 20), pat=1),   # SQUELCH
    dict(name="SCOOP", cat="ACID", e=[0, 0, 62, 100, 32, 120, 12, 0], env=[0, 72, 15, 30], fenv=62, mono=1,
         fx=(5, 0, 40, 15), pat=1),   # SQUELCH
    dict(name="DIRTY 303", cat="ACID", e=[0, 0, 55, 85, 52, 95, 75, 0], env=[0, 65, 15, 30], fenv=48, mono=1,
         fx=(30, 0, 35, 10), pat=1),   # DISTORTED
    dict(name="CRUSHER", cat="ACID", e=[1, 0, 48, 95, 46, 100, 100, 0], env=[0, 58, 15, 30], fenv=55, mono=1,
         fx=(45, 0, 30, 10), pat=1),   # DISTORTED
    dict(name="FUZZ BOX", cat="ACID", e=[0, 0, 60, 80, 58, 85, 90, 0], env=[0, 70, 15, 30], fenv=42, mono=1,
         fx=(35, 0, 30, 15), pat=1),   # DISTORTED
    dict(name="OVERDRIVE", cat="ACID", e=[1, 0, 52, 90, 50, 90, 110, 0], env=[0, 62, 15, 30], fenv=50, mono=1,
         fx=(40, 0, 35, 10), pat=1),   # DISTORTED
    dict(name="BURNT", cat="ACID", e=[0, 0, 66, 100, 44, 105, 85, 0], env=[0, 76, 15, 30], fenv=56, mono=1,
         fx=(50, 0, 40, 15), pat=1),   # DISTORTED
    dict(name="GRIT", cat="ACID", e=[1, -12, 45, 75, 54, 88, 95, 0], env=[0, 55, 15, 30], fenv=46, mono=1,
         fx=(30, 0, 25, 10), pat=1),   # DISTORTED
    dict(name="SCREAM", cat="ACID", e=[0, 12, 40, 110, 62, 112, 120, 0], env=[0, 50, 15, 30], fenv=60, mono=1,
         fx=(55, 0, 45, 20), pat=1),   # DISTORTED
    dict(name="RUST", cat="ACID", e=[0, 0, 70, 70, 40, 96, 80, 0], env=[0, 80, 15, 30], fenv=50, mono=1,
         fx=(40, 10, 35, 25), pat=1),   # DISTORTED
    dict(name="DEEP ACID", cat="ACID", e=[1, -12, 66, 70, 30, 90, 0, 0], env=[0, 76, 15, 30], fenv=44, mono=1,
         fx=(0, 0, 30, 20), pat=1),   # DEEP
    dict(name="SUB 303", cat="ACID", e=[0, -24, 70, 60, 26, 80, 10, 0], env=[0, 80, 15, 30], fenv=38, mono=1,
         fx=(5, 0, 20, 10), pat=1),   # DEEP
    dict(name="DARK TRAX", cat="ACID", e=[0, -12, 75, 85, 28, 100, 20, 0], env=[0, 85, 15, 30], fenv=50, mono=1,
         fx=(10, 0, 35, 30), pat=1),   # DEEP
    dict(name="MINIMAL", cat="ACID", e=[1, -12, 40, 65, 34, 96, 0, 0], env=[0, 50, 15, 30], fenv=36, mono=1,
         fx=(0, 0, 25, 15), pat=1),   # DEEP
    dict(name="ROLLING", cat="ACID", e=[0, -12, 60, 80, 32, 95, 15, 0], env=[0, 70, 15, 30], fenv=46, mono=1,
         fx=(10, 0, 30, 15), pat=1),   # DEEP
    dict(name="DUB ACID", cat="ACID", e=[1, -24, 78, 75, 24, 88, 5, 0], env=[0, 88, 15, 30], fenv=42, mono=1,
         fx=(0, 10, 40, 40), pat=1),   # DEEP
    dict(name="LOW SQUELCH", cat="ACID", e=[0, -12, 58, 100, 22, 114, 10, 0], env=[0, 68, 15, 30], fenv=58, mono=1,
         fx=(10, 0, 35, 20), pat=1),   # DEEP
    dict(name="UNDERTOW", cat="ACID", e=[1, -24, 82, 90, 20, 102, 25, 0], env=[0, 92, 15, 30], fenv=52, mono=1,
         fx=(15, 0, 35, 30), pat=1),   # DEEP
]
