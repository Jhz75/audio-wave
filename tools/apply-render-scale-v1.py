from pathlib import Path

CPP = Path("src/audio-shader-source.cpp")
HPP = Path("src/includes/audio-shader-source.hpp")

cpp = CPP.read_text(encoding="utf-8")
hpp = HPP.read_text(encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

# Header state
hpp = replace_once(
    hpp,
    "\tbool use_obs_canvas = true;\n\tuint32_t logged_width = 0;\n\tuint32_t logged_height = 0;\n",
    "\tbool use_obs_canvas = true;\n\tint render_scale_percent = 100;\n\tuint32_t logged_width = 0;\n\tuint32_t logged_height = 0;\n\tuint32_t logged_render_width = 0;\n\tuint32_t logged_render_height = 0;\n",
    "header render scale state",
)

# Setting key
cpp = replace_once(
    cpp,
    'static const char *S_USE_OBS_CANVAS = "use_obs_canvas";\n',
    'static const char *S_USE_OBS_CANVAS = "use_obs_canvas";\nstatic const char *S_RENDER_SCALE = "render_scale";\n',
    "render scale key",
)

# Helper to sanitize supported presets and calculate internal dimensions.
needle = "static uint32_t valid_dimension(int64_t value, uint32_t fallback)\n{\n\tif (value < 16)\n\t\treturn fallback;\n\tif (value > 8192)\n\t\treturn 8192;\n\treturn static_cast<uint32_t>(value);\n}\n"
replacement = needle + "\nstatic int valid_render_scale(int value)\n{\n\tswitch (value) {\n\tcase 25:\n\tcase 50:\n\tcase 75:\n\tcase 100:\n\t\treturn value;\n\tdefault:\n\t\treturn 100;\n\t}\n}\n\nstatic uint32_t scaled_render_dimension(uint32_t output_dimension, int render_scale_percent)\n{\n\tconst uint64_t scaled = (uint64_t(output_dimension) * uint64_t(render_scale_percent) + 50u) / 100u;\n\treturn std::clamp<uint32_t>(uint32_t(scaled), 16u, 8192u);\n}\n"
cpp = replace_once(cpp, needle, replacement, "render scale helpers")

# Shader params: preserve source_size as public output size, resolution as actual internal render target.
cpp = replace_once(
    cpp,
    "static void set_shader_params(audio_shader_source *s)\n{\n\tgs_effect_t *e = s->effect;\n\tif (!e)\n\t\treturn;\n\tset_vec2_param(e, \"source_size\", float(s->width), float(s->height));\n\tset_vec2_param(e, \"resolution\", float(s->width), float(s->height));\n",
    "static void set_shader_params(audio_shader_source *s, uint32_t render_width, uint32_t render_height)\n{\n\tgs_effect_t *e = s->effect;\n\tif (!e)\n\t\treturn;\n\tset_vec2_param(e, \"source_size\", float(s->width), float(s->height));\n\tset_vec2_param(e, \"resolution\", float(render_width), float(render_height));\n\tset_float_param(e, \"render_scale\", float(s->render_scale_percent) / 100.0f);\n",
    "shader params signature",
)

cpp = replace_once(
    cpp,
    "static void draw_fullscreen_quad(audio_shader_source *s)\n{\n\tgs_draw_sprite(nullptr, 0, s->width, s->height);\n}\n",
    "static void draw_fullscreen_quad(uint32_t width, uint32_t height)\n{\n\tgs_draw_sprite(nullptr, 0, width, height);\n}\n",
    "fullscreen quad helper",
)

# Render at scaled resolution, then upscale to the unchanged OBS source dimensions.
cpp = replace_once(
    cpp,
    "\tset_shader_params(s);\n\tgs_texrender_reset(s->texrender);\n\tif (!gs_texrender_begin(s->texrender, (int)s->width, (int)s->height)) {\n",
    "\tconst uint32_t render_width = scaled_render_dimension(s->width, s->render_scale_percent);\n\tconst uint32_t render_height = scaled_render_dimension(s->height, s->render_scale_percent);\n\tset_shader_params(s, render_width, render_height);\n\tgs_texrender_reset(s->texrender);\n\tif (!gs_texrender_begin(s->texrender, (int)render_width, (int)render_height)) {\n",
    "scaled texrender begin",
)

cpp = replace_once(
    cpp,
    "\tgs_ortho(0.0f, (float)s->width, 0.0f, (float)s->height, -100.0f, 100.0f);\n",
    "\tgs_ortho(0.0f, (float)render_width, 0.0f, (float)render_height, -100.0f, 100.0f);\n",
    "scaled projection",
)

cpp = replace_once(
    cpp,
    "\t\tdraw_fullscreen_quad(s);\n",
    "\t\tdraw_fullscreen_quad(render_width, render_height);\n",
    "scaled fullscreen draw",
)

cpp = replace_once(
    cpp,
    "\tif (!s->render_logged_ok || s->logged_width != s->width || s->logged_height != s->height) {\n\t\tBLOG(LOG_INFO, \"Rendering source '%s' with effect '%s' at %ux%u\", obs_source_get_name(s->self),\n\t\t     s->effect_path.c_str(), s->width, s->height);\n\t\ts->render_logged_ok = true;\n\t\ts->logged_width = s->width;\n\t\ts->logged_height = s->height;\n\t}\n",
    "\tif (!s->render_logged_ok || s->logged_width != s->width || s->logged_height != s->height ||\n\t    s->logged_render_width != render_width || s->logged_render_height != render_height) {\n\t\tBLOG(LOG_INFO,\n\t\t     \"Rendering source '%s' with effect '%s': output=%ux%u internal=%ux%u (%d%%)\",\n\t\t     obs_source_get_name(s->self), s->effect_path.c_str(), s->width, s->height, render_width, render_height,\n\t\t     s->render_scale_percent);\n\t\ts->render_logged_ok = true;\n\t\ts->logged_width = s->width;\n\t\ts->logged_height = s->height;\n\t\ts->logged_render_width = render_width;\n\t\ts->logged_render_height = render_height;\n\t}\n",
    "render scale logging",
)

# Properties UI
cpp = replace_once(
    cpp,
    "\tobs_properties_add_int(props, S_WIDTH, \"Manual Canvas Width\", 16, 8192, 1);\n\tobs_properties_add_int(props, S_HEIGHT, \"Manual Canvas Height\", 16, 8192, 1);\n",
    "\tobs_properties_add_int(props, S_WIDTH, \"Manual Canvas Width\", 16, 8192, 1);\n\tobs_properties_add_int(props, S_HEIGHT, \"Manual Canvas Height\", 16, 8192, 1);\n\tobs_property_t *render_scale = obs_properties_add_list(props, S_RENDER_SCALE, \"Internal Render Scale\",\n\t\t\t\t\t\t\t\t OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);\n\tobs_property_list_add_int(render_scale, \"100% (Full quality)\", 100);\n\tobs_property_list_add_int(render_scale, \"75%\", 75);\n\tobs_property_list_add_int(render_scale, \"50%\", 50);\n\tobs_property_list_add_int(render_scale, \"25% (Maximum performance)\", 25);\n",
    "render scale property",
)

# Default 100% for backward compatibility.
cpp = replace_once(
    cpp,
    "\tobs_data_set_default_int(settings, S_HEIGHT, 400);\n",
    "\tobs_data_set_default_int(settings, S_HEIGHT, 400);\n\tobs_data_set_default_int(settings, S_RENDER_SCALE, 100);\n",
    "render scale default",
)

# Read setting; changing only render scale does not alter public source dimensions.
cpp = replace_once(
    cpp,
    "\tset_source_dimensions(s, next_width, next_height);\n\ts->react_db = float(obs_data_get_double(settings, S_REACT_DB));\n",
    "\tset_source_dimensions(s, next_width, next_height);\n\tconst int next_render_scale = valid_render_scale((int)obs_data_get_int(settings, S_RENDER_SCALE));\n\tif (s->render_scale_percent != next_render_scale) {\n\t\ts->render_scale_percent = next_render_scale;\n\t\ts->render_logged_ok = false;\n\t}\n\ts->react_db = float(obs_data_get_double(settings, S_REACT_DB));\n",
    "render scale update",
)

CPP.write_text(cpp, encoding="utf-8")
HPP.write_text(hpp, encoding="utf-8")
print("Applied Internal Render Scale v1 changes")
