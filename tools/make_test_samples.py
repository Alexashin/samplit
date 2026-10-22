#!/usr/bin/env python3
"""Генератор тестовой библиотеки драм-сэмплов (16-bit WAV) без внешних зависимостей.

Нужен, чтобы проверять плагин до появления настоящих сэмплов: имена файлов
содержат слова-характеристики (Punchy, Dirty, Long ...) для будущего тегирования,
форматы разные (44.1/48 кГц, моно/стерео) для проверки ресемплинга и каналов.

Использование:
    python tools/make_test_samples.py [OUT_DIR]

По умолчанию OUT_DIR = ~/Music/Samplit Test Samples. Существующие файлы
перезаписываются, результат детерминирован (seed от имени файла).
"""
import argparse
import math
import random
import struct
import wave
import zlib
from pathlib import Path

TAU = 2 * math.pi
NOTE_OFFSET = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_freq(name):
    """'F1' -> Гц (A4 = 440)."""
    n = NOTE_OFFSET[name[0]] + 12 * (int(name[1:]) + 1)
    return 440.0 * 2 ** ((n - 69) / 12)


# ---------- простые блоки ----------

def hpf(x, sr, fc):
    """Однополюсный ФВЧ."""
    a = math.exp(-TAU * fc / sr)
    out, px, py = [], 0.0, 0.0
    for s in x:
        py = a * (py + s - px)
        px = s
        out.append(py)
    return out


def lpf(x, sr, fc):
    """Однополюсный ФНЧ."""
    a = 1 - math.exp(-TAU * fc / sr)
    out, y = [], 0.0
    for s in x:
        y += a * (s - y)
        out.append(y)
    return out


def noise(rng, n):
    return [rng.uniform(-1, 1) for _ in range(n)]


def env(n, sr, decay):
    """Экспоненциальная огибающая, decay — постоянная времени в секундах."""
    return [math.exp(-i / sr / decay) for i in range(n)]


def mix(*layers):
    n = max(len(l) for l in layers)
    out = [0.0] * n
    for l in layers:
        for i, s in enumerate(l):
            out[i] += s
    return out


def saturate(x, drive):
    return [math.tanh(s * drive) for s in x]


# ---------- синтез ----------

def kick(rng, sr, dur, f0=150, f1=45, pitch_t=0.04, decay=0.2, click=0.3, drive=1.0):
    n = int(sr * dur)
    ph, out = 0.0, []
    for i in range(n):
        t = i / sr
        f = f1 + (f0 - f1) * math.exp(-t / pitch_t)
        ph += TAU * f / sr
        out.append(math.sin(ph) * math.exp(-t / decay))
    # короткий клик
    for i in range(int(sr * 0.003)):
        out[i] += click * rng.uniform(-1, 1) * (1 - i / (sr * 0.003))
    return saturate(out, drive) if drive > 1 else out


def snare(rng, sr, dur, tone_f=200, tone_decay=0.08, noise_decay=0.12, bright=2000, tone_amt=0.6):
    n = int(sr * dur)
    tone = [math.sin(TAU * tone_f * i / sr) * math.exp(-i / sr / tone_decay) for i in range(n)]
    nz = hpf(noise(rng, n), sr, bright)
    nz = [s * e for s, e in zip(nz, env(n, sr, noise_decay))]
    return mix([s * tone_amt for s in tone], nz)


def hat(rng, sr, dur, decay, bright=7000):
    n = int(sr * dur)
    x = noise(rng, n)
    for k in (1.0, 1.3, 1.6):  # каскад ФВЧ
        x = hpf(x, sr, bright * k * 0.6)
    return [s * e for s, e in zip(x, env(n, sr, decay))]


def clap(rng, sr, dur, tail=0.12, bright=900, bursts=3):
    n = int(sr * dur)
    x = hpf(noise(rng, n), sr, bright)
    x = lpf(x, sr, 6000)
    out = [0.0] * n
    gap = int(sr * 0.010)
    for b in range(bursts):  # серия коротких всплесков
        s0 = b * gap
        for i in range(gap):
            if s0 + i < n:
                out[s0 + i] = x[s0 + i] * math.exp(-i / sr / 0.004)
    s0 = bursts * gap  # хвост
    for i in range(s0, n):
        out[i] = x[i] * math.exp(-(i - s0) / sr / tail)
    return out


def sub808(rng, sr, dur, freq, decay=1.0, glide=0.03, drive=1.0):
    n = int(sr * dur)
    ph, out = 0.0, []
    for i in range(n):
        t = i / sr
        f = freq * (1 + 0.5 * math.exp(-t / glide))
        ph += TAU * f / sr
        out.append(math.sin(ph) * math.exp(-t / decay))
    return saturate(out, drive) if drive > 1 else out


def rim(rng, sr, dur, tone_f=900, decay=0.02):
    n = int(sr * dur)
    tone = [math.sin(TAU * tone_f * i / sr) for i in range(n)]
    nz = hpf(noise(rng, n), sr, 2500)
    e = env(n, sr, decay)
    return [(t * 0.7 + z * 0.5) * k for t, z, k in zip(tone, nz, e)]


def tom(rng, sr, dur, f0=180, decay=0.18):
    n = int(sr * dur)
    ph, out = 0.0, []
    for i in range(n):
        t = i / sr
        ph += TAU * (f0 * (1 + 0.4 * math.exp(-t / 0.03))) / sr
        out.append(math.sin(ph) * math.exp(-t / decay))
    return out


def shaker(rng, sr, dur):
    n = int(sr * dur)
    x = lpf(hpf(noise(rng, n), sr, 4000), sr, 10000)
    # плавная атака, затем спад
    return [s * (min(1, i / (sr * 0.03))) * math.exp(-i / sr / 0.07) for i, s in enumerate(x)]


def riser(rng, sr, dur, dark=False):
    n = int(sr * dur)
    nz = noise(rng, n)
    out, y, py, px = [], 0.0, 0.0, 0.0
    for i in range(n):
        p = i / n
        # частота среза растёт со временем
        fc = 300 + (6000 if dark else 9000) * p * p
        a = 1 - math.exp(-TAU * fc / sr)
        y += a * (nz[i] - y)  # ФНЧ с растущим срезом
        if dark:
            out.append(y * p)
        else:
            # ФВЧ с растущим срезом
            h = math.exp(-TAU * (200 + 4000 * p) / sr)
            py = h * (py + y - px)
            px = y
            out.append(py * p)
    return out


# ---------- вывод ----------

def finalize(x, sr, peak_db=-1.0):
    """Фейд-аут 2 мс и нормализация в пик peak_db."""
    x = list(x)
    nf = min(len(x), int(sr * 0.002))
    for i in range(nf):
        x[len(x) - nf + i] *= 1 - (i + 1) / nf
    return x


def normalise(chans, peak_db=-1.0):
    peak = max(abs(s) for c in chans for s in c) or 1.0
    g = 10 ** (peak_db / 20) / peak
    return [[s * g for s in c] for c in chans]


def write_wav(path, chans, sr):
    path.parent.mkdir(parents=True, exist_ok=True)
    n = len(chans[0])
    frames = bytearray()
    for i in range(n):
        for c in chans:
            frames += struct.pack("<h", max(-32768, min(32767, int(round(c[i] * 32767)))))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(len(chans))
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(frames))


def make_stereo(x, sr, rng):
    """Слегка разные L/R: задержка ~0.3 мс и разный уровень."""
    d = int(sr * 0.0003)
    right = [0.0] * d + x[:len(x) - d]
    return [[s * 1.0 for s in x], [s * 0.85 for s in right]]


# ---------- каталог ----------
# (папка, имя, функция(rng, sr), sr, stereo)

def catalog():
    K = lambda dur, **kw: lambda r, sr: kick(r, sr, dur, **kw)
    S = lambda dur, **kw: lambda r, sr: snare(r, sr, dur, **kw)
    H = lambda dur, decay, **kw: lambda r, sr: hat(r, sr, dur, decay, **kw)
    C = lambda dur, **kw: lambda r, sr: clap(r, sr, dur, **kw)
    B = lambda note, dur, **kw: lambda r, sr: sub808(r, sr, dur, note_freq(note), **kw)
    P = lambda f: f

    L = [
        ("Kicks", "Kick_Punchy_01", K(0.45, f0=160, f1=50, decay=0.16, click=0.5), 44100, False),
        ("Kicks", "Kick_Punchy_Hard_02", K(0.5, f0=180, f1=55, decay=0.2, click=0.6, drive=3), 44100, False),
        ("Kicks", "Kick_Soft_Long_01", K(0.85, f0=120, f1=42, pitch_t=0.06, decay=0.35, click=0.05), 44100, False),
        ("Kicks", "Kick_Soft_Long_02", K(0.9, f0=110, f1=40, pitch_t=0.07, decay=0.4, click=0.02), 48000, False),
        ("Kicks", "Kick_Deep_Sub_01", K(0.8, f0=100, f1=38, pitch_t=0.05, decay=0.38, click=0.1), 44100, False),
        ("Kicks", "Kick_Dirty_Short_01", K(0.4, f0=150, f1=48, decay=0.12, click=0.4, drive=5), 44100, True),
        ("Snares", "Snare_Bright_Tight_01", S(0.3, tone_f=220, tone_decay=0.05, noise_decay=0.07, bright=4500), 44100, False),
        ("Snares", "Snare_Bright_Tight_02", S(0.3, tone_f=210, tone_decay=0.06, noise_decay=0.09, bright=5000), 44100, False),
        ("Snares", "Snare_Fat_Body_01", S(0.45, tone_f=180, tone_decay=0.12, noise_decay=0.15, bright=1500, tone_amt=1.0), 44100, False),
        ("Snares", "Snare_Dark_Soft_01", S(0.4, tone_f=190, tone_decay=0.09, noise_decay=0.14, bright=800, tone_amt=0.8), 44100, False),
        ("Snares", "Snare_Crisp_Long_01", S(0.5, tone_f=200, tone_decay=0.07, noise_decay=0.2, bright=3500), 44100, False),
        ("Hats", "Hat_Closed_Short_01", H(0.08, 0.015, bright=7000), 44100, False),
        ("Hats", "Hat_Closed_Short_02", H(0.1, 0.02, bright=6000), 44100, False),
        ("Hats", "Hat_Closed_Dark_01", H(0.12, 0.03, bright=3500), 44100, False),
        ("Hats", "Hat_Open_Long_01", H(0.6, 0.18, bright=6500), 44100, False),
        ("Hats", "Hat_Open_Bright_01", H(0.5, 0.14, bright=9000), 44100, False),
        ("Hats", "Hat_Open_Dark_Long_01", H(0.6, 0.22, bright=4000), 44100, True),
        ("Claps", "Clap_Wide_01", C(0.4, tail=0.14, bright=700, bursts=4), 44100, False),
        ("Claps", "Clap_Tight_01", C(0.4, tail=0.06, bright=1500, bursts=3), 44100, False),
        ("Claps", "Clap_Bright_01", C(0.4, tail=0.1, bright=2500, bursts=3), 44100, False),
        ("Claps", "Clap_Dark_Room_01", C(0.4, tail=0.18, bright=500, bursts=4), 44100, False),
        ("808", "808_Dirty_Long_F1", B("F1", 2.5, decay=1.1, drive=4), 44100, False),
        ("808", "808_Dirty_Short_D1", B("D1", 1.3, decay=0.4, drive=6), 44100, False),
        ("808", "808_Clean_Short_C2", B("C2", 1.2, decay=0.35, glide=0.02), 44100, False),
        ("808", "808_Clean_Long_G1", B("G1", 2.2, decay=1.0), 44100, False),
        ("808", "808_Deep_Long_A1", B("A1", 2.5, decay=1.3, glide=0.05), 44100, False),
        ("808", "808_Punchy_Short_E1", B("E1", 1.5, decay=0.5, glide=0.015, drive=2), 44100, False),
        ("Perc", "Perc_Rim_01", lambda r, sr: rim(r, sr, 0.12, tone_f=900), 44100, False),
        ("Perc", "Perc_Rim_Bright_02", lambda r, sr: rim(r, sr, 0.1, tone_f=1400, decay=0.015), 44100, False),
        ("Perc", "Perc_Tom_Low_01", lambda r, sr: tom(r, sr, 0.6, f0=110, decay=0.25), 44100, False),
        ("Perc", "Perc_Tom_High_01", lambda r, sr: tom(r, sr, 0.4, f0=220, decay=0.15), 44100, False),
        ("Perc", "Perc_Shaker_Short_01", lambda r, sr: shaker(r, sr, 0.25), 44100, False),
        ("Perc", "Perc_Click_Wood_01", lambda r, sr: rim(r, sr, 0.08, tone_f=600, decay=0.01), 44100, False),
        ("FX", "FX_Riser_Dark_01", lambda r, sr: riser(r, sr, 3.0, dark=True), 44100, False),
        ("FX", "FX_Riser_Bright_01", lambda r, sr: riser(r, sr, 2.0, dark=False), 44100, False),
        ("FX", "FX_Riser_Bright_Long_02", lambda r, sr: riser(r, sr, 3.0, dark=False), 48000, False),
        ("FX", "FX_Riser_Dark_Short_02", lambda r, sr: riser(r, sr, 2.0, dark=True), 44100, False),
    ]
    return L


def main():
    ap = argparse.ArgumentParser(description="Генератор тестовых драм-сэмплов")
    ap.add_argument("out_dir", nargs="?", default=str(Path.home() / "Music" / "Samplit Test Samples"))
    out = Path(ap.parse_args().out_dir)

    counts, total = {}, 0
    for folder, name, fn, sr, stereo in catalog():
        rng = random.Random(zlib.crc32(name.encode()))  # воспроизводимый seed
        x = finalize(fn(rng, sr), sr)
        chans = make_stereo(x, sr, rng) if stereo else [x]
        write_wav(out / folder / f"{name}.wav", normalise(chans), sr)
        counts[folder] = counts.get(folder, 0) + 1
        total += 1

    print(f"Выходная папка: {out}")
    for folder, c in counts.items():
        print(f"  {folder:8s} {c}")
    print(f"Всего файлов: {total}")


if __name__ == "__main__":
    main()
