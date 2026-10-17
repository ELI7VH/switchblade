/* SPDX-License-Identifier: GPL-3.0-only */
/* Preset audition renderer (VOID FOUR Task 7.1): every factory preset of the four machines through the real mix path
 * (mix_block: the part, its sends, the buses, the master) into build/audition/<MACHINE>/<nn>_<NAME>.wav, 6 s, 44.1 kHz,
 * 16-bit stereo, plus summary.json (the figures and verdicts; tools/audition_index.py turns it into index.html).
 *   build/host/audition OUTDIR       render everything; one line per preset that fails a check; exit 1 if any does
 *   build/host/audition --selftest   the checks against synthetic signals (run by tests/run_tests.sh)
 *
 * What is played (by the preset's category, category.c):
 *   ACID                       PATTERNS[0] "ACID" (engines.c: 16 steps, 4 accents, 3 slides) on the sequencer, twice (2 bars)
 *   BASS                       PATTERNS[1] "OFFBEAT", twice
 *   LEAD                       PATTERNS[3] "LEAD" (with ties and slides), twice
 *   KEYS BRASS STRINGS ATMOS   a 4-note chord (C4 E4 G4 B4) held 3 s, then released
 * 120 BPM, so 2 bars = 4 s; the rest of the 6 s is the release / FX tail. DRIFT is mono (DRIFT_POLY 1): a chord on it
 * sounds one note, and is rendered anyway.
 * The preset is loaded by hostsim.c's host_preset (the path apply_preset_to takes), each in a forked child so every
 * render starts from the same fresh state (byte-identical WAVs run to run).
 *
 * The checks (au_measure / au_check), all on the mix_block output before the WAV's clipping to 16 bits:
 *   gate      the sounding part = the 10 ms blocks (441 frames) whose own RMS is above -60 dBFS; the silent tail and
 *             the gaps do not count
 *   peak      max |sample| of either channel, dBFS re 32768; fails above -1
 *   RMS       over the sounding part, both channels pooled, dBFS re 32768; fails outside -24..-10
 *   DC        max over the channels of |mean| over the sounding part, % of 32768; fails above 1
 *   silent    less than 50 ms of sounding part (RMS and DC are then not judged) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>
#include "../firmware/src/category.c"

#define AU_FRAMES (6u * FS)
#define AU_BLK 441u
enum { AF_PEAK = 1, AF_RMS_LO = 2, AF_RMS_HI = 4, AF_DC = 8, AF_SILENT = 16 };
typedef struct { double peak_db, rms_db, dc_pct, sound_ms; } meas_t;

/* x is interleaved L R, n frames */
static void au_measure(const int32_t *x, uint32_t n, meas_t *m)
{
    const double FSC = 32768.0, GATE = FSC * 0.001;           /* -60 dBFS, as an RMS amplitude */
    double sq = 0, sum[2] = {0, 0};
    uint64_t cnt = 0, blocks = 0;
    int32_t peak = 0;
    uint32_t b, i, c;
    for (i = 0; i < 2u * n; i++) {
        int32_t a = abs(x[i]);
        peak = a > peak ? a : peak;
    }
    for (b = 0; b + AU_BLK <= n; b += AU_BLK) {
        double bs = 0;
        for (i = 0; i < 2u * AU_BLK; i++)
            bs += (double)x[2u * b + i] * x[2u * b + i];
        if (sqrt(bs / (2.0 * AU_BLK)) <= GATE)
            continue;
        blocks++;
        sq += bs;
        cnt += 2u * AU_BLK;
        for (i = 0; i < AU_BLK; i++)
            for (c = 0; c < 2u; c++)
                sum[c] += x[2u * (b + i) + c];
    }
    m->peak_db = peak ? 20.0 * log10(peak / FSC) : -120.0;
    m->sound_ms = (double)blocks * AU_BLK * 1000.0 / FS;
    m->rms_db = cnt ? 10.0 * log10(sq / cnt / (FSC * FSC)) : -120.0;
    m->dc_pct = cnt ? 100.0 * fmax(fabs(sum[0]), fabs(sum[1])) / (blocks * AU_BLK) / FSC : 0.0;
}
static uint32_t au_check(const meas_t *m)
{
    uint32_t f = 0;
    if (m->peak_db > -1.0)
        f |= AF_PEAK;
    if (m->sound_ms < 50.0)
        return f | AF_SILENT;
    if (m->rms_db < -24.0)
        f |= AF_RMS_LO;
    if (m->rms_db > -10.0)
        f |= AF_RMS_HI;
    if (m->dc_pct > 1.0)
        f |= AF_DC;
    return f;
}

/* ------------------------------------------------------------------ selftest */
static uint32_t lcg = 12345u;
static double lcg_unit(void)             /* -1..1 */
{
    lcg = lcg * 1664525u + 1013904223u;
    return (double)(lcg >> 8) / 8388608.0 - 1.0;
}
static void st_sine(int32_t *x, uint32_t n, double amp, uint32_t sound_frames)   /* then zeros */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        x[2 * i] = x[2 * i + 1] = i < sound_frames ? (int32_t)(amp * sin(2.0 * M_PI * 440.0 * i / FS)) : 0;
}
static int selftest(void)
{
    static int32_t x[2 * AU_FRAMES];
    uint32_t i, bad = 0, k, got;
    static const struct { const char *name; uint32_t want; } T[] = {
        {"good sine, amp 8000 (-15 dBFS RMS, -12 dBFS peak)", 0},
        {"too-hot sine, amp 32000", AF_PEAK | AF_RMS_HI},
        {"good sine plus one 30000 spike in one channel", AF_PEAK},
        {"too-quiet sine, amp 1000 (-33 dBFS RMS)", AF_RMS_LO},
        {"silent buffer", AF_SILENT},
        {"-70 dBFS sine (below the gate)", AF_SILENT},
        {"noise on a +5 % offset", AF_DC},
        {"noise on a +0.5 % offset (under the limit)", 0},
        {"-21 dBFS sine for 1.5 s then silence (the tail must not count)", 0},
        {"sine amp 29000 (peak -1.06 dBFS, just under; RMS -4 dBFS)", AF_RMS_HI},
        {"sine amp 30000 (peak -0.77 dBFS)", AF_PEAK | AF_RMS_HI},
        {"sine amp 2000 (RMS -27 dBFS)", AF_RMS_LO},
        {"sine amp 12000 (RMS -11.7 dBFS)", 0},
    };
    for (k = 0; k < sizeof T / sizeof T[0]; k++) {
        meas_t m;
        memset(x, 0, sizeof x);
        switch (k) {
        case 0: st_sine(x, AU_FRAMES, 8000, AU_FRAMES); break;
        case 1: st_sine(x, AU_FRAMES, 32000, AU_FRAMES); break;
        case 2: st_sine(x, AU_FRAMES, 8000, AU_FRAMES); x[2 * 100000] = 30000; break;
        case 3: st_sine(x, AU_FRAMES, 1000, AU_FRAMES); break;
        case 4: break;
        case 5: st_sine(x, AU_FRAMES, 10, AU_FRAMES); break;
        case 6: case 7:
            for (i = 0; i < AU_FRAMES; i++)
                x[2 * i] = x[2 * i + 1] = (int32_t)(4000.0 * lcg_unit() + (k == 6u ? 1638.0 : 164.0));
            break;
        case 8: st_sine(x, AU_FRAMES, 4000, 3u * FS / 2u); break;
        case 9: st_sine(x, AU_FRAMES, 29000, AU_FRAMES); break;
        case 10: st_sine(x, AU_FRAMES, 30000, AU_FRAMES); break;
        case 11: st_sine(x, AU_FRAMES, 2000, AU_FRAMES); break;
        case 12: st_sine(x, AU_FRAMES, 12000, AU_FRAMES); break;
        }
        au_measure(x, AU_FRAMES, &m);
        got = au_check(&m);
        printf("selftest: %-66s peak %7.2f rms %7.2f dc %6.3f%% snd %6.0f ms -> flags %02x (want %02x) %s\n", T[k].name,
               m.peak_db, m.rms_db, m.dc_pct, m.sound_ms, got, T[k].want, got == T[k].want ? "ok" : "FAIL");
        bad += got != T[k].want;
    }
    printf(bad ? "selftest: %u FAILED\n" : "selftest: all passed\n", bad);
    return bad != 0;
}

/* ------------------------------------------------------------------ render */
enum { PH_PATTERN, PH_CHORD };
typedef struct {
    meas_t m;
    uint32_t flags;
    uint32_t ok;                          /* the child finished */
} result_t;

static void au_name(char *d, size_t n, const char *s)            /* file name part: letters and digits, the rest "_" */
{
    size_t i = 0;
    for (; *s && i + 1 < n; s++)
        d[i++] = (char)((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ? *s : '_');
    d[i] = 0;
}

/* the child: one preset, from the process's fresh state; writes the WAV, returns the figures */
static int render_one(uint32_t e, uint32_t pi, uint32_t cat, const char *path, result_t *r)
{
    static const uint8_t CH[4] = {60, 64, 67, 71};
    static int32_t x[2 * AU_FRAMES + 2 * CTL];
    track_t *t = &trk[0];
    uint32_t f, i, pat = cat == CAT_ACID ? 0u : cat == CAT_BASS ? 1u : cat == CAT_LEAD ? 3u : ~0u, stop = 0;
    FILE *w;
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t, e, pi);
    if (pat != ~0u) {                                  /* PATTERNS[pat] in steps 1..16, as ui.c load_pat16 */
        for (i = 0; i < 16u; i++) {
            uint8_t n = PATTERNS[pat].note[i], fl = PATTERNS[pat].flags[i];
            put_step(t, i, n ? 1u : 0u, &n, (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST,
                     n ? (uint32_t)(fl & (SF_ACCENT | SF_SLIDE)) : 0u);
            t->step[i].vel = n ? 96 : 0;
        }
        t->p[P_SLEN] = 16;
        stop = 2u * 16u * div_samples((uint32_t)t->p[P_SDIV]);      /* two passes = 2 bars */
        transport_req = 1;
    }
    for (f = 0; f < AU_FRAMES; f += CTL) {
        int32_t o[2 * CTL];
        if (pat == ~0u) {
            if (f == 0)
                for (i = 0; i < 4u; i++)
                    trk_note_on(t, CH[i], 100);
            if (f == 3u * FS / CTL * CTL)
                for (i = 0; i < 4u; i++)
                    trk_note_off(t, CH[i]);
        } else if (f >= stop && stop) {
            transport_req = 2;                         /* the transport stops: its notes are released */
            stop = 0;
        }
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            x[2u * f + i] = o[i];
    }
    au_measure(x, AU_FRAMES, &r->m);
    r->flags = au_check(&r->m);
    if (!(w = fopen(path, "wb")))
        return 1;
    wav_hdr(w, AU_FRAMES);
    for (f = 0; f < AU_FRAMES; f++)
        wav_put(w, x[2u * f], x[2u * f + 1]);
    fclose(w);
    r->ok = 1;
    return 0;
}

static const char *const FLAG_NAME[5] = {"PEAK", "RMS_LOW", "RMS_HIGH", "DC", "SILENT"};
static void json_str(FILE *j, const char *s)
{
    fputc('"', j);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', j);
        fputc(*s, j);
    }
    fputc('"', j);
}

static int render_all(const char *out)
{
    char dir[512], path[600], rel[256], fn[64], jpath[600];
    uint32_t e, pi, total = 0, nfail[8] = {0}, anyfail = 0, k;
    FILE *j;
    snprintf(jpath, sizeof jpath, "%s/summary.json", out);
    mkdir(out, 0777);
    if (!(j = fopen(jpath, "w"))) {
        fprintf(stderr, "audition: cannot write %s\n", jpath);
        return 2;
    }
    fprintf(j, "{\"sample_rate\":%u,\"seconds\":6,\"limits\":{\"peak_db_max\":-1,\"rms_db_min\":-24,\"rms_db_max\":-10,"
               "\"dc_pct_max\":1,\"sound_ms_min\":50},\"presets\":[\n", FS);
    for (e = 0; e < NENGINES; e++) {
        snprintf(dir, sizeof dir, "%s/%s", out, ENGINES[e]->name);
        mkdir(dir, 0777);
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            uint32_t cat = preset_cat(e, pi);
            const char *pname = ENGINES[e]->presets[pi].name;
            result_t r;
            int fd[2], st = 0;
            pid_t pid;
            au_name(fn, sizeof fn, pname);
            snprintf(rel, sizeof rel, "%s/%02u_%s.wav", ENGINES[e]->name, pi + 1u, fn);
            snprintf(path, sizeof path, "%s/%s", out, rel);
            memset(&r, 0, sizeof r);
            if (pipe(fd))
                return 2;
            fflush(NULL);
            if (!(pid = fork())) {
                result_t c;
                memset(&c, 0, sizeof c);
                render_one(e, pi, cat, path, &c);
                if (write(fd[1], &c, sizeof c) != (ssize_t)sizeof c)
                    _exit(1);
                _exit(0);
            }
            close(fd[1]);
            if (read(fd[0], &r, sizeof r) != (ssize_t)sizeof r)
                r.ok = 0;
            close(fd[0]);
            waitpid(pid, &st, 0);
            if (!r.ok || !WIFEXITED(st) || WEXITSTATUS(st)) {
                fprintf(stderr, "audition: %s %s: render failed\n", ENGINES[e]->name, pname);
                return 2;
            }
            total++;
            if (r.flags) {
                nfail[e]++;
                anyfail = 1;
                printf("FAIL %-5s %02u %-14s %-7s peak %6.2f dBFS  rms %7.2f dBFS  dc %6.3f %%  sounding %5.0f ms  [",
                       ENGINES[e]->name, pi + 1u, pname, CAT_NAME[cat], r.m.peak_db, r.m.rms_db, r.m.dc_pct, r.m.sound_ms);
                for (k = 0; k < 5u; k++)
                    if (r.flags & (1u << k))
                        printf(" %s", FLAG_NAME[k]);
                printf(" ]\n");
            }
            fprintf(j, "%s{\"machine\":", total > 1u ? ",\n" : "");
            json_str(j, ENGINES[e]->name);
            fprintf(j, ",\"index\":%u,\"name\":", pi + 1u);
            json_str(j, pname);
            fprintf(j, ",\"category\":\"%s\",\"phrase\":\"%s\",\"file\":", CAT_NAME[cat],
                    cat == CAT_ACID ? "ACID pattern x2 (accent, slide)" : cat == CAT_BASS ? "OFFBEAT bass x2 (2 bars)"
                    : cat == CAT_LEAD ? "LEAD line x2 (2 bars)" : "4-note chord, 3 s");
            json_str(j, rel);
            fprintf(j, ",\"peak_db\":%.2f,\"rms_db\":%.2f,\"dc_pct\":%.3f,\"sound_ms\":%.0f,\"fail\":[", r.m.peak_db,
                    r.m.rms_db, r.m.dc_pct, r.m.sound_ms);
            for (k = 0, st = 0; k < 5u; k++)
                if (r.flags & (1u << k))
                    fprintf(j, "%s\"%s\"", st++ ? "," : "", FLAG_NAME[k]);
            fprintf(j, "]}");
        }
    }
    fprintf(j, "\n]}\n");
    fclose(j);
    printf("audition: %u presets rendered to %s; failing the checks:", total, out);
    for (e = 0; e < NENGINES; e++)
        printf(" %s %u%s", ENGINES[e]->name, nfail[e], e + 1u < NENGINES ? "," : "\n");
    return anyfail;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest"))
        return selftest();
    if (argc == 2 && argv[1][0] != '-')
        return render_all(argv[1]);
    fprintf(stderr, "usage: audition OUTDIR | --selftest\n");
    return 2;
}
