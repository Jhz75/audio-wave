# VFX v1.1 calibration

Validated reference preset for techno / shranz testing:

- React at dB: -82 dB
- Peak at dB: -28 dB
- Attack: 14 ms
- Release: 140 ms
- FFT Size: 4096
- Shader Bands: 64

Per-zone musical calibration is applied in dB before normalization:

- SUB 20-60 Hz: -1 dB
- LOW 60-150 Hz: -2 dB
- LOW-MID 150-500 Hz: 0 dB
- MID 500-2000 Hz: +1 dB
- HIGH-MID 2-6 kHz: +2 dB
- HIGH 6-16 kHz: +3 dB

Transient sensitivity is multiplied by 1.5 compared with VFX v1. Kick detection intentionally keeps the original uncalibrated SUB/LOW detector input so the validated kick behaviour is preserved.
