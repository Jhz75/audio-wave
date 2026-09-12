from pathlib import Path

CPP = Path('src/audio-shader-source.cpp')
HPP = Path('src/includes/audio-shader-source.hpp')

cpp = CPP.read_text(encoding='utf-8')
hpp = HPP.read_text(encoding='utf-8')


def replace_exact(text, old, new, label, expected=1):
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f'{label}: expected {expected} matches, found {count}')
    return text.replace(old, new)

# 1) Double shader-side UI capacity: 16 sliders / 8 colors.
hpp = replace_exact(
    hpp,
    '\tstd::array<float, 8> options{};\n\tstd::array<uint32_t, 4> colors{0xFFFFFFu, 0xFFD200u, 0xBB509Du, 0xAC3CFFu};',
    '\tstd::array<float, 16> options{};\n\tstd::array<uint32_t, 8> colors{0xFFFFFFu, 0xFFD200u, 0xBB509Du, 0xAC3CFFu, 0x38D9FFu, 0xFF6B35u, 0x7CFF6Bu, 0x111111u};',
    'header control arrays',
)

cpp = replace_exact(cpp, 'std::array<std::string, 8> option_labels{};', 'std::array<std::string, 16> option_labels{};', 'option metadata array')
cpp = replace_exact(cpp, 'std::array<std::string, 4> color_labels{};', 'std::array<std::string, 8> color_labels{};', 'color metadata array')
cpp = replace_exact(cpp, 'if (idx >= 1 && idx <= 8 && value.rfind("Custom Option", 0) != 0)', 'if (idx >= 1 && idx <= 16 && value.rfind("Custom Option", 0) != 0)', 'option metadata limit')
cpp = replace_exact(cpp, 'if (idx >= 1 && idx <= 4)', 'if (idx >= 1 && idx <= 8)', 'color metadata limit')
cpp = replace_exact(cpp, 'for (int i = 1; i <= 8; ++i) {', 'for (int i = 1; i <= 16; ++i) {', 'option loops', expected=2)
cpp = replace_exact(cpp, 'for (int i = 1; i <= 4; ++i) {', 'for (int i = 1; i <= 8; ++i) {', 'color loops', expected=2)

# 2) Add convenience musical macro-uniforms. These do not replace the eight
# calibrated bands; they are ergonomic combinations for common visual actions.
needle = '''\tset_float_param(e, "audio_transient", s->transient);\n\tset_float_param(e, "audio_kick", s->kick);\n\tset_float_param(e, "band_count", float(s->band_count));'''
replacement = '''\tset_float_param(e, "audio_transient", s->transient);\n\tset_float_param(e, "audio_kick", s->kick);\n\n\t// VFX v1.4 convenience macro-uniforms. The original calibrated bands stay\n\t// available independently; these simply make common mappings faster to author.\n\tconst float audio_body = clamp01(s->sub * 0.15f + s->low * 0.45f + s->low_mid * 0.40f);\n\tconst float audio_motion = clamp01(s->low_mid * 0.25f + s->mid_vfx * 0.50f + s->high_mid * 0.25f);\n\tconst float audio_detail = clamp01(s->high_mid * 0.45f + s->high * 0.55f);\n\tconst float audio_impact = std::max(s->kick, s->transient);\n\tconst float audio_energy = clamp01(s->sub * 0.08f + s->low * 0.20f + s->low_mid * 0.22f +\n\t\t\t\t\t s->mid_vfx * 0.22f + s->high_mid * 0.16f + s->high * 0.12f);\n\tset_float_param(e, "audio_body", audio_body);\n\tset_float_param(e, "audio_motion", audio_motion);\n\tset_float_param(e, "audio_detail", audio_detail);\n\tset_float_param(e, "audio_impact", audio_impact);\n\tset_float_param(e, "audio_energy", audio_energy);\n\tset_float_param(e, "band_count", float(s->band_count));'''
cpp = replace_exact(cpp, needle, replacement, 'macro uniforms')

# 3) Give all eight colors stable defaults for new sources.
needle = '''\tobs_data_set_default_int(settings, "color1", 0xFFFFFF);\n\tobs_data_set_default_int(settings, "color2", 0xFFD200);\n\tobs_data_set_default_int(settings, "color3", 0xBB509D);\n\tobs_data_set_default_int(settings, "color4", 0xAC3CFF);'''
replacement = '''\tobs_data_set_default_int(settings, "color1", 0xFFFFFF);\n\tobs_data_set_default_int(settings, "color2", 0xFFD200);\n\tobs_data_set_default_int(settings, "color3", 0xBB509D);\n\tobs_data_set_default_int(settings, "color4", 0xAC3CFF);\n\tobs_data_set_default_int(settings, "color5", 0x38D9FF);\n\tobs_data_set_default_int(settings, "color6", 0xFF6B35);\n\tobs_data_set_default_int(settings, "color7", 0x7CFF6B);\n\tobs_data_set_default_int(settings, "color8", 0x111111);'''
cpp = replace_exact(cpp, needle, replacement, 'color defaults')

# 4) Update help copy so shader authors know the expanded capacity exists.
cpp = replace_exact(
    cpp,
    'Only named controls are shown here; unnamed option uniforms stay hidden.',
    'Up to 16 named option sliders and 8 named colors can be exposed; unnamed option uniforms stay hidden.',
    'effect metadata help',
)

CPP.write_text(cpp, encoding='utf-8')
HPP.write_text(hpp, encoding='utf-8')
print('Applied VFX v1.4 mapping ergonomics: 16 options, 8 colors, macro audio uniforms')
