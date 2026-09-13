from pathlib import Path

hpp = Path('src/includes/audio-shader-source.hpp')
cpp = Path('src/audio-shader-source.cpp')

h = hpp.read_text(encoding='utf-8')
c = cpp.read_text(encoding='utf-8')

if 'float beat = 0.0f;' not in h:
    anchor = '\tfloat kick = 0.0f;\n'
    if anchor not in h:
        raise SystemExit('Header anchor not found')
    h = h.replace(anchor, anchor + '\tfloat beat = 0.0f; // VFX v1.7 independent short beat envelope\n', 1)

if 'const float beat_target = clamp01(kick_rise * 5.0f + transient_target * 0.10f);' not in c:
    anchor = '\ts->kick = clamp01(smooth(s->kick, kick_target, 4.0f, 110.0f));\n'
    if anchor not in c:
        raise SystemExit('Kick anchor not found')
    insert = anchor + (
        '\n\t// VFX v1.7 independent BEAT envelope. This is intentionally decoupled from\n'
        '\t// the user-facing Attack/Release controls used by the continuous bands.\n'
        '\t// It follows the positive SUB/LOW rise with a very short decay, so shaders\n'
        '\t// can create a true heartbeat/punch without freezing the rest of the motion.\n'
        '\tconst float beat_target = clamp01(kick_rise * 5.0f + transient_target * 0.10f);\n'
        '\ts->beat = clamp01(smooth(s->beat, beat_target, 6.0f, 45.0f));\n'
    )
    c = c.replace(anchor, insert, 1)

if 'set_float_param(e, "audio_beat", s->beat);' not in c:
    anchor = '\tset_float_param(e, "audio_kick", s->kick);\n'
    if anchor not in c:
        raise SystemExit('Shader-param anchor not found')
    c = c.replace(anchor, anchor + '\tset_float_param(e, "audio_beat", s->beat);\n', 1)

hpp.write_text(h, encoding='utf-8')
cpp.write_text(c, encoding='utf-8')
print('VFX v1.7 independent beat envelope applied')
