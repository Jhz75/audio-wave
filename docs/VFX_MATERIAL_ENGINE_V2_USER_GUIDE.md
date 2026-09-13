# VFX Material Engine v2 — User Guide

## What changed

The Audio Shader Engine keeps all existing VFX v1.x audio uniforms and shader compatibility, and adds six optional material texture slots:

1. Base / Albedo
2. Normal Map
3. Roughness Map
4. Metallic Map
5. Height / Displacement
6. Environment Map (2D equirectangular)

Compatible shaders also receive material quality and PBR controls. Existing shaders do not need to declare these uniforms and continue to work unchanged.

## Material quality presets

- **LIVE** — 56 raymarch steps. Intended for streaming and multi-source OBS scenes.
- **HIGH** — 80 raymarch steps. Recommended starting point for the RTX 3080 Ti at 600–900 px internal shader resolution.
- **ULTRA** — 112 raymarch steps. Intended for quality tests and lighter OBS scenes; not the default live preset.

The shader can additionally skip expensive height-map displacement in LIVE mode.

## Material controls

- **Normal Strength**: 0–2. Controls normal-map relief.
- **Height Strength**: 0–1. Controls real raymarched displacement when the shader supports it.
- **Environment Strength**: 0–2. Reflection/environment intensity.
- **Roughness**: fallback roughness when no map is loaded.
- **Metallic**: fallback metallic value when no map is loaded.
- **Triplanar Scale**: texture tiling density on raymarched 3D surfaces.

## Recommended 2K live starting point

For the first live tests:

- OBS output: 2560×1440 at 60 fps
- Shader manual canvas: 800×800
- Internal Render Scale: 75% or 100%
- Material Quality: HIGH
- Height map: optional; disable it first if GPU headroom becomes tight
- Normal map: enabled
- Roughness map: enabled
- Metallic map: enabled
- Environment map: enabled

The final decision must be based on OBS rendering-lag statistics on the real scene. Keep enough 3D GPU headroom for scene composition; do not tune a shader in isolation to 99–100% GPU utilization.

## Current environment-map limitation

v2 loads environment images through OBS' standard image loader. The first supported workflow is standard 2D image formats (PNG/JPEG/WebP/BMP/TGA) used as equirectangular environment maps. True floating-point `.hdr` / `.exr` loading is deliberately not claimed in this first build; it requires a dedicated HDR decode/upload path and should be added only after this texture/PBR pipeline is runtime-validated.

## Backward compatibility

The previous known-good Audio Wave state is preserved on:

`backup-vfx-v1-8-rendu-clean-1-2026-09-13`

The Material Engine development branch is:

`vfx-material-engine-v2`
