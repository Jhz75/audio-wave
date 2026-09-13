from pathlib import Path

root = Path(__file__).resolve().parents[1]
fx = root / "data/effects/vfx-material-engine-v2-pbr-sphere.effect"
text = fx.read_text(encoding="utf-8")
old = "if (material_has_height > 0.5 && material_quality > 0.5) {"
new = "if (material_has_height > 0.5 && material_quality > 1.5) {"
if text.count(old) != 1:
    raise RuntimeError(f"Expected one HIGH/ULTRA height gate, found {text.count(old)}")
text = text.replace(old, new, 1)
text = text.replace(
    "// Compatible with Audio Shader Engine v2 material uniforms.\n",
    "// Compatible with Audio Shader Engine v2 material uniforms.\n// Geometry height displacement is intentionally ULTRA-only; LIVE/HIGH keep texture cost bounded.\n",
    1,
)
fx.write_text(text, encoding="utf-8")
print("Applied LIVE/HIGH performance gate: true height displacement is ULTRA-only")
