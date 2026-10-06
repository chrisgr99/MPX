"""REPRODUCES mpxPiano's RELEASE ENGINE OUTSIDE RACK, so that a claim about it can be checked.

WHY THIS EXISTS. The module's voice engine lives inside a Rack Module, where nothing but Rack can
reach it — and twice in a row a change to its damping was reported as working when it was not. A
thing that cannot be run cannot be verified, and "it compiled" is not evidence. So the arithmetic
is written out here a second time, from the module's own source, and driven with the same notes.

IT IS A COPY AND THAT IS THE WEAKNESS OF IT. If the two drift apart this file lies. It is worth
having anyway, because it turns a question that took two rounds of listening into one that takes a
second — and when the two DISAGREE, that disagreement is itself the finding.

What it mirrors, from mpxPiano.cpp:
  the string voice   amp = ((1-vt) + vt x^2), vt = 0.73, x = velocity over 127
  its release        decay per sample = exp(-6.9 / (seconds * rate)),
                     seconds = 0.02 * 40^DAMPING / max(0.05, 1 - sustain)
  the resonance      gain = RELEASE * amp * (how loud the note still is), dying in 3 * seconds
  the key-up noise   gain = HAMMER * amp, played out as recorded

    python3 test/salamander-release.py <notes> <out.wav> <seconds> <damping 0..1>
"""
import json, math, os, struct, sys, wave

DIR = os.path.expanduser("~/Library/Application Support/Rack2/DreamerMPX/piano")
SR = 48000.0
VT = 0.73
RELEASE_KNOB = 1.0
HAMMER_KNOB = 1.0
# The levels the SFZ's own groups ask for, which map.json dropped. See partGain in mpxPiano.cpp.
HAMMER_PART = 0.0141      # -37 dB
RESONANCE_PART = 0.631    # -4 dB
SUSTAIN = 0.0


def read_wav(path):
    with wave.open(path, "rb") as w:
        n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    v = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left = v[0::2] if ch == 2 else v
    return left, rate


def main():
    notes_file, out_path, seconds, damping = (sys.argv[1], sys.argv[2], float(sys.argv[3]),
                                              float(sys.argv[4]))
    with open(os.path.join(DIR, "map.json")) as f:
        regions = json.load(f)["regions"]

    def pick(part, key, vel):
        best = score = None
        for r in regions:
            if r.get("part") != part:
                continue
            if not (r.get("lokey", 0) <= key <= r.get("hikey", 127)):
                continue
            lo, hi = r.get("lovel", 1), r.get("hivel", 127)
            if lo <= vel <= hi:
                return r
            d = min(abs(vel - lo), abs(vel - hi))
            if score is None or d < score:
                best, score = r, d
        return best

    # The module's own arithmetic for how fast a damper falls.
    fall = 0.02 * (40.0 ** damping) / max(0.05, 1.0 - SUSTAIN)
    string_decay = math.exp(-6.9 / (fall * SR))
    res_decay = math.exp(-6.9 / (3.0 * fall * SR))

    notes = []
    for line in open(notes_file):
        f = line.split()
        if len(f) == 4:
            notes.append((float(f[0]), int(f[1]), float(f[2]), float(f[3])))

    total = int((seconds + 4.0) * SR)
    out = [0.0] * total
    cache = {}

    def play(region, key, start, gain, decay_from, pitched):
        """One recording, from `start`, at `gain`, decaying after `decay_from` samples."""
        path = os.path.join(DIR, "samples", region["file"])
        if path not in cache:
            if not os.path.exists(path):
                return
            cache[path] = read_wav(path)
        left, rate = cache[path]
        ref = region.get("key", 60)
        step = (2.0 ** ((key - ref) / 12.0)) * (rate / SR) if pitched else (rate / SR)
        pos = 0.0
        env = 1.0
        i = start
        while i < total:
            p = int(pos)
            if p + 1 >= len(left):
                break
            fr = pos - p
            out[i] += (left[p] * (1 - fr) + left[p + 1] * fr) / 32768.0 * gain * env
            if decay_from is not None and i >= decay_from:
                env *= decay_from_rate
                if env < 1e-5:
                    break
            pos += step
            i += 1

    for at, midi, level, lasts in notes:
        vel = max(1, min(127, int(round(1 + 126 * level))))
        r = pick("strings", midi, vel)
        if r is None:
            continue
        x = vel / 127.0
        amp = ((1 - VT) + VT * x * x) * r.get("gain", 1.0)
        start = int(at * SR)
        release_at = int((at + lasts) * SR)

        # The string, decaying from the moment the key comes up.
        decay_from_rate = string_decay
        play(r, midi, start, amp, release_at, True)
        # Its release resonance, which the damper also stops.
        res = pick("resonance", midi, vel)
        if res is not None:
            decay_from_rate = res_decay
            play(res, midi, release_at, RELEASE_KNOB * amp * 0.6 * RESONANCE_PART, release_at, True)
        # The key coming up, which is a mechanical noise with no damper to answer to.
        ham = pick("hammer", midi, vel)
        if ham is not None:
            decay_from_rate = 1.0
            play(ham, midi, release_at, HAMMER_KNOB * amp * HAMMER_PART, None, False)

    peak = max(abs(v) for v in out) or 1.0
    print("   damping %.2f -> string falls 60 dB in %.0f ms, resonance in %.0f ms; peak %.3f"
          % (damping, 1000 * fall, 3000 * fall, peak))
    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(SR))
        fr = bytearray()
        for v in out:
            fr += struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767))
        w.writeframes(bytes(fr))
    print("   wrote", out_path)


# `decay_from_rate` is set just before each call; declared here so `play` can see it.
decay_from_rate = 1.0
main()
