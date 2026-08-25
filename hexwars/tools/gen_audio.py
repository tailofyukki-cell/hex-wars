# gen_audio.py - HEX WARS 用のBGM/SEを手続き生成（純stdlib）
import wave, struct, math, random, os

SR = 22050
# 出力先はこのスクリプトからの相対（他の tools/*.py と同じ流儀）
BASE = os.path.join(os.path.dirname(__file__), "..")
OUT_SFX = os.path.join(BASE, "assets", "sfx")
OUT_BGM = os.path.join(BASE, "assets", "bgm")
os.makedirs(OUT_SFX, exist_ok=True)
os.makedirs(OUT_BGM, exist_ok=True)
random.seed(42)

def write_wav(path, samples):
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)
    print(os.path.basename(path), len(samples) / SR, "s")

def env(i, n, a=0.01, r=0.3):
    """attack/release envelope 0..1"""
    t = i / n
    at = max(1, int(n * a))
    if i < at:
        return i / at
    rel = 1.0 - max(0.0, (t - (1 - r)) / r)
    return min(1.0, rel)

def square(freq, i, duty=0.5):
    return 1.0 if (freq * i / SR) % 1.0 < duty else -1.0

def tri(freq, i):
    p = (freq * i / SR) % 1.0
    return 4 * abs(p - 0.5) - 1

def noise():
    return random.uniform(-1, 1)

# ---------------- SE ----------------
def se_cursor():
    n = int(SR * 0.04)
    return [0.18 * square(2100, i) * env(i, n, 0.05, 0.6) for i in range(n)]

def se_ok():
    out = []
    for f, d in [(660, 0.06), (990, 0.09)]:
        n = int(SR * d)
        out += [0.22 * square(f, i) * env(i, n, 0.02, 0.5) for i in range(n)]
    return out

def se_cancel():
    out = []
    for f, d in [(880, 0.06), (440, 0.1)]:
        n = int(SR * d)
        out += [0.2 * square(f, i) * env(i, n, 0.02, 0.5) for i in range(n)]
    return out

def se_move_foot():
    out = []
    for k in range(3):
        n = int(SR * 0.05)
        out += [0.16 * noise() * env(i, n, 0.05, 0.8) for i in range(n)]
        out += [0.0] * int(SR * 0.03)
    return out

def se_move_vehicle():
    n = int(SR * 0.28)
    return [(0.14 * square(70 + 25 * math.sin(i / SR * 9), i) + 0.07 * noise())
            * env(i, n, 0.1, 0.4) for i in range(n)]

def se_move_air():
    n = int(SR * 0.32)
    out = []
    lp = 0.0
    for i in range(n):
        lp = lp * 0.92 + noise() * 0.08
        f = 0.3 + 0.7 * i / n
        out.append(0.5 * lp * f * env(i, n, 0.2, 0.5))
    return out

def se_shot():
    n = int(SR * 0.1)
    out = []
    lp = 0.0
    for i in range(n):
        lp = lp * 0.6 + noise() * 0.4
        out.append(0.5 * lp * (1 - i / n) ** 1.5)
    return out

def se_explosion():
    n = int(SR * 0.5)
    out = []
    lp = 0.0
    for i in range(n):
        lp = lp * 0.9 + noise() * 0.1
        out.append(0.85 * lp * (1 - i / n) ** 1.2)
    return out

def se_capture():
    out = []
    for f in [523, 659, 784, 1047]:
        n = int(SR * 0.09)
        out += [0.2 * square(f, i) * env(i, n, 0.02, 0.4) for i in range(n)]
    n = int(SR * 0.2)
    out += [0.22 * square(1047, i) * env(i, n, 0.02, 0.7) for i in range(n)]
    return out

def se_turn():
    n = int(SR * 0.35)
    return [(0.25 * math.sin(2 * math.pi * 440 * i / SR)
             + 0.12 * math.sin(2 * math.pi * 660 * i / SR))
            * env(i, n, 0.01, 0.85) for i in range(n)]

SE_LIST = [("cursor", se_cursor), ("ok", se_ok), ("cancel", se_cancel),
           ("move_foot", se_move_foot), ("move_vehicle", se_move_vehicle),
           ("move_air", se_move_air), ("shot", se_shot),
           ("explosion", se_explosion), ("capture", se_capture),
           ("turn", se_turn)]


def resample(src, rate):
    """rate>1 で高く短く、rate<1 で低く長くなる（再生速度を変える要領）"""
    n = max(1, int(len(src) / rate))
    out = []
    last = len(src) - 1
    for i in range(n):
        pos = i * rate
        j = int(pos)
        if j >= last:
            out.append(src[last])
        else:
            f = pos - j
            out.append(src[j] * (1.0 - f) + src[j + 1] * f)
    return out


def lowpass(src, amt):
    """1次ローパス。角を丸めて太い音にする（amtが小さいほど鈍る）"""
    out = []
    prev = 0.0
    for v in src:
        prev += (v - prev) * amt
        out.append(prev)
    return out


def normalize(src, peak=0.85):
    m = max((abs(v) for v in src), default=0.0)
    if m <= 1e-9:
        return src
    g = peak / m
    return [v * g for v in src]


# 効果音セット: 同じ素材を加工して「音の個性」を変える。
#   set0 = 標準（従来どおり）/ set1 = レトロ（高く短い電子音）
#   set2 = 重厚（低く太い音）
SE_SETS = [
    ("", None),                      # 標準: assets/sfx/ 直下（従来のパスを維持）
    ("set1", ("retro", 1.45)),
    ("set2", ("heavy", 0.72)),
]

for sub, style in SE_SETS:
    outdir = OUT_SFX if not sub else os.path.join(OUT_SFX, sub)
    os.makedirs(outdir, exist_ok=True)
    for name, fn in SE_LIST:
        base = fn()
        if style is None:
            samples = base
        else:
            kind, rate = style
            samples = resample(base, rate)
            if kind == "heavy":
                samples = lowpass(samples, 0.25)
                samples = normalize(samples, 0.8)
            else:
                samples = normalize(samples, 0.7)
        write_wav(os.path.join(outdir, f"se_{name}.wav"), samples)

# ---------------- BGM ----------------
NOTE = {}
names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
for octv in range(2, 7):
    for k, nm in enumerate(names):
        NOTE[f"{nm}{octv}"] = 440.0 * 2 ** ((octv - 4) + (k - 9) / 12.0)
NOTE["-"] = 0.0

def render_song(mel, bass, bpm, vol_m=0.16, vol_b=0.12, duty=0.5):
    """mel/bass: list of (note, beats)"""
    spb = 60.0 / bpm
    def track(seq, wave_fn, vol):
        out = []
        for note, beats in seq:
            n = int(SR * spb * beats)
            f = NOTE[note]
            if f == 0:
                out += [0.0] * n
            else:
                out += [vol * wave_fn(f, i) * env(i, n, 0.01, 0.15) for i in range(n)]
        return out
    m = track(mel, lambda f, i: square(f, i, duty), vol_m)
    b = track(bass, tri, vol_b)
    n = max(len(m), len(b))
    m += [0.0] * (n - len(m))
    b += [0.0] * (n - len(b))
    return [m[i] + b[i] for i in range(n)]

# タイトル: 静かな短調のワルツ風
mel = [("A4",1.5),("C5",0.5),("E5",2),("D5",1.5),("C5",0.5),("B4",2),
       ("C5",1.5),("D5",0.5),("E5",1),("C5",1),("A4",1),("B4",1),("A4",2),("-",2),
       ("E5",1.5),("F5",0.5),("E5",1),("D5",1),("C5",2),("B4",2),
       ("A4",1),("C5",1),("B4",1),("G4",1),("A4",3),("-",1)]
bass = [("A2",2),("E3",2),("F2",2),("C3",2),("G2",2),("D3",2),("A2",2),("E3",2)] * 2 + \
       [("F2",2),("C3",2),("G2",2),("E3",2),("A2",2),("E3",2),("A2",2),("A2",2)]
write_wav(os.path.join(OUT_BGM, "bgm_title.wav"), render_song(mel, bass, 96))

# 戦闘1(西方): 行進曲風
mel = [("C5",0.5),("C5",0.5),("G4",0.5),("C5",0.5),("E5",1),("D5",0.5),("C5",0.5),
       ("D5",0.5),("D5",0.5),("B4",0.5),("D5",0.5),("F5",1),("E5",0.5),("D5",0.5),
       ("E5",0.5),("E5",0.5),("C5",0.5),("E5",0.5),("G5",1),("F5",0.5),("E5",0.5),
       ("D5",0.5),("E5",0.5),("D5",0.5),("B4",0.5),("C5",1.5),("-",0.5)] * 2
bass = [("C3",0.5),("C3",0.5),("G2",0.5),("G2",0.5)] * 4 + \
       [("F2",0.5),("F2",0.5),("C3",0.5),("C3",0.5)] * 2 + \
       [("G2",0.5),("G2",0.5),("C3",0.5),("C3",0.5)] * 2
write_wav(os.path.join(OUT_BGM, "bgm_battle0.wav"),
          render_song(mel, bass * 2, 132, duty=0.25))

# 戦闘2(東方): 短調で重い
mel = [("A4",0.75),("A4",0.25),("C5",1),("B4",0.75),("B4",0.25),("D5",1),
       ("E5",0.75),("E5",0.25),("F5",0.5),("E5",0.5),("D5",0.5),("C5",0.5),
       ("B4",1),("E4",1),("A4",1.5),("-",0.5)] * 2
bass = [("A2",1),("A2",0.5),("A2",0.5),("F2",1),("F2",0.5),("F2",0.5),
        ("G2",1),("G2",0.5),("G2",0.5),("E2",1),("E2",0.5),("E2",0.5)] * 2
write_wav(os.path.join(OUT_BGM, "bgm_battle1.wav"),
          render_song(mel, bass, 112, duty=0.35))

# 戦闘3(疾走): 明るく速い駆け足
mel = [("G4",0.25),("A4",0.25),("B4",0.5),("D5",0.5),("B4",0.5),("G5",1),
       ("F5",0.25),("E5",0.25),("D5",0.5),("B4",0.5),("D5",0.5),("E5",1),
       ("C5",0.25),("D5",0.25),("E5",0.5),("G5",0.5),("E5",0.5),("C6",1),
       ("B5",0.5),("A5",0.5),("G5",0.5),("D5",0.5),("G5",1.5),("-",0.5)] * 2
bass = [("G2",0.5),("D3",0.5),("G2",0.5),("B2",0.5)] * 4 + \
       [("C3",0.5),("G2",0.5),("C3",0.5),("E3",0.5)] * 2 + \
       [("D3",0.5),("A2",0.5),("D3",0.5),("G2",0.5)] * 2
write_wav(os.path.join(OUT_BGM, "bgm_battle2.wav"),
          render_song(mel, bass * 2, 150, duty=0.3))

# 戦闘4(緊迫): 半音でじりじり詰める
mel = [("D5",0.5),("D#5",0.5),("D5",0.5),("C5",0.5),("A#4",1),("A4",1),
       ("A4",0.5),("A#4",0.5),("C5",0.5),("D5",0.5),("D#5",1),("D5",1),
       ("F5",0.5),("E5",0.5),("D#5",0.5),("D5",0.5),("C5",1),("A#4",1),
       ("A4",0.5),("G4",0.5),("A4",1),("D4",1),("D5",1)] * 2
bass = [("D2",1),("D2",0.5),("A2",0.5),("A#2",1),("A2",1),
        ("G2",1),("G2",0.5),("D3",0.5),("A2",1),("D2",1)] * 2
write_wav(os.path.join(OUT_BGM, "bgm_battle3.wav"),
          render_song(mel, bass, 104, 0.15, 0.13, duty=0.4))

# 戦闘5(荘厳): ゆったりした讃歌調
mel = [("F4",2),("A4",1),("C5",1),("D5",2),("C5",2),
       ("A4",2),("G4",1),("F4",1),("G4",3),("-",1),
       ("C5",2),("D5",1),("E5",1),("F5",2),("E5",2),
       ("D5",2),("C5",1),("A4",1),("F4",3),("-",1)]
bass = [("F2",2),("C3",2),("D3",2),("A2",2),("B2",2),("F2",2),("C3",2),("C3",2)] + \
       [("F2",2),("C3",2),("A2",2),("F2",2),("B2",2),("C3",2),("F2",2),("F2",2)]
write_wav(os.path.join(OUT_BGM, "bgm_battle4.wav"),
          render_song(mel, bass, 88, 0.15, 0.13))

# 戦闘6(機械): 反復する無機質なリフ
mel = [("E4",0.25),("E5",0.25),("E4",0.25),("B4",0.25)] * 4 + \
      [("D4",0.25),("D5",0.25),("D4",0.25),("A4",0.25)] * 4 + \
      [("C4",0.25),("C5",0.25),("C4",0.25),("G4",0.25)] * 4 + \
      [("B3",0.25),("B4",0.25),("B3",0.25),("F#4",0.25)] * 4
bass = [("E2",1),("E2",1)] * 2 + [("D2",1),("D2",1)] * 2 + \
       [("C2",1),("C2",1)] * 2 + [("B2",1),("B2",1)] * 2
write_wav(os.path.join(OUT_BGM, "bgm_battle5.wav"),
          render_song(mel, bass, 138, 0.14, 0.13, duty=0.15))

# 勝利ファンファーレ
mel = [("C5",0.5),("C5",0.25),("C5",0.25),("C5",0.5),("E5",0.5),
       ("G5",0.75),("E5",0.25),("G5",2)]
bass = [("C3",1),("C3",1),("G2",1),("C3",2)]
write_wav(os.path.join(OUT_BGM, "bgm_victory.wav"), render_song(mel, bass, 120, 0.2, 0.14))

# 敗北
mel = [("E4",1),("D#4",1),("D4",1),("C#4",2),("-",0.5),("C4",2.5)]
bass = [("A2",2),("G#2",2),("A2",4)]
write_wav(os.path.join(OUT_BGM, "bgm_defeat.wav"), render_song(mel, bass, 80, 0.16, 0.12, 0.4))

print("OK")
