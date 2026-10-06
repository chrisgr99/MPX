"""HOW CLOSE IS THE MODEL TO A RECORDED PIANO, note by note, on terms that can be acted on.

Not a score of realism — there is no such number — but the four things that are measurable and that
a listener actually reacts to: the balance of the spectrum, how the brightness changes with how hard
the note is struck, how long it rings, and how quickly it speaks.

Every figure is the MODEL MINUS THE RECORDING, so a positive number means the model has more of
that thing. Spectra are normalised to each one's own strongest band, because the recording is a
piano in a room through microphones and the model is a soundboard's surface velocity: their
absolute levels are not comparable and their shapes are.
"""
import cmath, json, math, os, struct, subprocess, sys, wave

DIR = os.path.expanduser("~/Library/Application Support/Rack2/DreamerMPX/piano")
MPX = os.path.expanduser("~/ProgrammingProjects/MPX")
SR = 48000.0
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def name(m):
    return "%s%d" % (NAMES[m % 12], m // 12 - 1)


def read(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    v = struct.unpack("<%dh" % (len(raw) // 2), raw)
    mono = [v[i * ch] / 32768.0 for i in range(n)] if ch > 1 else [x / 32768.0 for x in v]
    return mono, sr


def fft(a):
    n = len(a)
    if n == 1:
        return a
    ev, od = fft(a[0::2]), fft(a[1::2])
    out = [0] * n
    for k in range(n // 2):
        t = cmath.exp(-2j * math.pi * k / n) * od[k]
        out[k] = ev[k] + t
        out[k + n // 2] = ev[k] - t
    return out


def spectrum(x, sr, at, n=8192):
    seg = list(x[at:at + n])
    if len(seg) < n:
        seg += [0.0] * (n - len(seg))
    w = [0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) for i in range(n)]
    return fft([complex(seg[i] * w[i], 0) for i in range(n)]), sr / n


def bands(x, sr, at):
    sp, binhz = spectrum(x, sr, at)
    b = [0.0] * 10
    for k in range(1, len(sp) // 2):
        f = k * binhz
        if f < 31 or f > 20000:
            continue
        o = int(math.floor(math.log2(f / 31.25)))
        if 0 <= o < 10:
            b[o] += abs(sp[k]) ** 2
    return b


def centroid(x, sr, at):
    sp, binhz = spectrum(x, sr, at)
    num = den = 0.0
    for k in range(1, len(sp) // 2):
        f = k * binhz
        if f > 16000:
            break
        m = abs(sp[k])
        num += f * m
        den += m
    return num / den if den else 0.0


def level_db(x, at, n):
    seg = x[at:at + n]
    if not seg:
        return -200.0
    return 10 * math.log10(sum(v * v for v in seg) / len(seg) + 1e-20)


def t60(x, sr, f0):
    """How long the fundamental takes to fall sixty decibels, from the rate it starts out at."""
    def tone(at, take=8192):
        if at + take >= len(x):
            return None
        re = im = 0.0
        for i in range(take):
            w = 0.5 - 0.5 * math.cos(2 * math.pi * i / (take - 1))
            a = 2 * math.pi * f0 * i / sr
            re += x[at + i] * w * math.cos(a)
            im -= x[at + i] * w * math.sin(a)
        return 20 * math.log10(math.hypot(re, im) / take + 1e-30)
    a = tone(int(0.15 * sr))
    b = tone(int(1.5 * sr))
    if a is None or b is None or a - b < 0.5:
        return None
    return 60.0 * 1.35 / (a - b)


def attack_ms(x, sr):
    peak = max(abs(v) for v in x[:int(0.2 * sr)]) or 1e-9
    for i, v in enumerate(x):
        if abs(v) > 0.5 * peak:
            return 1000.0 * i / sr
    return 0.0


def sample_for(key, vel, regions):
    best = score = None
    for r in regions:
        if not (r.get("lokey", 0) <= key <= r.get("hikey", 127)):
            continue
        lo, hi = r.get("lovel", 1), r.get("hivel", 127)
        if lo <= vel <= hi:
            return r
        d = min(abs(vel - lo), abs(vel - hi))
        if score is None or d < score:
            best, score = r, d
    return best


def resample_to(path, key, region):
    """The recording for this key, pitched to it as the module does, as a mono list."""
    left, sr = read(path)
    step = (2.0 ** ((key - region.get("key", 60)) / 12.0)) * (sr / SR)
    out = []
    pos = 0.0
    while True:
        p = int(pos)
        if p + 1 >= len(left):
            break
        f = pos - p
        out.append(left[p] * (1 - f) + left[p + 1] * f)
        pos += step
        if len(out) > int(4.0 * SR):
            break
    return out


def main():
    with open(os.path.join(DIR, "map.json")) as f:
        regions = [r for r in json.load(f)["regions"] if r.get("part") == "strings"]

    keys = [33, 45, 52, 60, 67, 76, 84, 93]
    levels = [(0.30, "soft"), (0.65, "firm"), (0.95, "hard")]

    print("MODEL MINUS RECORDING, in decibels per octave band. Positive means the model has more.")
    print("%-6s %-6s %s" % ("note", "blow",
          "   44    88   176   352   704  1408  2816  5632 11264"))
    tilt_rows = []
    for key in keys:
        for lv, label in levels:
            vel = max(1, min(127, int(round(1 + 126 * lv))))
            r = sample_for(key, vel, regions)
            if r is None:
                continue
            path = os.path.join(DIR, "samples", r["file"])
            if not os.path.exists(path):
                continue
            ref = resample_to(path, key, r)
            mine_path = "/tmp/model-note.wav"
            subprocess.run(["make", "-s", "pianonote",
                            "ARGS=%d %.2f 4 %s" % (key, lv, mine_path)],
                           cwd=MPX, capture_output=True)
            mine, _ = read(mine_path)
            at = int(0.02 * SR)
            a, b = bands(mine, SR, at), bands(ref, SR, at)
            ma, mb = max(a) or 1e-30, max(b) or 1e-30
            row = []
            for i in range(9):
                da = 10 * math.log10(a[i] / ma + 1e-30)
                db = 10 * math.log10(b[i] / mb + 1e-30)
                row.append(da - db)
            print("%-6s %-6s %s" % (name(key), label,
                  " ".join("%5.0f" % v for v in row)))
            tilt_rows.append((key, label, row, mine, ref))
    print()
    print("AND THE OTHER THREE THINGS, model against recording.")
    print("%-6s %-6s %12s %12s %10s %10s %10s %10s" % ("note", "blow", "centroid", "centroid",
          "T60", "T60", "attack", "attack"))
    print("%-6s %-6s %12s %12s %10s %10s %10s %10s" % ("", "", "model", "recorded",
          "model", "recorded", "model", "recorded"))
    for key, label, row, mine, ref in tilt_rows:
        f0 = 440.0 * 2 ** ((key - 69) / 12.0)
        cm, cr = centroid(mine, SR, int(0.02 * SR)), centroid(ref, SR, int(0.02 * SR))
        tm, tr = t60(mine, SR, f0), t60(ref, SR, f0)
        print("%-6s %-6s %10.0f Hz %9.0f Hz %9s %10s %8.1f ms %7.1f ms" % (
            name(key), label, cm, cr,
            ("%.1f" % tm) if tm else "-", ("%.1f" % tr) if tr else "-",
            attack_ms(mine, SR), attack_ms(ref, SR)))


main()
