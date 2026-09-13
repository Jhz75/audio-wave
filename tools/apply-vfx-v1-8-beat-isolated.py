from pathlib import Path

root = Path(__file__).resolve().parents[1]
hpp = root / "src/includes/audio-shader-source.hpp"
cpp = root / "src/audio-shader-source.cpp"

h = hpp.read_text(encoding="utf-8")
c = cpp.read_text(encoding="utf-8")

old_h = """\tfloat kick = 0.0f;\n\tfloat beat = 0.0f; // VFX v1.7 independent short beat envelope\n\tfloat previous_kick_energy = 0.0f;\n"""
new_h = """\tfloat kick = 0.0f;\n\tfloat beat = 0.0f; // VFX v1.8 isolated short beat envelope\n\tfloat beat_floor = 0.0f;\n\tfloat previous_beat_focus = 0.0f;\n\tfloat beat_refractory = 0.0f;\n\tfloat previous_kick_energy = 0.0f;\n"""
if old_h not in h:
    raise SystemExit("header guard block not found")
h = h.replace(old_h, new_h, 1)

old_reset = """\t\ts->transient = clamp01(smooth(s->transient, 0.0f, 3.0f, 80.0f));\n\t\ts->kick = clamp01(smooth(s->kick, 0.0f, 4.0f, 110.0f));\n\t\tfor (float &band : s->bands)\n"""
new_reset = """\t\ts->transient = clamp01(smooth(s->transient, 0.0f, 3.0f, 80.0f));\n\t\ts->kick = clamp01(smooth(s->kick, 0.0f, 4.0f, 110.0f));\n\t\ts->beat = clamp01(smooth(s->beat, 0.0f, 5.0f, 52.0f));\n\t\ts->beat_floor = 0.0f;\n\t\ts->previous_beat_focus = 0.0f;\n\t\ts->beat_refractory = 0.0f;\n\t\tfor (float &band : s->bands)\n"""
if old_reset not in c:
    raise SystemExit("reset guard block not found")
c = c.replace(old_reset, new_reset, 1)

old_beat = """\t// VFX v1.7 independent BEAT envelope. This is intentionally decoupled from\n\t// the user-facing Attack/Release controls used by the continuous bands.\n\t// It follows the positive SUB/LOW rise with a very short decay, so shaders\n\t// can create a true heartbeat/punch without freezing the rest of the motion.\n\tconst float beat_target = clamp01(kick_rise * 5.0f + transient_target * 0.10f);\n\ts->beat = clamp01(smooth(s->beat, beat_target, 6.0f, 45.0f));\n"""
new_beat = """\t// VFX v1.8 isolated BEAT detector. Unlike v1.7, this does not use the broad\n\t// 20-150 Hz SUB/LOW energy or the full-spectrum transient detector. It focuses\n\t// on 35-105 Hz, compares the instantaneous energy with a slowly adapting\n\t// background floor, and uses a short refractory period to reject rumble tails\n\t// and double triggers. The output envelope remains independent of the user's\n\t// global Attack/Release controls.\n\tconst float beat_focus_db = hz_db(35.0f, 105.0f);\n\tconst float beat_focus = db_to_norm(beat_focus_db, s->react_db, s->peak_db);\n\tconst float beat_floor_tau = beat_focus > s->beat_floor ? 320.0f : 780.0f;\n\ts->beat_floor = clamp01(smooth(s->beat_floor, beat_focus, beat_floor_tau, beat_floor_tau));\n\tconst float beat_rise = std::max(0.0f, beat_focus - s->previous_beat_focus);\n\tconst float beat_above_floor = std::max(0.0f, beat_focus - s->beat_floor);\n\ts->previous_beat_focus = beat_focus;\n\ts->beat_refractory = std::max(0.0f, s->beat_refractory - dt);\n\n\tconst float beat_score = beat_rise * 3.9f + beat_above_floor * 1.55f;\n\tfloat beat_trigger = 0.0f;\n\tif (s->beat_refractory <= 0.0f && beat_focus > 0.16f && beat_score > 0.20f) {\n\t\tbeat_trigger = clamp01((beat_score - 0.16f) * 2.8f);\n\t\ts->beat_refractory = 0.115f;\n\t}\n\ts->beat = clamp01(smooth(s->beat, beat_trigger, 5.0f, 52.0f));\n"""
if old_beat not in c:
    raise SystemExit("v1.7 beat block not found")
c = c.replace(old_beat, new_beat, 1)

hpp.write_text(h, encoding="utf-8")
cpp.write_text(c, encoding="utf-8")
print("Applied VFX v1.8 isolated beat detector")
