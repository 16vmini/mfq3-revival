#!/usr/bin/env python3
# MFQ3 AI radio: generate the ATC/AWACS/pilot voice library with neural TTS
# (edge-tts), then run every line through an aviation-radio effect (bandpass +
# soft clip + hiss + squelch click) and write Q3-ready WAVs (22050 Hz mono s16).
#   python gen_radio.py <outdir>
# Voices: tower = British male, AWACS = US female, pilot = US male.
import sys, os, asyncio, math
import numpy as np
import edge_tts, miniaudio
import wave

OUT = sys.argv[1] if len(sys.argv) > 1 else r"C:\source\mfq3\_incoming\radio"
os.makedirs(OUT, exist_ok=True)

TOWER  = "en-GB-RyanNeural"
AWACS  = "en-US-JennyNeural"
PILOT  = "en-US-GuyNeural"

LINES = [
    # name                voice   text
    ("radio_clearance",   TOWER,  "Ghost One, runway two seven, wind calm. Cleared for take off."),
    ("radio_airborne",    TOWER,  "Ghost One airborne. Good hunting."),
    ("radio_bandit",      AWACS,  "Ghost One, be advised: enemy aircraft on the ground, bullseye zero four five. Weapons free."),
    ("radio_splash",      AWACS,  "Splash one! Good kill, Ghost One. Good kill."),
    ("radio_rtb",         AWACS,  "All targets destroyed. Ghost One, return to base."),
    ("radio_home",        TOWER,  "Ghost One, welcome home. Nicely done."),
    ("radio_eject",       PILOT,  "Eject! Eject! Eject!"),
    ("radio_chute",       AWACS,  "Good chute, good chute. Recovery team is on the way."),
    ("radio_failed",      AWACS,  "Ghost One is down. I say again: Ghost One is down."),
    ("radio_board",       TOWER,  "Ghost One, you are clear to start engines."),
    ("radio_gate",        AWACS,  "Gate clear. Next gate is on your nose."),
    ("radio_course_done", AWACS,  "That's the course complete. Well flown, Ghost One."),
    ("radio_trap",        AWACS,  "Three wire! Good trap, Ghost One."),
]

SR = 22050

def radio_fx(pcm, sr):
    """aviation radio: bandpass 300-3000, soft clip, hiss, squelch clicks."""
    x = pcm.astype(np.float64) / 32768.0
    # resample to SR (linear)
    if sr != SR:
        n2 = int(len(x) * SR / sr)
        x = np.interp(np.linspace(0, len(x) - 1, n2), np.arange(len(x)), x)
    # FFT bandpass 300..3000 Hz with soft edges
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    mask = np.clip((f - 200) / 150, 0, 1) * np.clip((3400 - f) / 600, 0, 1)
    x = np.fft.irfft(X * mask, n=len(x))
    # compress/soft-clip hard (radios are LOUD and flat)
    x = np.tanh(x * 6.0) * 0.85
    # background hiss
    x += np.random.normal(0, 0.012, len(x))
    # squelch click + noise burst at both ends
    click = int(0.035 * SR)
    burst = np.random.normal(0, 0.25, click) * np.linspace(1, 0, click)
    pad   = int(0.06 * SR)
    out = np.concatenate([burst, np.zeros(pad // 2), x, np.zeros(pad // 2), burst[::-1] * 0.8])
    return np.clip(out * 32767, -32767, 32767).astype(np.int16)

async def gen_one(name, voice, text):
    mp3 = b""
    com = edge_tts.Communicate(text, voice, rate="+8%")
    async for chunk in com.stream():
        if chunk["type"] == "audio":
            mp3 += chunk["data"]
    dec = miniaudio.decode(mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1)
    pcm = np.frombuffer(dec.samples, dtype=np.int16)
    out = radio_fx(pcm, dec.sample_rate)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(out.tobytes())
    print("  %-18s %-22s %4.1fs  %s" % (name, voice.split("-")[-1], len(out) / SR, text[:50]))

async def main():
    print("generating %d radio calls -> %s" % (len(LINES), OUT))
    for name, voice, text in LINES:
        if os.path.exists(os.path.join(OUT, name + ".wav")):
            print("  %-18s (already done)" % name); continue
        for attempt in range(6):
            try:
                await gen_one(name, voice, text)
                break
            except Exception as e:
                print("  %-18s retry %d (%s)" % (name, attempt + 1, str(e)[:60]))
                await asyncio.sleep(2 + attempt * 2)
        else:
            print("  %-18s FAILED after retries" % name)

asyncio.run(main())
print("done.")
