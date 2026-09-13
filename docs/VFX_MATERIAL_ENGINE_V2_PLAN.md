# VFX Material Engine v2

Development branch: `vfx-material-engine-v2`

Goals:
- Preserve current Audio Wave VFX audio analysis and the Rendu Clean 1 baseline.
- Keep existing `.effect` shaders backward compatible.
- Add external material texture inputs.
- Add environment-map reflection support.
- Add material/quality controls suitable for live OBS use.
- Expose new shader uniforms for PBR-style materials.
- Validate all supported platforms before producing a Windows test package.

Initial implementation:
1. Texture slots: base/albedo, normal, roughness, metallic, height, environment.
2. Safe GPU texture loading with empty-slot fallbacks.
3. Texture sampling controls and quality preset uniforms.
4. Optional material uniforms: texture enable flags, normal strength, height strength, environment strength, roughness override, metallic override, triplanar scale, quality/raymarch budget.
5. Diagnostic material shader using GGX-style PBR, triplanar mapping and environment reflection.
6. Compatibility validation: old shaders must continue to compile and render.

Performance intent:
- LIVE: conservative material sampling and raymarch budget.
- HIGH: full PBR material path for 600–1024 internal pixels.
- ULTRA: higher raymarch/sample budgets for offline/testing, not the default live mode.

The previous Audio Wave state is preserved in `backup-vfx-v1-8-rendu-clean-1-2026-09-13`.
