from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HPP = ROOT / "src/includes/audio-shader-source.hpp"
CPP = ROOT / "src/audio-shader-source.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


hpp = HPP.read_text(encoding="utf-8")
cpp = CPP.read_text(encoding="utf-8")

# -----------------------------------------------------------------------------
# Header: image loader + material state
# -----------------------------------------------------------------------------
hpp = replace_once(
    hpp,
    '#include <graphics/graphics.h>\n',
    '#include <graphics/graphics.h>\n#include <graphics/image-file.h>\n',
    'header image-file include',
)

hpp = replace_once(
    hpp,
    '\tgs_texture_t *band_texture = nullptr;\n\tstd::array<uint8_t, 64 * 4> band_texture_pixels{};\n\n\tstd::array<float, 16> options{};\n',
    '''\tgs_texture_t *band_texture = nullptr;\n\tstd::array<uint8_t, 64 * 4> band_texture_pixels{};\n\n\t// VFX Material Engine v2. Six optional 2D texture slots are loaded by the\n\t// plugin and exposed to compatible .effect shaders. Existing shaders simply\n\t// ignore these uniforms and remain fully backward compatible.\n\tstd::array<std::string, 6> material_texture_paths{};\n\tstd::array<gs_image_file_t, 6> material_images{};\n\tstd::array<bool, 6> material_image_initialized{};\n\tbool reload_material_textures = true;\n\tint material_quality = 1; // 0=LIVE, 1=HIGH, 2=ULTRA\n\tfloat material_normal_strength = 1.0f;\n\tfloat material_height_strength = 0.35f;\n\tfloat material_environment_strength = 1.0f;\n\tfloat material_roughness = 0.22f;\n\tfloat material_metallic = 1.0f;\n\tfloat material_triplanar_scale = 2.5f;\n\n\tstd::array<float, 16> options{};\n''',
    'header material state',
)

# -----------------------------------------------------------------------------
# CPP constants
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    'static const char *S_COLOR_PREFIX = "color";\n',
    '''static const char *S_COLOR_PREFIX = "color";\n\n// VFX Material Engine v2 settings.\nstatic const char *S_MAT_ALBEDO = "material_albedo_path";\nstatic const char *S_MAT_NORMAL = "material_normal_path";\nstatic const char *S_MAT_ROUGHNESS = "material_roughness_path";\nstatic const char *S_MAT_METALLIC = "material_metallic_path";\nstatic const char *S_MAT_HEIGHT = "material_height_path";\nstatic const char *S_MAT_ENVIRONMENT = "material_environment_path";\nstatic const char *S_MAT_QUALITY = "material_quality";\nstatic const char *S_MAT_NORMAL_STRENGTH = "material_normal_strength";\nstatic const char *S_MAT_HEIGHT_STRENGTH = "material_height_strength";\nstatic const char *S_MAT_ENV_STRENGTH = "material_environment_strength";\nstatic const char *S_MAT_ROUGHNESS_VALUE = "material_roughness";\nstatic const char *S_MAT_METALLIC_VALUE = "material_metallic";\nstatic const char *S_MAT_TRIPLANAR_SCALE = "material_triplanar_scale";\n\nstatic const std::array<const char *, 6> kMaterialPathKeys = {\n\tS_MAT_ALBEDO, S_MAT_NORMAL, S_MAT_ROUGHNESS, S_MAT_METALLIC, S_MAT_HEIGHT, S_MAT_ENVIRONMENT};\nstatic const std::array<const char *, 6> kMaterialUniformNames = {\n\t"material_albedo_texture", "material_normal_texture", "material_roughness_texture",\n\t"material_metallic_texture", "material_height_texture", "material_environment_texture"};\nstatic const std::array<const char *, 6> kMaterialHasUniformNames = {\n\t"material_has_albedo", "material_has_normal", "material_has_roughness",\n\t"material_has_metallic", "material_has_height", "material_has_environment"};\nstatic const std::array<const char *, 6> kMaterialSizeUniformNames = {\n\t"material_albedo_size", "material_normal_size", "material_roughness_size",\n\t"material_metallic_size", "material_height_size", "material_environment_size"};\n''',
    'material setting constants',
)

# -----------------------------------------------------------------------------
# Texture lifecycle helpers, inserted after band texture destroy helper.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''static void destroy_band_texture(audio_shader_source *s)\n{\n\tif (s && s->band_texture) {\n\t\tgs_texture_destroy(s->band_texture);\n\t\ts->band_texture = nullptr;\n\t}\n}\n\nstatic void load_effect_if_needed(audio_shader_source *s)\n''',
    '''static void destroy_band_texture(audio_shader_source *s)\n{\n\tif (s && s->band_texture) {\n\t\tgs_texture_destroy(s->band_texture);\n\t\ts->band_texture = nullptr;\n\t}\n}\n\nstatic void destroy_material_texture(audio_shader_source *s, size_t index)\n{\n\tif (!s || index >= s->material_images.size() || !s->material_image_initialized[index])\n\t\treturn;\n\tgs_image_file_free(&s->material_images[index]);\n\tstd::memset(&s->material_images[index], 0, sizeof(gs_image_file_t));\n\ts->material_image_initialized[index] = false;\n}\n\nstatic void destroy_material_textures(audio_shader_source *s)\n{\n\tif (!s)\n\t\treturn;\n\tfor (size_t i = 0; i < s->material_images.size(); ++i)\n\t\tdestroy_material_texture(s, i);\n}\n\nstatic void load_material_textures_if_needed(audio_shader_source *s)\n{\n\tif (!s || !s->reload_material_textures)\n\t\treturn;\n\n\ts->reload_material_textures = false;\n\tfor (size_t i = 0; i < s->material_images.size(); ++i) {\n\t\tdestroy_material_texture(s, i);\n\t\tconst std::string &path = s->material_texture_paths[i];\n\t\tif (path.empty())\n\t\t\tcontinue;\n\n\t\tgs_image_file_init(&s->material_images[i], path.c_str());\n\t\ts->material_image_initialized[i] = true;\n\t\tif (!s->material_images[i].loaded) {\n\t\t\tBLOG(LOG_WARNING, "Material texture %zu failed to load: %s", i + 1, path.c_str());\n\t\t\tcontinue;\n\t\t}\n\t\tgs_image_file_init_texture(&s->material_images[i]);\n\t\tif (!s->material_images[i].texture) {\n\t\t\tBLOG(LOG_WARNING, "Material texture %zu loaded but GPU texture creation failed: %s", i + 1,\n\t\t\t     path.c_str());\n\t\t\tcontinue;\n\t\t}\n\t\tBLOG(LOG_INFO, "Material texture %zu ready: %s (%ux%u)", i + 1, path.c_str(),\n\t\t     s->material_images[i].cx, s->material_images[i].cy);\n\t}\n}\n\nstatic gs_texture_t *material_texture(const audio_shader_source *s, size_t index)\n{\n\tif (!s || index >= s->material_images.size() || !s->material_image_initialized[index])\n\t\treturn nullptr;\n\treturn s->material_images[index].texture;\n}\n\nstatic void load_effect_if_needed(audio_shader_source *s)\n''',
    'material texture lifecycle',
)

# -----------------------------------------------------------------------------
# Bind material uniforms and textures after audio band textures.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\tset_texture_param(e, "audio_band_texture", s->band_texture);\n\tset_texture_param(e, "audio_spectrum_texture", s->band_texture);\n\n\tfor (size_t i = 0; i < s->options.size(); ++i) {\n''',
    '''\tset_texture_param(e, "audio_band_texture", s->band_texture);\n\tset_texture_param(e, "audio_spectrum_texture", s->band_texture);\n\n\t// VFX Material Engine v2 uniforms. The quality value and explicit raymarch\n\t// budget let compatible shaders scale cost without changing source resolution.\n\tset_float_param(e, "material_quality", float(s->material_quality));\n\tset_float_param(e, "material_raymarch_steps", s->material_quality == 0 ? 56.0f : (s->material_quality == 1 ? 80.0f : 112.0f));\n\tset_float_param(e, "material_normal_strength", s->material_normal_strength);\n\tset_float_param(e, "material_height_strength", s->material_height_strength);\n\tset_float_param(e, "material_environment_strength", s->material_environment_strength);\n\tset_float_param(e, "material_roughness", s->material_roughness);\n\tset_float_param(e, "material_metallic", s->material_metallic);\n\tset_float_param(e, "material_triplanar_scale", s->material_triplanar_scale);\n\tfor (size_t i = 0; i < s->material_images.size(); ++i) {\n\t\tgs_texture_t *texture = material_texture(s, i);\n\t\tset_float_param(e, kMaterialHasUniformNames[i], texture ? 1.0f : 0.0f);\n\t\tif (texture) {\n\t\t\tset_texture_param(e, kMaterialUniformNames[i], texture);\n\t\t\tset_vec2_param(e, kMaterialSizeUniformNames[i], float(gs_texture_get_width(texture)),\n\t\t\t               float(gs_texture_get_height(texture)));\n\t\t} else {\n\t\t\tset_vec2_param(e, kMaterialSizeUniformNames[i], 0.0f, 0.0f);\n\t\t}\n\t}\n\n\tfor (size_t i = 0; i < s->options.size(); ++i) {\n''',
    'material shader bindings',
)

# -----------------------------------------------------------------------------
# Load pending material images on the render thread before the effect is used.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\tcalculate_audio_state(s);\n\tupdate_band_texture(s);\n\tload_effect_if_needed(s);\n''',
    '''\tcalculate_audio_state(s);\n\tupdate_band_texture(s);\n\tload_material_textures_if_needed(s);\n\tload_effect_if_needed(s);\n''',
    'render material loader',
)

# -----------------------------------------------------------------------------
# UI: material group, inserted before dynamic effect controls.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\tobs_properties_add_int_slider(props, S_BAND_COUNT, "Shader Bands", 8, 64, 1);\n\tstd::string meta_effect_path = s && !s->effect_path.empty() ? s->effect_path : default_effect_path_string();\n''',
    '''\tobs_properties_add_int_slider(props, S_BAND_COUNT, "Shader Bands", 8, 64, 1);\n\n\tobs_properties_t *material = obs_properties_create();\n\tconst char *image_filter = "Images (*.png *.jpg *.jpeg *.webp *.bmp *.tga);;All files (*.*)";\n\tobs_properties_add_path(material, S_MAT_ALBEDO, "Base / Albedo", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_properties_add_path(material, S_MAT_NORMAL, "Normal Map", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_properties_add_path(material, S_MAT_ROUGHNESS, "Roughness Map", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_properties_add_path(material, S_MAT_METALLIC, "Metallic Map", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_properties_add_path(material, S_MAT_HEIGHT, "Height / Displacement", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_properties_add_path(material, S_MAT_ENVIRONMENT, "Environment Map (equirectangular)", OBS_PATH_FILE, image_filter, nullptr);\n\tobs_property_t *quality = obs_properties_add_list(material, S_MAT_QUALITY, "Material Quality",\n\t\t\t\t\t\t\t\t OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);\n\tobs_property_list_add_int(quality, "LIVE - 56 raymarch steps", 0);\n\tobs_property_list_add_int(quality, "HIGH - 80 raymarch steps", 1);\n\tobs_property_list_add_int(quality, "ULTRA - 112 raymarch steps", 2);\n\tobs_properties_add_float_slider(material, S_MAT_NORMAL_STRENGTH, "Normal Strength", 0.0, 2.0, 0.01);\n\tobs_properties_add_float_slider(material, S_MAT_HEIGHT_STRENGTH, "Height Strength", 0.0, 1.0, 0.01);\n\tobs_properties_add_float_slider(material, S_MAT_ENV_STRENGTH, "Environment Strength", 0.0, 2.0, 0.01);\n\tobs_properties_add_float_slider(material, S_MAT_ROUGHNESS_VALUE, "Roughness", 0.02, 1.0, 0.01);\n\tobs_properties_add_float_slider(material, S_MAT_METALLIC_VALUE, "Metallic", 0.0, 1.0, 0.01);\n\tobs_properties_add_float_slider(material, S_MAT_TRIPLANAR_SCALE, "Triplanar Scale", 0.25, 12.0, 0.05);\n\tobs_properties_add_text(material, "material_help",\n\t\t\t\t"VFX Material Engine v2: optional PBR texture slots. Empty slots are safe. "\n\t\t\t\t"Environment maps are sampled as equirectangular 2D images by compatible shaders.",\n\t\t\t\tOBS_TEXT_INFO);\n\tobs_properties_add_group(props, "material_engine_v2", "VFX Material Engine v2", OBS_GROUP_NORMAL, material);\n\n\tstd::string meta_effect_path = s && !s->effect_path.empty() ? s->effect_path : default_effect_path_string();\n''',
    'material property group',
)

# -----------------------------------------------------------------------------
# Defaults
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\tobs_data_set_default_int(settings, S_BAND_COUNT, 64);\n\tobs_data_set_default_int(settings, "color1", 0xFFFFFF);\n''',
    '''\tobs_data_set_default_int(settings, S_BAND_COUNT, 64);\n\tobs_data_set_default_string(settings, S_MAT_ALBEDO, "");\n\tobs_data_set_default_string(settings, S_MAT_NORMAL, "");\n\tobs_data_set_default_string(settings, S_MAT_ROUGHNESS, "");\n\tobs_data_set_default_string(settings, S_MAT_METALLIC, "");\n\tobs_data_set_default_string(settings, S_MAT_HEIGHT, "");\n\tobs_data_set_default_string(settings, S_MAT_ENVIRONMENT, "");\n\tobs_data_set_default_int(settings, S_MAT_QUALITY, 1);\n\tobs_data_set_default_double(settings, S_MAT_NORMAL_STRENGTH, 1.0);\n\tobs_data_set_default_double(settings, S_MAT_HEIGHT_STRENGTH, 0.35);\n\tobs_data_set_default_double(settings, S_MAT_ENV_STRENGTH, 1.0);\n\tobs_data_set_default_double(settings, S_MAT_ROUGHNESS_VALUE, 0.22);\n\tobs_data_set_default_double(settings, S_MAT_METALLIC_VALUE, 1.0);\n\tobs_data_set_default_double(settings, S_MAT_TRIPLANAR_SCALE, 2.5);\n\tobs_data_set_default_int(settings, "color1", 0xFFFFFF);\n''',
    'material defaults',
)

# -----------------------------------------------------------------------------
# Update settings: material paths and numeric controls.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\ts->band_count = std::clamp<int>((int)obs_data_get_int(settings, S_BAND_COUNT), 1, 64);\n\tif (old_fft_size != s->fft_size) {\n''',
    '''\ts->band_count = std::clamp<int>((int)obs_data_get_int(settings, S_BAND_COUNT), 1, 64);\n\n\tbool material_paths_changed = false;\n\tfor (size_t i = 0; i < s->material_texture_paths.size(); ++i) {\n\t\tconst char *value = obs_data_get_string(settings, kMaterialPathKeys[i]);\n\t\tconst std::string next = value ? value : "";\n\t\tif (next != s->material_texture_paths[i]) {\n\t\t\ts->material_texture_paths[i] = next;\n\t\t\tmaterial_paths_changed = true;\n\t\t}\n\t}\n\tif (material_paths_changed)\n\t\ts->reload_material_textures = true;\n\ts->material_quality = std::clamp<int>((int)obs_data_get_int(settings, S_MAT_QUALITY), 0, 2);\n\ts->material_normal_strength = std::clamp(float(obs_data_get_double(settings, S_MAT_NORMAL_STRENGTH)), 0.0f, 2.0f);\n\ts->material_height_strength = std::clamp(float(obs_data_get_double(settings, S_MAT_HEIGHT_STRENGTH)), 0.0f, 1.0f);\n\ts->material_environment_strength = std::clamp(float(obs_data_get_double(settings, S_MAT_ENV_STRENGTH)), 0.0f, 2.0f);\n\ts->material_roughness = std::clamp(float(obs_data_get_double(settings, S_MAT_ROUGHNESS_VALUE)), 0.02f, 1.0f);\n\ts->material_metallic = std::clamp(float(obs_data_get_double(settings, S_MAT_METALLIC_VALUE)), 0.0f, 1.0f);\n\ts->material_triplanar_scale = std::clamp(float(obs_data_get_double(settings, S_MAT_TRIPLANAR_SCALE)), 0.25f, 12.0f);\n\n\tif (old_fft_size != s->fft_size) {\n''',
    'material source update',
)

# Reset beat detector completely if FFT size changes.
cpp = replace_once(
    cpp,
    '''\t\ts->previous_raw_bands.fill(0.0f);\n\t\ts->previous_kick_energy = 0.0f;\n\t}\n''',
    '''\t\ts->previous_raw_bands.fill(0.0f);\n\t\ts->previous_kick_energy = 0.0f;\n\t\ts->beat_floor = 0.0f;\n\t\ts->previous_beat_focus = 0.0f;\n\t\ts->beat_refractory = 0.0f;\n\t}\n''',
    'beat reset on FFT change',
)

# -----------------------------------------------------------------------------
# Destroy textures safely with the other GPU resources.
# -----------------------------------------------------------------------------
cpp = replace_once(
    cpp,
    '''\tdestroy_effect(s);\n\tdestroy_texrender(s);\n\tdestroy_band_texture(s);\n\tobs_leave_graphics();\n''',
    '''\tdestroy_effect(s);\n\tdestroy_texrender(s);\n\tdestroy_band_texture(s);\n\tdestroy_material_textures(s);\n\tobs_leave_graphics();\n''',
    'material source destroy',
)

# Idempotence / migration guards.
if 'VFX Material Engine v2 settings.' not in cpp:
    raise RuntimeError('material constants missing after migration')
if 'material_environment_texture' not in cpp:
    raise RuntimeError('material environment binding missing after migration')
if 'std::array<gs_image_file_t, 6>' not in hpp:
    raise RuntimeError('material images missing from header')

HPP.write_text(hpp, encoding='utf-8')
CPP.write_text(cpp, encoding='utf-8')
print('VFX Material Engine v2 migration applied successfully')
