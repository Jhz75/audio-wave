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

# Track the actually active shader separately from the selected path. This lets
# a failed compile keep the last known-good shader running.
hpp = replace_once(
    hpp,
    "\tstd::string effect_path;\n\tgs_effect_t *effect = nullptr;\n\tstd::string effect_error;\n",
    "\tstd::string effect_path;\n\tstd::string active_effect_path;\n\tgs_effect_t *effect = nullptr;\n\tstd::string effect_error;\n",
    "active shader path state",
)

# Small formatter for readable OBS property diagnostics.
needle = "static std::string trim_copy(const std::string &v)\n{\n\tsize_t a = 0;\n\twhile (a < v.size() && std::isspace((unsigned char)v[a]))\n\t\t++a;\n\tsize_t b = v.size();\n\twhile (b > a && std::isspace((unsigned char)v[b - 1]))\n\t\t--b;\n\treturn v.substr(a, b - a);\n}\n"
replacement = needle + "\nstatic std::string diagnostic_excerpt(std::string text, size_t max_chars = 700)\n{\n\tfor (char &c : text) {\n\t\tif (c == '\\r' || c == '\\n' || c == '\\t')\n\t\t\tc = ' ';\n\t}\n\twhile (text.find(\"  \") != std::string::npos)\n\t\ttext.replace(text.find(\"  \"), 2, \" \" );\n\tif (text.size() > max_chars)\n\t\ttext = text.substr(0, max_chars - 3) + \"...\";\n\treturn text;\n}\n"
cpp = replace_once(cpp, needle, replacement, "diagnostic excerpt helper")

# Compile into a temporary candidate first. Only replace the active shader when
# compilation succeeds. A broken edit therefore cannot turn a working source
# into a black frame.
old_load = '''static void load_effect_if_needed(audio_shader_source *s)
{
\tif (!s || !s->reload_effect)
\t\treturn;
\ts->reload_effect = false;
\tdestroy_effect(s);
\ts->effect_error.clear();
\tif (s->effect_path.empty()) {
\t\tBLOG(LOG_WARNING, "No .effect file selected");
\t\treturn;
\t}
\tBLOG(LOG_INFO, "Loading effect: %s", s->effect_path.c_str());
\tchar *error = nullptr;
\ts->effect = gs_effect_create_from_file(s->effect_path.c_str(), &error);
\tif (!s->effect) {
\t\ts->effect_error = error ? error : "Unknown shader compile error";
\t\tBLOG(LOG_ERROR, "Could not load effect '%s': %s", s->effect_path.c_str(), s->effect_error.c_str());
\t} else {
\t\tBLOG(LOG_INFO, "Effect loaded successfully: %s", s->effect_path.c_str());
\t}
\tif (error)
\t\tbfree(error);
}
'''
new_load = '''static void load_effect_if_needed(audio_shader_source *s)
{
\tif (!s || !s->reload_effect)
\t\treturn;
\ts->reload_effect = false;
\ts->effect_error.clear();
\tif (s->effect_path.empty()) {
\t\tBLOG(LOG_WARNING, "No .effect file selected");
\t\tdestroy_effect(s);
\t\ts->active_effect_path.clear();
\t\treturn;
\t}

\tBLOG(LOG_INFO, "Compiling effect candidate: %s", s->effect_path.c_str());
\tchar *error = nullptr;
\tgs_effect_t *candidate = gs_effect_create_from_file(s->effect_path.c_str(), &error);
\tif (!candidate) {
\t\ts->effect_error = error ? error : "Unknown shader compile error";
\t\tconst std::string excerpt = diagnostic_excerpt(s->effect_error);
\t\tif (s->effect) {
\t\t\tBLOG(LOG_ERROR,
\t\t\t     "Shader compile failed for '%s'; keeping last valid effect '%s' active. Error: %s",
\t\t\t     s->effect_path.c_str(), s->active_effect_path.c_str(), excerpt.c_str());
\t\t} else {
\t\t\tBLOG(LOG_ERROR, "Shader compile failed for '%s' and no fallback effect is available. Error: %s",
\t\t\t     s->effect_path.c_str(), excerpt.c_str());
\t\t}
\t\tif (error)
\t\t\tbfree(error);
\t\treturn;
\t}

\tdestroy_effect(s);
\ts->effect = candidate;
\ts->active_effect_path = s->effect_path;
\ts->effect_error.clear();
\ts->render_logged_ok = false;
\ts->render_logged_no_effect = false;
\ts->render_logged_no_technique = false;
\tBLOG(LOG_INFO, "Effect compiled and activated successfully: %s", s->active_effect_path.c_str());
\tif (error)
\t\tbfree(error);
}
'''
cpp = replace_once(cpp, old_load, new_load, "safe shader candidate compile")

# The render log should identify the effect actually being rendered, not a
# broken candidate path that failed to replace it.
cpp = replace_once(
    cpp,
    "\t\t\t\tBLOG(LOG_ERROR, \"Effect '%s' has no Draw, Solid, or Default technique\", s->effect_path.c_str());\n",
    "\t\t\t\tBLOG(LOG_ERROR, \"Effect '%s' has no Draw, Solid, or Default technique\", s->active_effect_path.c_str());\n",
    "active path technique log",
)
cpp = replace_once(
    cpp,
    "\t\t     obs_source_get_name(s->self), s->effect_path.c_str(), s->width, s->height, render_width, render_height,\n",
    "\t\t     obs_source_get_name(s->self), s->active_effect_path.c_str(), s->width, s->height, render_width, render_height,\n",
    "active path render log",
)

# Add an immediately visible status block when the user opens source properties.
old_ui = '''\tobs_property_set_modified_callback(effect_path, effect_path_modified);
\tobs_properties_add_button(props, "reload_shader", "\\xe2\\x86\\xba  Reload Shader", reload_effect_clicked);
\tobs_properties_add_text(props, "effect_metadata_help",
'''
new_ui = '''\tobs_property_set_modified_callback(effect_path, effect_path_modified);
\tobs_properties_add_button(props, "reload_shader", "\\xe2\\x86\\xba  Reload Shader", reload_effect_clicked);
\tstd::string shader_status;
\tif (!s) {
\t\tshader_status = "Shader status: source state unavailable";
\t} else if (!s->effect_error.empty()) {
\t\tshader_status = "Shader status: ERROR - " + diagnostic_excerpt(s->effect_error);
\t\tif (s->effect && !s->active_effect_path.empty())
\t\t\tshader_status += " | Fallback active: " + s->active_effect_path;
\t} else if (s->effect && !s->active_effect_path.empty()) {
\t\tshader_status = "Shader status: OK | Active: " + s->active_effect_path;
\t} else if (!s->effect_path.empty()) {
\t\tshader_status = "Shader status: waiting for first compile/reload";
\t} else {
\t\tshader_status = "Shader status: no effect selected";
\t}
\tobs_properties_add_text(props, "shader_status", shader_status.c_str(), OBS_TEXT_INFO);
\tobs_properties_add_text(props, "effect_metadata_help",
'''
cpp = replace_once(cpp, old_ui, new_ui, "shader status properties")

CPP.write_text(cpp, encoding="utf-8")
HPP.write_text(hpp, encoding="utf-8")
print("Applied shader diagnostics / last-known-good fallback changes")
