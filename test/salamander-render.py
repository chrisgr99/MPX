"""A REFERENCE RENDER: the same notes, played on the Salamander samples instead of the model.

WHY BOTHER when the two can be compared by moving a cable in Rack. Because a file pair can be
measured, and the measurement is what says WHERE the model is wrong rather than that it is. A
listener can hear that the bass is unpleasant; a spectrum says whether it is too bright, too dull,
too long or too short.

IT PLAYS THE SAME NOTES, from the same reader, with the same lengths. The one difference is meant
to be the instrument.

AND IT HONOURS THE LENGTHS, which the real sampler module does not — it ignores the duration on
the cable and relies on its samples ending and its thirty-two voices stealing from one another.
Rendering it the way the module behaves would compare note handling as well as tone, and tone is
the question. So both sides here stop their notes at the written length, with a short release.
"""
import json, os, struct, sys, wave, math

DIR = os.path.expanduser("~/Library/Application Support/Rack2/DreamerMPX/piano")
SR = 48000.0
# The module's DAMPING knob at its default puts the release at about an eighth of a second.
RELEASE = 0.13
# The set's own velocity tracking, from its SFZ, as the module applies it.
VELOCITY_TRACK = 0.73


def read_wav(path):
    with wave.open(path, "rb") as w:
        n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if width != 2:
        raise RuntimeError("expected 16-bit: " + path)
    vals = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if ch == 2:
        left = vals[0::2]
        right = vals[1::2]
    else:
        left = right = vals
    return left, right, rate


def main():
    notes_file, out_path, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
    with open(os.path.join(DIR, "map.json")) as f:
        regions = [r for r in json.load(f)["regions"] if r.get("part") == "strings"]

    def pick(key, vel):
        best, score = None, None
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

    notes = []
    for line in open(notes_file):
        parts = line.split()
        if len(parts) != 4:
            continue
        at, midi, level, lasts = float(parts[0]), int(parts[1]), float(parts[2]), float(parts[3])
        notes.append((at, midi, level, lasts))

    total = int((seconds + 4.0) * SR)
    outL = [0.0] * total
    outR = [0.0] * total
    cache = {}
    played = 0
    for at, midi, level, lasts in notes:
        vel = max(1, min(127, int(round(1.0 + 126.0 * level))))
        r = pick(midi, vel)
        if r is None:
            continue
        path = os.path.join(DIR, "samples", r["file"])
        if path not in cache:
            if not os.path.exists(path):
                continue
            cache[path] = read_wav(path)
        left, right, rate = cache[path]
        # Pitched by stepping through the recording faster or slower, and by the engine's rate
        # against the rate it was recorded at: the same arithmetic the module does.
        step = (2.0 ** ((midi - r.get("key", 60)) / 12.0)) * (rate / SR)
        x = vel / 127.0
        amp = ((1.0 - VELOCITY_TRACK) + VELOCITY_TRACK * x * x) * r.get("gain", 1.0)
        start = int(at * SR)
        stop = int((at + lasts) * SR)
        fade = int(RELEASE * SR)
        pos = 0.0
        i = start
        n = len(left)
        while i < total:
            p = int(pos)
            if p + 1 >= n:
                break
            f = pos - p
            l = (left[p] * (1.0 - f) + left[p + 1] * f) / 32768.0
            rr = (right[p] * (1.0 - f) + right[p + 1] * f) / 32768.0
            g = amp
            if i >= stop:
                k = i - stop
                if k >= fade:
                    break
                g *= 1.0 - k / float(fade)
            outL[i] += l * g
            outR[i] += rr * g
            pos += step
            i += 1
        played += 1

    peak = max(max(abs(v) for v in outL), max(abs(v) for v in outR)) or 1.0
    print("   %d notes played, %d recordings loaded, peak %.3f" % (played, len(cache), peak))
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(SR))
        frames = bytearray()
        for i in range(total):
            for v in (outL[i], outR[i]):
                s = int(max(-1.0, min(1.0, v)) * 32767)
                frames += struct.pack("<h", s)
        w.writeframes(bytes(frames))
    print("   wrote", out_path)


main()
