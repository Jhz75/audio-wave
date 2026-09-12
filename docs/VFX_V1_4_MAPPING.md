# VFX v1.4 — Frequency mapping ergonomics

This revision keeps all existing calibrated audio uniforms and adds convenience mapping tools for richer shaders.

## Direct calibrated uniforms

- `audio_sub` — 20–60 Hz
- `audio_low` — 60–150 Hz
- `audio_low_mid` — 150–500 Hz
- `audio_mid_vfx` — 500–2000 Hz
- `audio_high_mid` — 2000–6000 Hz
- `audio_high` — 6000–16000 Hz
- `audio_transient` — fast positive spectral flux
- `audio_kick` — kick-oriented low-frequency onset detector

## Convenience macro uniforms

These are weighted combinations. They do not replace the direct bands.

- `audio_body` — low-end/body movement from SUB + LOW + LOW-MID
- `audio_motion` — medium-scale movement from LOW-MID + MID + HIGH-MID
- `audio_detail` — surface/detail motion from HIGH-MID + HIGH
- `audio_impact` — maximum of KICK and TRANSIENT
- `audio_energy` — weighted broad-spectrum energy

A shader can use any direct band, any macro, or any weighted blend. Example: use `audio_kick` for contraction, `audio_mid_vfx` for torsion, and `audio_high` for fine surface vibration.

## Expanded effect controls

`.effect.ini` metadata now supports:

- `option1` through `option16` (16 named float sliders)
- `color1` through `color8` (8 named colors)

Legacy shaders using only option1–8 and color1–4 remain compatible.

A practical pattern for complex audio-reactive effects is to reserve option1–8 for band influence amounts and option9–16 for visual parameters such as deformation, flow, roughness, rotation, glow, speed, scale and master response.
