#!/usr/bin/env python3
"""Generate original mono placeholder ambience; no downloaded audio is used."""
from array import array
from math import exp, pi, sin
from pathlib import Path
import random
import sys
import wave

RATE = 22050
OUTPUT = Path(__file__).resolve().parents[1] / 'runtime/assets/audio/village'
OUTPUT.mkdir(parents=True, exist_ok=True)


def write(name, samples):
    pcm = array('h', (round(max(-1, min(1, value)) * 32767) for value in samples))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(OUTPUT / name), 'wb') as output:
        output.setparams((1, 2, RATE, len(pcm), 'NONE', 'not compressed'))
        output.writeframes(pcm.tobytes())


def strike(samples, onset, duration, frequency, modes, decay, gain):
    start = round(onset * RATE)
    for offset in range(round(duration * RATE)):
        t = offset / RATE
        attack = min(1, t * 1500)
        value = sum(sin(2 * pi * frequency * ratio * t) * exp(-t * decay * damping) * weight
                    for ratio, damping, weight in modes)
        samples[start + offset] += gain * attack * value


rng = random.Random(631)
wind = []
filtered = 0
for sample in range(12 * RATE):
    t = sample / RATE
    filtered += (rng.uniform(-1, 1) - filtered) * 0.035
    envelope = min(1, t * 3, (12 - t) * 3)
    wind.append(filtered * (0.9 + 0.3 * sin(2 * pi * t / 12)) * envelope)
write('wind.wav', wind)

forge = [0.0] * (8 * RATE)
for onset in (0.4, 1.15, 1.95, 3.4, 4.2, 5.05):
    strike(forge, onset, 1.2, 730, ((1, 1, 0.55), (2.73, 1.9, 0.25), (4.12, 3, 0.12)), 5, 0.65)
write('hammer.wav', forge)

bell = [0.0] * (48 * RATE)
for onset in (0.5, 3.0):
    strike(bell, onset, 12, 185, ((0.5, 0.6, 0.3), (1, 0.8, 0.4), (1.19, 1.1, 0.18),
                               (2.76, 1.8, 0.1), (4.07, 2.5, 0.05)), 0.65, 0.48)
write('bell.wav', bell)
print(f'Generated wind, hammer and bell ambience in {OUTPUT}')
