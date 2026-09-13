from pathlib import Path

CPP = Path("src/audio-shader-source.cpp")
text = CPP.read_text(encoding="utf-8")

old = '''\t// VFX v1.4 convenience macro-uniforms. The original calibrated bands stay
\t// available independently; these simply make common mappings faster to author.
\tconst float audio_body = clamp01(s->sub * 0.15f + s->low * 0.45f + s->low_mid * 0.40f);
\tconst float audio_motion = clamp01(s->low_mid * 0.25f + s->mid_vfx * 0.50f + s->high_mid * 0.25f);
\tconst float audio_detail = clamp01(s->high_mid * 0.45f + s->high * 0.55f);
\tconst float audio_impact = std::max(s->kick, s->transient);
\tconst float audio_energy = clamp01(s->sub * 0.08f + s->low * 0.20f + s->low_mid * 0.22f +
\t\t\t\t\t s->mid_vfx * 0.22f + s->high_mid * 0.16f + s->high * 0.12f);
\tset_float_param(e, "audio_body", audio_body);
\tset_float_param(e, "audio_motion", audio_motion);
\tset_float_param(e, "audio_detail", audio_detail);
\tset_float_param(e, "audio_impact", audio_impact);
\tset_float_param(e, "audio_energy", audio_energy);
'''

new = '''\t// VFX v1.5 musical macro-uniforms. These are genre-neutral building blocks
\t// tuned for dense electronic music (shranz / hardgroove / industrial / bochka).
\t// The eight calibrated source signals remain independently available.
\t//
\t// BODY: low-frequency mass without letting SUB dominate everything.
\t// MOTION: material/shape movement centered on low-mids and mids.
\t// DETAIL: upper-spectrum surface activity.
\t// IMPACT: short-event detector, favouring kick but retaining non-kick transients.
\t// ENERGY: broad sustained musical density.
\t// PRESSURE: heavy low-end force useful for compression/expansion.
\t// GROOVE: rhythmic movement that remains useful on hardgroove as well as shranz.
\t// TEXTURE: metallic/noisy high-frequency content useful for industrial material detail.
\tconst float audio_body = clamp01(s->sub * 0.18f + s->low * 0.44f + s->low_mid * 0.38f);
\tconst float audio_motion = clamp01(s->low_mid * 0.32f + s->mid_vfx * 0.46f + s->high_mid * 0.22f);
\tconst float audio_detail = clamp01(s->high_mid * 0.52f + s->high * 0.48f);
\tconst float audio_impact = clamp01(std::max(s->kick * 1.15f, s->transient));
\tconst float audio_energy = clamp01(s->sub * 0.07f + s->low * 0.18f + s->low_mid * 0.22f +
\t\t\t\t\t s->mid_vfx * 0.23f + s->high_mid * 0.18f + s->high * 0.12f);
\tconst float audio_pressure = clamp01(s->sub * 0.30f + s->low * 0.50f + s->kick * 0.20f);
\tconst float audio_groove = clamp01(s->kick * 0.34f + s->low_mid * 0.28f + s->mid_vfx * 0.26f +
\t\t\t\t\t s->transient * 0.12f);
\tconst float audio_texture = clamp01(s->high_mid * 0.42f + s->high * 0.38f + s->transient * 0.20f);
\tset_float_param(e, "audio_body", audio_body);
\tset_float_param(e, "audio_motion", audio_motion);
\tset_float_param(e, "audio_detail", audio_detail);
\tset_float_param(e, "audio_impact", audio_impact);
\tset_float_param(e, "audio_energy", audio_energy);
\tset_float_param(e, "audio_pressure", audio_pressure);
\tset_float_param(e, "audio_groove", audio_groove);
\tset_float_param(e, "audio_texture", audio_texture);
'''

count = text.count(old)
if count != 1:
    raise RuntimeError(f"expected exactly one v1.4 macro block, found {count}")
text = text.replace(old, new, 1)
CPP.write_text(text, encoding="utf-8")
print("Applied VFX v1.5 musical macro-uniforms")
