#include "includes/audio-shader-source.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>

#include <util/platform.h>

#define BLOG(level, fmt, ...) blog(level, "[audio-shader-engine] " fmt, ##__VA_ARGS__)

static const char *kSourceId = "audio_shader_engine_source";
static const char *kSourceName = "Audio Shader Engine";

static const char *S_AUDIO_SOURCE = "audio_source";
static const char *S_WIDTH = "width";
static const char *S_HEIGHT = "height";
static const char *S_USE_OBS_CANVAS = "use_obs_canvas";
static const char *S_RENDER_SCALE = "render_scale";
static const char *S_EFFECT_PATH = "effect_path";
static const char *S_REACT_DB = "react_db";
static const char *S_PEAK_DB = "peak_db";
static const char *S_ATTACK_MS = "attack_ms";
static const char *S_RELEASE_MS = "release_ms";
static const char *S_FFT_SIZE = "fft_size";
static const char *S_BAND_COUNT = "band_count";
static const char *S_OPTION_PREFIX = "option";
static const char *S_COLOR_PREFIX = "color";

static constexpr float PI_F = 3.14159265358979323846f;
static obs_source_info g_source_info = {};

static inline float clamp01(float v)
{
	return std::max(0.0f, std::min(1.0f, v));
}

static inline float amp_to_db(float amp)
{
	amp = std::max(amp, 0.000001f);
	return 20.0f * std::log10(amp);
}

static inline float db_to_norm(float db, float react_db, float peak_db)
{
	if (peak_db <= react_db)
		return 0.0f;
	return clamp01((db - react_db) / (peak_db - react_db));
}

static inline float hash01(float n)
{
	float h = std::fmod(std::sin(n) * 43758.5453123f, 1.0f);
	return h < 0.0f ? h + 1.0f : h;
}

static inline int clamp_pow2(int value, int min_value, int max_value)
{
	value = std::clamp(value, min_value, max_value);
	int p = 1;
	while (p < value)
		p <<= 1;
	const int lower = p >> 1;
	if (lower < min_value)
		return p;
	if (p > max_value)
		return lower;
	return (value - lower) < (p - value) ? lower : p;
}

static bool is_pow2(size_t n)
{
	return n >= 2 && (n & (n - 1)) == 0;
}

static void fft_inplace(std::vector<std::complex<float>> &a)
{
	const size_t n = a.size();
	if (!is_pow2(n))
		return;

	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}

	for (size_t len = 2; len <= n; len <<= 1) {
		const float ang = -2.0f * PI_F / float(len);
		const std::complex<float> wlen(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			std::complex<float> w(1.0f, 0.0f);
			for (size_t j = 0; j < len / 2; ++j) {
				const std::complex<float> u = a[i + j];
				const std::complex<float> v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wlen;
			}
		}
	}
}

static void ensure_analysis_buffers(audio_shader_source *s, size_t n)
{
	if (!s || n < 2)
		return;
	if (s->analysis_fft_size == int(n) && s->fft_snapshot.size() == n && s->hann_window.size() == n &&
	    s->fft_work.size() == n)
		return;

	s->fft_snapshot.resize(n);
	s->hann_window.resize(n);
	s->fft_work.resize(n);
	for (size_t i = 0; i < n; ++i)
		s->hann_window[i] = 0.5f - 0.5f * std::cos(2.0f * PI_F * float(i) / float(n - 1));
	s->analysis_fft_size = int(n);
	BLOG(LOG_INFO, "Prepared reusable FFT analysis buffers for %zu samples", n);
}

static void color_to_vec4(uint32_t color, vec4 *out)
{
	const float r = float(color & 0xFFu) / 255.0f;
	const float g = float((color >> 8) & 0xFFu) / 255.0f;
	const float b = float((color >> 16) & 0xFFu) / 255.0f;
	vec4_set(out, r, g, b, 1.0f);
}

static std::string trim_copy(const std::string &v)
{
	size_t a = 0;
	while (a < v.size() && std::isspace((unsigned char)v[a]))
		++a;
	size_t b = v.size();
	while (b > a && std::isspace((unsigned char)v[b - 1]))
		--b;
	return v.substr(a, b - a);
}

static std::string diagnostic_excerpt(std::string text, size_t max_chars = 700)
{
	for (char &c : text) {
		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';
	}
	while (text.find("  ") != std::string::npos)
		text.replace(text.find("  "), 2, " " );
	if (text.size() > max_chars)
		text = text.substr(0, max_chars - 3) + "...";
	return text;
}

struct effect_metadata {
	std::string name;
	std::array<std::string, 16> option_labels{};
	std::array<std::string, 8> color_labels{};
	std::array<float, 16> option_defaults{};
	std::array<bool, 16> option_default_set{};
	std::array<uint32_t, 8> color_defaults{};
	std::array<bool, 8> color_default_set{};
	bool use_obs_canvas_set = false;
	bool use_obs_canvas = false;
	bool width_set = false;
	int width = 0;
	bool height_set = false;
	int height = 0;
	bool render_scale_set = false;
	int render_scale = 100;
	bool react_db_set = false;
	float react_db = -82.0f;
	bool peak_db_set = false;
	float peak_db = -28.0f;
	bool attack_ms_set = false;
	int attack_ms = 14;
	bool release_ms_set = false;
	int release_ms = 140;
	bool fft_size_set = false;
	int fft_size = 4096;
	bool band_count_set = false;
	int band_count = 64;
};

static bool parse_bool_value(std::string value, bool *out)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	if (value == "1" || value == "true" || value == "yes" || value == "on") {
		*out = true;
		return true;
	}
	if (value == "0" || value == "false" || value == "no" || value == "off") {
		*out = false;
		return true;
	}
	return false;
}

static bool parse_hex_rgb(std::string value, uint32_t *out)
{
	if (!value.empty() && value[0] == '#')
		value.erase(0, 1);
	if (value.size() != 6)
		return false;
	char *end = nullptr;
	const unsigned long rgb = std::strtoul(value.c_str(), &end, 16);
	if (!end || *end != '\0')
		return false;
	const uint32_t r = (uint32_t(rgb) >> 16) & 0xFFu;
	const uint32_t g = (uint32_t(rgb) >> 8) & 0xFFu;
	const uint32_t b = uint32_t(rgb) & 0xFFu;
	*out = r | (g << 8) | (b << 16);
	return true;
}

static std::string default_effect_path_string()
{
	char *path = obs_module_file("effects/pulse-ring.effect");
	if (!path)
		return {};
	std::string result = path;
	bfree(path);
	return result;
}

static effect_metadata load_effect_metadata(const std::string &effect_path)
{
	effect_metadata meta;
	if (effect_path.empty())
		return meta;

	const std::string ini_path = effect_path + ".ini";
	std::ifstream file(ini_path);
	if (!file.is_open())
		return meta;

	std::string section;
	std::string line;
	while (std::getline(file, line)) {
		line = trim_copy(line);
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line.front() == '[' && line.back() == ']') {
			section = trim_copy(line.substr(1, line.size() - 2));
			std::transform(section.begin(), section.end(), section.begin(),
				       [](unsigned char c) { return (char)std::tolower(c); });
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = trim_copy(line.substr(0, eq));
		std::string value = trim_copy(line.substr(eq + 1));
		if (value.empty())
			continue;

		if (section == "effect" && key == "name") {
			meta.name = value;
		} else if (section == "options" && key.rfind("option", 0) == 0) {
			const int idx = std::atoi(key.c_str() + 6);
			if (idx >= 1 && idx <= 16 && value.rfind("Custom Option", 0) != 0)
				meta.option_labels[(size_t)idx - 1] = value;
		} else if (section == "colors" && key.rfind("color", 0) == 0) {
			const int idx = std::atoi(key.c_str() + 5);
			if (idx >= 1 && idx <= 8)
				meta.color_labels[(size_t)idx - 1] = value;
		} else if (section == "defaults") {
			if (key.rfind("option", 0) == 0) {
				const int idx = std::atoi(key.c_str() + 6);
				if (idx >= 1 && idx <= 16) {
					meta.option_defaults[(size_t)idx - 1] = std::clamp((float)std::atof(value.c_str()), 0.0f, 1.0f);
					meta.option_default_set[(size_t)idx - 1] = true;
				}
			} else if (key.rfind("color", 0) == 0) {
				const int idx = std::atoi(key.c_str() + 5);
				uint32_t color = 0;
				if (idx >= 1 && idx <= 8 && parse_hex_rgb(value, &color)) {
					meta.color_defaults[(size_t)idx - 1] = color;
					meta.color_default_set[(size_t)idx - 1] = true;
				}
			} else if (key == "use_obs_canvas") {
				meta.use_obs_canvas_set = parse_bool_value(value, &meta.use_obs_canvas);
			} else if (key == "width") {
				meta.width = std::atoi(value.c_str());
				meta.width_set = true;
			} else if (key == "height") {
				meta.height = std::atoi(value.c_str());
				meta.height_set = true;
			} else if (key == "render_scale") {
				meta.render_scale = std::atoi(value.c_str());
				meta.render_scale_set = true;
			} else if (key == "react_db") {
				meta.react_db = (float)std::atof(value.c_str());
				meta.react_db_set = true;
			} else if (key == "peak_db") {
				meta.peak_db = (float)std::atof(value.c_str());
				meta.peak_db_set = true;
			} else if (key == "attack_ms") {
				meta.attack_ms = std::atoi(value.c_str());
				meta.attack_ms_set = true;
			} else if (key == "release_ms") {
				meta.release_ms = std::atoi(value.c_str());
				meta.release_ms_set = true;
			} else if (key == "fft_size") {
				meta.fft_size = std::atoi(value.c_str());
				meta.fft_size_set = true;
			} else if (key == "band_count") {
				meta.band_count = std::atoi(value.c_str());
				meta.band_count_set = true;
			}
		}
	}
	return meta;
}

static bool apply_effect_defaults(obs_data_t *settings, const effect_metadata &meta)
{
	bool applied = false;
	if (meta.use_obs_canvas_set) { obs_data_set_bool(settings, S_USE_OBS_CANVAS, meta.use_obs_canvas); applied = true; }
	if (meta.width_set) { obs_data_set_int(settings, S_WIDTH, std::clamp(meta.width, 16, 8192)); applied = true; }
	if (meta.height_set) { obs_data_set_int(settings, S_HEIGHT, std::clamp(meta.height, 16, 8192)); applied = true; }
	if (meta.render_scale_set) {
		const int scale = (meta.render_scale == 25 || meta.render_scale == 50 || meta.render_scale == 75 || meta.render_scale == 100)
				  ? meta.render_scale
				  : 100;
		obs_data_set_int(settings, S_RENDER_SCALE, scale);
		applied = true;
	}
	if (meta.react_db_set) { obs_data_set_double(settings, S_REACT_DB, std::clamp(meta.react_db, -90.0f, -1.0f)); applied = true; }
	if (meta.peak_db_set) { obs_data_set_double(settings, S_PEAK_DB, std::clamp(meta.peak_db, -60.0f, 0.0f)); applied = true; }
	if (meta.attack_ms_set) { obs_data_set_int(settings, S_ATTACK_MS, std::clamp(meta.attack_ms, 0, 500)); applied = true; }
	if (meta.release_ms_set) { obs_data_set_int(settings, S_RELEASE_MS, std::clamp(meta.release_ms, 0, 2000)); applied = true; }
	if (meta.fft_size_set) { obs_data_set_int(settings, S_FFT_SIZE, clamp_pow2(meta.fft_size, 512, 8192)); applied = true; }
	if (meta.band_count_set) { obs_data_set_int(settings, S_BAND_COUNT, std::clamp(meta.band_count, 8, 64)); applied = true; }
	for (int i = 1; i <= 16; ++i) {
		if (!meta.option_default_set[(size_t)i - 1])
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_OPTION_PREFIX, i);
		obs_data_set_double(settings, key, meta.option_defaults[(size_t)i - 1]);
		applied = true;
	}
	for (int i = 1; i <= 8; ++i) {
		if (!meta.color_default_set[(size_t)i - 1])
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_COLOR_PREFIX, i);
		obs_data_set_int(settings, key, meta.color_defaults[(size_t)i - 1]);
		applied = true;
	}
	return applied;
}

static void rebuild_effect_controls(obs_properties_t *props, const std::string &effect_path)
{
	if (!props)
		return;
	obs_properties_remove_by_name(props, "shader_options");

	effect_metadata meta = load_effect_metadata(effect_path);
	obs_properties_t *shader_opts = obs_properties_create();
	bool any_control = false;
	for (int i = 1; i <= 16; ++i) {
		const std::string &label = meta.option_labels[(size_t)i - 1];
		if (label.empty())
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_OPTION_PREFIX, i);
		obs_properties_add_float_slider(shader_opts, key, label.c_str(), 0.0, 1.0, 0.001);
		any_control = true;
	}
	for (int i = 1; i <= 8; ++i) {
		const std::string &label = meta.color_labels[(size_t)i - 1];
		if (label.empty())
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_COLOR_PREFIX, i);
		obs_properties_add_color(shader_opts, key, label.c_str());
		any_control = true;
	}
	if (!any_control) {
		obs_properties_add_text(shader_opts, "no_effect_controls",
					"This effect has no named controls. Add a matching .effect.ini file to expose sliders/colors.",
					OBS_TEXT_INFO);
	}
	std::string group_name = meta.name.empty() ? "Effect Controls" : (meta.name + " Controls");
	obs_properties_add_group(props, "shader_options", group_name.c_str(), OBS_GROUP_NORMAL, shader_opts);
}

static void get_obs_canvas_size(uint32_t *width, uint32_t *height)
{
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		*width = ovi.base_width;
		*height = ovi.base_height;
		return;
	}
	*width = 1920;
	*height = 1080;
}

static uint32_t valid_dimension(int64_t value, uint32_t fallback)
{
	if (value < 16)
		return fallback;
	if (value > 8192)
		return 8192;
	return static_cast<uint32_t>(value);
}

static int valid_render_scale(int value)
{
	switch (value) {
	case 25:
	case 50:
	case 75:
	case 100:
		return value;
	default:
		return 100;
	}
}

static uint32_t scaled_render_dimension(uint32_t output_dimension, int render_scale_percent)
{
	const uint64_t scaled = (uint64_t(output_dimension) * uint64_t(render_scale_percent) + 50u) / 100u;
	return std::clamp<uint32_t>(uint32_t(scaled), 16u, 8192u);
}

static void set_source_dimensions(audio_shader_source *s, uint32_t width, uint32_t height)
{
	if (!s)
		return;
	width = std::clamp<uint32_t>(width, 16u, 8192u);
	height = std::clamp<uint32_t>(height, 16u, 8192u);
	if (s->width == width && s->height == height)
		return;
	s->width = width;
	s->height = height;
	s->render_logged_ok = false;
	BLOG(LOG_INFO, "Source canvas size changed to %ux%u", s->width, s->height);
}

static void release_audio_weak(audio_shader_source *s)
{
	if (!s || !s->audio_weak)
		return;
	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

static void audio_capture_cb(void *param, obs_source_t *, const audio_data *audio, bool muted)
{
	auto *s = static_cast<audio_shader_source *>(param);
	if (!s || !audio || !s->alive.load(std::memory_order_acquire))
		return;
	s->audio_cb_inflight.fetch_add(1, std::memory_order_acq_rel);

	if (muted || audio->frames == 0 || !audio->data[0]) {
		if (muted && audio->frames > 0) {
			std::lock_guard<std::mutex> lock(s->audio_mutex);
			const size_t n = s->mono_ring.size();
			if (n > 0) {
				const size_t fill = std::min(static_cast<size_t>(audio->frames), n);
				for (size_t i = 0; i < fill; ++i) {
					s->mono_ring[s->mono_pos] = 0.0f;
					s->mono_pos = (s->mono_pos + 1) % n;
					if (s->mono_count < n)
						++s->mono_count;
				}
			}
			s->raw_level = 0.0f;
			s->raw_peak = 0.0f;
		}
		s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
		return;
	}

	const size_t frames = audio->frames;
	const float *left = reinterpret_cast<const float *>(audio->data[0]);
	const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;
	float sum_sq = 0.0f;
	float peak = 0.0f;

	std::lock_guard<std::mutex> lock(s->audio_mutex);
	if ((int)s->mono_ring.size() != s->fft_size) {
		s->mono_ring.assign((size_t)s->fft_size, 0.0f);
		s->mono_pos = 0;
		s->mono_count = 0;
	}
	for (size_t i = 0; i < frames; ++i) {
		const float l = left[i];
		const float r = right ? right[i] : l;
		const float mono = 0.5f * (l + r);
		sum_sq += mono * mono;
		peak = std::max(peak, std::fabs(mono));
		s->mono_ring[s->mono_pos] = mono;
		s->mono_pos = (s->mono_pos + 1) % s->mono_ring.size();
		if (s->mono_count < s->mono_ring.size())
			++s->mono_count;
	}
	s->raw_level = std::sqrt(sum_sq / std::max<size_t>(1, frames));
	s->raw_peak = peak;
	s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
}

static void detach_audio(audio_shader_source *s)
{
	if (!s || !s->audio_weak)
		return;
	obs_source_t *target = obs_weak_source_get_source(s->audio_weak);
	if (target) {
		obs_source_remove_audio_capture_callback(target, audio_capture_cb, s);
		obs_source_release(target);
	}
	release_audio_weak(s);
}

static void attach_audio(audio_shader_source *s)
{
	if (!s || s->audio_source_name.empty())
		return;
	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		BLOG(LOG_WARNING, "Audio source '%s' not found", s->audio_source_name.c_str());
		return;
	}
	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);
	obs_source_release(target);
}

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = static_cast<obs_property_t *>(data);
	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, kSourceId) == 0)
		return true;
	if (!obs_source_audio_active(source))
		return true;
	const char *name = obs_source_get_name(source);
	if (name)
		obs_property_list_add_string(prop, name, name);
	return true;
}

static void calculate_audio_state(audio_shader_source *s)
{
	const size_t n = (size_t)std::clamp(s->fft_size, 512, 8192);
	ensure_analysis_buffers(s, n);

	float raw_level = 0.0f;
	float raw_peak = 0.0f;
	size_t pos = 0;
	size_t count = 0;
	{
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		raw_level = s->raw_level;
		raw_peak = s->raw_peak;
		pos = s->mono_pos;
		count = s->mono_count;
		if (s->mono_ring.size() == n) {
			for (size_t i = 0; i < n; ++i)
				s->fft_snapshot[i] = s->mono_ring[(pos + i) % n];
		} else {
			count = 0;
		}
	}

	const float target_level = db_to_norm(amp_to_db(raw_level), s->react_db, s->peak_db);
	const float target_peak = db_to_norm(amp_to_db(raw_peak), s->react_db, s->peak_db);
	const uint64_t now = os_gettime_ns();
	float dt = 1.0f / 60.0f;
	if (s->last_ts_ns != 0 && now > s->last_ts_ns)
		dt = float(double(now - s->last_ts_ns) / 1000000000.0);
	dt = std::clamp(dt, 0.0001f, 0.25f);
	s->last_ts_ns = now;

	auto smooth = [dt](float current, float target, float attack_ms, float release_ms) {
		const float tau = (target > current ? attack_ms : release_ms) / 1000.0f;
		if (tau <= 0.000001f)
			return target;
		const float a = 1.0f - std::exp(-dt / tau);
		return current + (target - current) * a;
	};

	s->level = clamp01(smooth(s->level, target_level, s->attack_ms, s->release_ms));
	s->peak = clamp01(smooth(s->peak, target_peak, s->attack_ms * 0.5f, s->release_ms * 1.5f));

	if (count < n / 2) {
		s->bass = clamp01(smooth(s->bass, 0.0f, s->attack_ms, s->release_ms));
		s->mid = clamp01(smooth(s->mid, 0.0f, s->attack_ms, s->release_ms));
		s->treble = clamp01(smooth(s->treble, 0.0f, s->attack_ms, s->release_ms));
		s->sub = clamp01(smooth(s->sub, 0.0f, s->attack_ms, s->release_ms));
		s->low = clamp01(smooth(s->low, 0.0f, s->attack_ms, s->release_ms));
		s->low_mid = clamp01(smooth(s->low_mid, 0.0f, s->attack_ms, s->release_ms));
		s->mid_vfx = clamp01(smooth(s->mid_vfx, 0.0f, s->attack_ms, s->release_ms));
		s->high_mid = clamp01(smooth(s->high_mid, 0.0f, s->attack_ms, s->release_ms));
		s->high = clamp01(smooth(s->high, 0.0f, s->attack_ms, s->release_ms));
		s->transient = clamp01(smooth(s->transient, 0.0f, 3.0f, 80.0f));
		s->kick = clamp01(smooth(s->kick, 0.0f, 4.0f, 110.0f));
		for (float &band : s->bands)
			band = clamp01(smooth(band, 0.0f, s->attack_ms, s->release_ms));
		return;
	}

	for (size_t i = 0; i < n; ++i)
		s->fft_work[i] = std::complex<float>(s->fft_snapshot[i] * s->hann_window[i], 0.0f);
	fft_inplace(s->fft_work);

	std::array<float, 64> raw_bands{};
	const int usable_bins = int(n / 2);
	const int bands = std::clamp(s->band_count, 1, 64);
	for (int b = 0; b < bands; ++b) {
		const float t0 = float(b) / float(bands);
		const float t1 = float(b + 1) / float(bands);
		const int bin0 = std::max(1, int(std::pow(t0, 2.0f) * usable_bins));
		const int bin1 = std::max(bin0 + 1, int(std::pow(t1, 2.0f) * usable_bins));
		float mag = 0.0f;
		int c = 0;
		for (int bin = bin0; bin < std::min(bin1, usable_bins); ++bin) {
			mag += std::abs(s->fft_work[(size_t)bin]);
			++c;
		}
		mag = c > 0 ? mag / float(c) : 0.0f;
		raw_bands[(size_t)b] = db_to_norm(amp_to_db(mag / float(n)), s->react_db, s->peak_db);
	}

	auto avg_raw_range = [&](int a, int b) {
		float sum = 0.0f;
		int c = 0;
		for (int i = std::max(0, a); i < b && i < bands; ++i) {
			sum += raw_bands[(size_t)i];
			++c;
		}
		return c ? sum / float(c) : 0.0f;
	};

	const float raw_bass = avg_raw_range(0, std::max(1, bands / 4));
	const float raw_mid = avg_raw_range(std::max(1, bands / 4), std::max(2, bands * 2 / 3));
	const float raw_treble = avg_raw_range(std::max(2, bands * 2 / 3), bands);
	s->bass = clamp01(smooth(s->bass, raw_bass, s->attack_ms, s->release_ms));
	s->mid = clamp01(smooth(s->mid, raw_mid, s->attack_ms, s->release_ms));
	s->treble = clamp01(smooth(s->treble, raw_treble, s->attack_ms, s->release_ms));

	auto hz_db = [&](float hz0, float hz1) {
		const float sample_rate = float(std::max(1, s->sample_rate));
		const float nyquist = sample_rate * 0.5f;
		hz0 = std::clamp(hz0, 0.0f, nyquist);
		hz1 = std::clamp(hz1, hz0, nyquist);
		const int bin0 = std::max(1, int(std::floor(hz0 * float(n) / sample_rate)));
		const int bin1 = std::min(usable_bins, int(std::ceil(hz1 * float(n) / sample_rate)));
		if (bin1 <= bin0)
			return -120.0f;
		float mag = 0.0f;
		for (int bin = bin0; bin < bin1; ++bin)
			mag += std::abs(s->fft_work[(size_t)bin]);
		mag /= float(bin1 - bin0);
		return amp_to_db(mag / float(n));
	};

	// VFX v1.1 musical calibration. Corrections are applied in dB before
	// normalization so silence remains exactly at zero and the OBS React/Peak
	// window keeps its expected meaning.
	const float sub_db = hz_db(20.0f, 60.0f);
	const float low_db = hz_db(60.0f, 150.0f);
	const float low_mid_db = hz_db(150.0f, 500.0f);
	const float mid_vfx_db = hz_db(500.0f, 2000.0f);
	const float high_mid_db = hz_db(2000.0f, 6000.0f);
	const float high_db = hz_db(6000.0f, 16000.0f);

	const float raw_sub = db_to_norm(sub_db - 1.0f, s->react_db, s->peak_db);
	const float raw_low = db_to_norm(low_db - 2.0f, s->react_db, s->peak_db);
	const float raw_low_mid = db_to_norm(low_mid_db, s->react_db, s->peak_db);
	const float raw_mid_vfx = db_to_norm(mid_vfx_db + 1.0f, s->react_db, s->peak_db);
	const float raw_high_mid = db_to_norm(high_mid_db + 2.0f, s->react_db, s->peak_db);
	const float raw_high = db_to_norm(high_db + 3.0f, s->react_db, s->peak_db);

	s->sub = clamp01(smooth(s->sub, raw_sub, s->attack_ms, s->release_ms));
	s->low = clamp01(smooth(s->low, raw_low, s->attack_ms, s->release_ms));
	s->low_mid = clamp01(smooth(s->low_mid, raw_low_mid, s->attack_ms, s->release_ms));
	s->mid_vfx = clamp01(smooth(s->mid_vfx, raw_mid_vfx, s->attack_ms, s->release_ms));
	s->high_mid = clamp01(smooth(s->high_mid, raw_high_mid, s->attack_ms, s->release_ms));
	s->high = clamp01(smooth(s->high, raw_high, s->attack_ms, s->release_ms));

	float spectral_flux = 0.0f;
	for (int b = 0; b < bands; ++b) {
		spectral_flux += std::max(0.0f, raw_bands[(size_t)b] - s->previous_raw_bands[(size_t)b]);
		s->previous_raw_bands[(size_t)b] = raw_bands[(size_t)b];
	}
	spectral_flux = bands > 0 ? spectral_flux / float(bands) : 0.0f;
	const float transient_target = clamp01(spectral_flux * 6.0f);
	s->transient = clamp01(smooth(s->transient, transient_target, 3.0f, 80.0f));

	// Keep kick detection on the uncalibrated SUB/LOW response so VFX band
	// balancing does not change the detector behaviour validated in v1.
	const float kick_sub = db_to_norm(sub_db, s->react_db, s->peak_db);
	const float kick_low = db_to_norm(low_db, s->react_db, s->peak_db);
	const float kick_energy = clamp01(kick_sub * 0.45f + kick_low * 0.55f);
	const float kick_rise = std::max(0.0f, kick_energy - s->previous_kick_energy);
	s->previous_kick_energy = kick_energy;
	const float kick_target = clamp01(kick_rise * 3.5f + kick_energy * 0.18f);
	s->kick = clamp01(smooth(s->kick, kick_target, 4.0f, 110.0f));

	std::array<float, 64> target_cells{};
	std::array<bool, 64> used_bands{};
	const float time_bucket = std::floor(float(now / 1000000000.0) * 3.0f);
	const int peak_slots = std::min(14, bands);
	for (int slot = 0; slot < peak_slots; ++slot) {
		int best = -1;
		float best_score = 0.0f;
		for (int b = 0; b < bands; ++b) {
			if (used_bands[(size_t)b])
				continue;
			const float left = raw_bands[(size_t)std::max(0, b - 1)];
			const float right = raw_bands[(size_t)std::min(bands - 1, b + 1)];
			const float local_contrast = std::max(0.0f, raw_bands[(size_t)b] - (left + right) * 0.35f);
			const float score = raw_bands[(size_t)b] * 0.70f + local_contrast * 0.85f;
			if (score > best_score) {
				best_score = score;
				best = b;
			}
		}
		if (best < 0 || best_score <= 0.001f)
			break;

		used_bands[(size_t)best] = true;
		if (best > 0)
			used_bands[(size_t)best - 1] = true;
		if (best + 1 < bands)
			used_bands[(size_t)best + 1] = true;

		const float h = hash01(float(best) * 19.731f + float(slot) * 7.113f + time_bucket * 0.173f);
		const int center = std::clamp((int)std::floor(h * 64.0f), 0, 63);
		const float gain = 0.72f + hash01(float(best) * 5.371f + float(slot) * 31.91f) * 0.55f;
		const float amp = clamp01(raw_bands[(size_t)best] * gain * (0.65f + s->peak * 0.55f));
		const int radius = 2 + (hash01(float(best) * 11.17f + float(slot) * 3.31f) > 0.62f ? 1 : 0);
		for (int off = -radius; off <= radius; ++off) {
			int idx = center + off;
			while (idx < 0)
				idx += 64;
			while (idx >= 64)
				idx -= 64;
			const float d = std::fabs(float(off));
			float falloff = 1.0f;
			if (d >= 1.0f)
				falloff = d < 2.0f ? 0.52f : (d < 3.0f ? 0.24f : 0.10f);
			target_cells[(size_t)idx] = std::max(target_cells[(size_t)idx], amp * falloff);
		}
	}

	const float floor_energy = s->level * 0.025f;
	for (size_t i = 0; i < s->bands.size(); ++i) {
		const float target = clamp01(std::max(target_cells[i], floor_energy));
		s->bands[i] = clamp01(smooth(s->bands[i], target, s->attack_ms, s->release_ms));
	}
}

static void destroy_effect(audio_shader_source *s)
{
	if (s && s->effect) {
		gs_effect_destroy(s->effect);
		s->effect = nullptr;
	}
}

static void destroy_texrender(audio_shader_source *s)
{
	if (s && s->texrender) {
		gs_texrender_destroy(s->texrender);
		s->texrender = nullptr;
	}
}

static void destroy_band_texture(audio_shader_source *s)
{
	if (s && s->band_texture) {
		gs_texture_destroy(s->band_texture);
		s->band_texture = nullptr;
	}
}

static void load_effect_if_needed(audio_shader_source *s)
{
	if (!s || !s->reload_effect)
		return;
	s->reload_effect = false;
	s->effect_error.clear();
	if (s->effect_path.empty()) {
		BLOG(LOG_WARNING, "No .effect file selected");
		destroy_effect(s);
		s->active_effect_path.clear();
		return;
	}

	BLOG(LOG_INFO, "Compiling effect candidate: %s", s->effect_path.c_str());
	char *error = nullptr;
	gs_effect_t *candidate = gs_effect_create_from_file(s->effect_path.c_str(), &error);
	if (!candidate) {
		s->effect_error = error ? error : "Unknown shader compile error";
		const std::string excerpt = diagnostic_excerpt(s->effect_error);
		if (s->effect) {
			BLOG(LOG_ERROR,
			     "Shader compile failed for '%s'; keeping last valid effect '%s' active. Error: %s",
			     s->effect_path.c_str(), s->active_effect_path.c_str(), excerpt.c_str());
		} else {
			BLOG(LOG_ERROR, "Shader compile failed for '%s' and no fallback effect is available. Error: %s",
			     s->effect_path.c_str(), excerpt.c_str());
		}
		if (error)
			bfree(error);
		return;
	}

	destroy_effect(s);
	s->effect = candidate;
	s->active_effect_path = s->effect_path;
	s->effect_error.clear();
	s->render_logged_ok = false;
	s->render_logged_no_effect = false;
	s->render_logged_no_technique = false;
	BLOG(LOG_INFO, "Effect compiled and activated successfully: %s", s->active_effect_path.c_str());
	if (error)
		bfree(error);
}

static void set_float_param(gs_effect_t *effect, const char *name, float value)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name))
		gs_effect_set_float(p, value);
}

static void set_vec2_param(gs_effect_t *effect, const char *name, float x, float y)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name)) {
		vec2 v;
		vec2_set(&v, x, y);
		gs_effect_set_vec2(p, &v);
	}
}

static void set_color_param(gs_effect_t *effect, const char *name, uint32_t color)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name)) {
		vec4 v;
		color_to_vec4(color, &v);
		gs_effect_set_vec4(p, &v);
	}
}

static void update_band_texture(audio_shader_source *s)
{
	if (!s)
		return;
	for (size_t i = 0; i < s->bands.size(); ++i) {
		const size_t px = i * 4;
		s->band_texture_pixels[px + 0] = uint8_t(clamp01(s->bands[i]) * 255.0f + 0.5f);
		s->band_texture_pixels[px + 1] = uint8_t(clamp01(s->bass) * 255.0f + 0.5f);
		s->band_texture_pixels[px + 2] = uint8_t(clamp01(s->mid) * 255.0f + 0.5f);
		s->band_texture_pixels[px + 3] = uint8_t(clamp01(s->treble) * 255.0f + 0.5f);
	}
	if (!s->band_texture) {
		const uint8_t *data[] = {s->band_texture_pixels.data()};
		s->band_texture = gs_texture_create(64, 1, GS_RGBA, 1, data, GS_DYNAMIC);
		if (!s->band_texture) {
			BLOG(LOG_ERROR, "Failed to create FFT band texture for source '%s'", obs_source_get_name(s->self));
			return;
		}
	} else {
		gs_texture_set_image(s->band_texture, s->band_texture_pixels.data(), 64 * 4, false);
	}
}

static void set_texture_param(gs_effect_t *effect, const char *name, gs_texture_t *texture)
{
	if (!texture)
		return;
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name))
		gs_effect_set_texture(p, texture);
}

static void set_shader_params(audio_shader_source *s, uint32_t render_width, uint32_t render_height)
{
	gs_effect_t *e = s->effect;
	if (!e)
		return;
	set_vec2_param(e, "source_size", float(s->width), float(s->height));
	set_vec2_param(e, "resolution", float(render_width), float(render_height));
	set_float_param(e, "render_scale", float(s->render_scale_percent) / 100.0f);
	set_float_param(e, "time", float(os_gettime_ns() / 1000000000.0));
	set_float_param(e, "audio_level", s->level);
	set_float_param(e, "audio_peak", s->peak);
	set_float_param(e, "audio_bass", s->bass);
	set_float_param(e, "audio_mid", s->mid);
	set_float_param(e, "audio_treble", s->treble);
	set_float_param(e, "audio_sub", s->sub);
	set_float_param(e, "audio_low", s->low);
	set_float_param(e, "audio_low_mid", s->low_mid);
	set_float_param(e, "audio_mid_vfx", s->mid_vfx);
	set_float_param(e, "audio_high_mid", s->high_mid);
	set_float_param(e, "audio_high", s->high);
	set_float_param(e, "audio_transient", s->transient);
	set_float_param(e, "audio_kick", s->kick);

	// VFX v1.5 musical macro-uniforms. These are genre-neutral building blocks
	// tuned for dense electronic music (shranz / hardgroove / industrial / bochka).
	// The eight calibrated source signals remain independently available.
	//
	// BODY: low-frequency mass without letting SUB dominate everything.
	// MOTION: material/shape movement centered on low-mids and mids.
	// DETAIL: upper-spectrum surface activity.
	// IMPACT: short-event detector, favouring kick but retaining non-kick transients.
	// ENERGY: broad sustained musical density.
	// PRESSURE: heavy low-end force useful for compression/expansion.
	// GROOVE: rhythmic movement that remains useful on hardgroove as well as shranz.
	// TEXTURE: metallic/noisy high-frequency content useful for industrial material detail.
	const float audio_body = clamp01(s->sub * 0.18f + s->low * 0.44f + s->low_mid * 0.38f);
	const float audio_motion = clamp01(s->low_mid * 0.32f + s->mid_vfx * 0.46f + s->high_mid * 0.22f);
	const float audio_detail = clamp01(s->high_mid * 0.52f + s->high * 0.48f);
	const float audio_impact = clamp01(std::max(s->kick * 1.15f, s->transient));
	const float audio_energy = clamp01(s->sub * 0.07f + s->low * 0.18f + s->low_mid * 0.22f +
					 s->mid_vfx * 0.23f + s->high_mid * 0.18f + s->high * 0.12f);
	const float audio_pressure = clamp01(s->sub * 0.30f + s->low * 0.50f + s->kick * 0.20f);
	const float audio_groove = clamp01(s->kick * 0.34f + s->low_mid * 0.28f + s->mid_vfx * 0.26f +
					 s->transient * 0.12f);
	const float audio_texture = clamp01(s->high_mid * 0.42f + s->high * 0.38f + s->transient * 0.20f);
	set_float_param(e, "audio_body", audio_body);
	set_float_param(e, "audio_motion", audio_motion);
	set_float_param(e, "audio_detail", audio_detail);
	set_float_param(e, "audio_impact", audio_impact);
	set_float_param(e, "audio_energy", audio_energy);
	set_float_param(e, "audio_pressure", audio_pressure);
	set_float_param(e, "audio_groove", audio_groove);
	set_float_param(e, "audio_texture", audio_texture);
	set_float_param(e, "band_count", float(s->band_count));
	set_texture_param(e, "audio_band_texture", s->band_texture);
	set_texture_param(e, "audio_spectrum_texture", s->band_texture);

	for (size_t i = 0; i < s->options.size(); ++i) {
		char name[32];
		snprintf(name, sizeof(name), "option%zu", i + 1);
		set_float_param(e, name, s->options[i]);
	}
	for (size_t i = 0; i < s->colors.size(); ++i) {
		char name[32];
		snprintf(name, sizeof(name), "color%zu", i + 1);
		set_color_param(e, name, s->colors[i]);
	}
}

static void draw_fullscreen_quad(uint32_t width, uint32_t height)
{
	gs_draw_sprite(nullptr, 0, width, height);
}

static void source_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->render_mutex);
	if (!s->alive.load(std::memory_order_acquire))
		return;
	if (!s->texrender) {
		s->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		if (!s->texrender) {
			BLOG(LOG_ERROR, "Failed to create texrender for source '%s'", obs_source_get_name(s->self));
			return;
		}
	}

	calculate_audio_state(s);
	update_band_texture(s);
	load_effect_if_needed(s);
	if (!s->effect) {
		if (!s->render_logged_no_effect) {
			BLOG(LOG_WARNING, "Source '%s' has no loaded effect. Selected path='%s'", obs_source_get_name(s->self),
			     s->effect_path.c_str());
			s->render_logged_no_effect = true;
		}
		return;
	}

	gs_technique_t *tech = gs_effect_get_technique(s->effect, "Draw");
	if (!tech)
		tech = gs_effect_get_technique(s->effect, "Solid");
	if (!tech)
		tech = gs_effect_get_technique(s->effect, "Default");
	if (!tech) {
		if (!s->render_logged_no_technique) {
			BLOG(LOG_ERROR, "Effect '%s' has no Draw, Solid, or Default technique", s->active_effect_path.c_str());
			s->render_logged_no_technique = true;
		}
		return;
	}

	const uint32_t render_width = scaled_render_dimension(s->width, s->render_scale_percent);
	const uint32_t render_height = scaled_render_dimension(s->height, s->render_scale_percent);
	set_shader_params(s, render_width, render_height);
	gs_texrender_reset(s->texrender);
	if (!gs_texrender_begin(s->texrender, (int)render_width, (int)render_height)) {
		BLOG(LOG_WARNING, "gs_texrender_begin failed for source '%s'", obs_source_get_name(s->self));
		return;
	}
	vec4 clear_color = {};
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_projection_push();
	gs_matrix_push();
	gs_ortho(0.0f, (float)render_width, 0.0f, (float)render_height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_reset_blend_state();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	const size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);
		draw_fullscreen_quad(render_width, render_height);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
	gs_blend_state_pop();
	gs_matrix_pop();
	gs_projection_pop();
	gs_texrender_end(s->texrender);

	gs_texture_t *tex = gs_texrender_get_texture(s->texrender);
	if (!tex)
		return;
	gs_effect_t *draw_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!draw_effect)
		return;
	gs_eparam_t *image_param = gs_effect_get_param_by_name(draw_effect, "image");
	if (image_param)
		gs_effect_set_texture(image_param, tex);
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(draw_effect, "Draw"))
		gs_draw_sprite(tex, 0, s->width, s->height);
	gs_blend_state_pop();

	if (!s->render_logged_ok || s->logged_width != s->width || s->logged_height != s->height ||
	    s->logged_render_width != render_width || s->logged_render_height != render_height) {
		BLOG(LOG_INFO,
		     "Rendering source '%s' with effect '%s': output=%ux%u internal=%ux%u (%d%%)",
		     obs_source_get_name(s->self), s->active_effect_path.c_str(), s->width, s->height, render_width, render_height,
		     s->render_scale_percent);
		s->render_logged_ok = true;
		s->logged_width = s->width;
		s->logged_height = s->height;
		s->logged_render_width = render_width;
		s->logged_render_height = render_height;
	}
}

static uint32_t source_width(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	return s ? s->width : 0;
}

static uint32_t source_height(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	return s ? s->height : 0;
}

static const char *source_name(void *)
{
	return kSourceName;
}

static bool use_canvas_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool use_canvas = obs_data_get_bool(settings, S_USE_OBS_CANVAS);
	obs_property_t *width = obs_properties_get(props, S_WIDTH);
	obs_property_t *height = obs_properties_get(props, S_HEIGHT);
	if (width)
		obs_property_set_enabled(width, !use_canvas);
	if (height)
		obs_property_set_enabled(height, !use_canvas);
	return true;
}

static bool effect_path_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const char *path = obs_data_get_string(settings, S_EFFECT_PATH);
	std::string effect_path = path && *path ? path : default_effect_path_string();
	rebuild_effect_controls(props, effect_path);
	return true;
}

static bool reload_effect_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *s = static_cast<audio_shader_source *>(obs_properties_get_param(props));
	if (!s)
		s = static_cast<audio_shader_source *>(data);
	if (!s)
		return false;
	std::lock_guard<std::mutex> lock(s->render_mutex);
	s->reload_effect = true;
	s->render_logged_ok = false;
	s->render_logged_no_effect = false;
	s->render_logged_no_technique = false;
	s->effect_error.clear();
	BLOG(LOG_INFO, "Manual shader reload queued for '%s'", obs_source_get_name(s->self));
	return true;
}

static obs_properties_t *source_properties(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, s, nullptr);
	obs_property_t *audio = obs_properties_add_list(props, S_AUDIO_SOURCE, "Audio Source", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, audio);
	obs_property_t *effect_path = obs_properties_add_path(props, S_EFFECT_PATH, "HLSL / OBS .effect file",
							      OBS_PATH_FILE, "OBS Effect (*.effect);;All files (*.*)", nullptr);
	obs_property_set_modified_callback(effect_path, effect_path_modified);
	obs_properties_add_button(props, "reload_shader", "\xe2\x86\xba  Reload Shader", reload_effect_clicked);
	std::string shader_status;
	if (!s) {
		shader_status = "Shader status: source state unavailable";
	} else if (!s->effect_error.empty()) {
		shader_status = "Shader status: ERROR - " + diagnostic_excerpt(s->effect_error);
		if (s->effect && !s->active_effect_path.empty())
			shader_status += " | Fallback active: " + s->active_effect_path;
	} else if (s->effect && !s->active_effect_path.empty()) {
		shader_status = "Shader status: OK | Active: " + s->active_effect_path;
	} else if (!s->effect_path.empty()) {
		shader_status = "Shader status: waiting for first compile/reload";
	} else {
		shader_status = "Shader status: no effect selected";
	}
	obs_properties_add_text(props, "shader_status", shader_status.c_str(), OBS_TEXT_INFO);
	obs_properties_add_text(props, "effect_metadata_help",
				"Effect controls and optional startup presets are loaded from your-shader.effect.ini. "
				"A [defaults] section can set canvas/audio parameters, 16 sliders and 8 colors when a new shader is selected.",
				OBS_TEXT_INFO);
	obs_property_t *use_canvas = obs_properties_add_bool(props, S_USE_OBS_CANVAS, "Use OBS base canvas size");
	obs_property_set_modified_callback(use_canvas, use_canvas_modified);
	obs_properties_add_int(props, S_WIDTH, "Manual Canvas Width", 16, 8192, 1);
	obs_properties_add_int(props, S_HEIGHT, "Manual Canvas Height", 16, 8192, 1);
	obs_property_t *render_scale = obs_properties_add_list(props, S_RENDER_SCALE, "Internal Render Scale",
								 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(render_scale, "100% (Full quality)", 100);
	obs_property_list_add_int(render_scale, "75%", 75);
	obs_property_list_add_int(render_scale, "50%", 50);
	obs_property_list_add_int(render_scale, "25% (Maximum performance)", 25);
	obs_properties_add_float_slider(props, S_REACT_DB, "React at dB", -90.0, -1.0, 1.0);
	obs_properties_add_float_slider(props, S_PEAK_DB, "Peak at dB", -60.0, 0.0, 1.0);
	obs_properties_add_int_slider(props, S_ATTACK_MS, "Attack ms", 0, 500, 1);
	obs_properties_add_int_slider(props, S_RELEASE_MS, "Release ms", 0, 2000, 1);
	obs_property_t *fft = obs_properties_add_list(props, S_FFT_SIZE, "FFT Size", OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fft, "512", 512);
	obs_property_list_add_int(fft, "1024", 1024);
	obs_property_list_add_int(fft, "2048", 2048);
	obs_property_list_add_int(fft, "4096", 4096);
	obs_property_list_add_int(fft, "8192", 8192);
	obs_properties_add_int_slider(props, S_BAND_COUNT, "Shader Bands", 8, 64, 1);
	std::string meta_effect_path = s && !s->effect_path.empty() ? s->effect_path : default_effect_path_string();
	rebuild_effect_controls(props, meta_effect_path);
	return props;
}

static void source_defaults(obs_data_t *settings)
{
	char *default_effect = obs_module_file("effects/pulse-ring.effect");
	if (default_effect) {
		obs_data_set_default_string(settings, S_EFFECT_PATH, default_effect);
		bfree(default_effect);
	}
	obs_data_set_default_bool(settings, S_USE_OBS_CANVAS, false);
	obs_data_set_default_int(settings, S_WIDTH, 400);
	obs_data_set_default_int(settings, S_HEIGHT, 400);
	obs_data_set_default_int(settings, S_RENDER_SCALE, 100);
	obs_data_set_default_double(settings, S_REACT_DB, -82.0);
	obs_data_set_default_double(settings, S_PEAK_DB, -28.0);
	obs_data_set_default_int(settings, S_ATTACK_MS, 14);
	obs_data_set_default_int(settings, S_RELEASE_MS, 140);
	obs_data_set_default_int(settings, S_FFT_SIZE, 4096);
	obs_data_set_default_int(settings, S_BAND_COUNT, 64);
	obs_data_set_default_int(settings, "color1", 0xFFFFFF);
	obs_data_set_default_int(settings, "color2", 0xFFD200);
	obs_data_set_default_int(settings, "color3", 0xBB509D);
	obs_data_set_default_int(settings, "color4", 0xAC3CFF);
	obs_data_set_default_int(settings, "color5", 0x38D9FF);
	obs_data_set_default_int(settings, "color6", 0xFF6B35);
	obs_data_set_default_int(settings, "color7", 0x7CFF6B);
	obs_data_set_default_int(settings, "color8", 0x111111);
}

static void source_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;
	detach_audio(s);
	std::lock_guard<std::mutex> lock(s->render_mutex);

	const char *new_effect = obs_data_get_string(settings, S_EFFECT_PATH);
	std::string next_path = new_effect ? new_effect : "";
	const bool effect_changed = next_path != s->effect_path;
	if (effect_changed && s->initial_update_complete && !next_path.empty()) {
		const effect_metadata meta = load_effect_metadata(next_path);
		if (apply_effect_defaults(settings, meta))
			BLOG(LOG_INFO, "Applied shader startup preset from '%s.ini'", next_path.c_str());
	}

	s->audio_source_name = obs_data_get_string(settings, S_AUDIO_SOURCE);
	s->use_obs_canvas = obs_data_get_bool(settings, S_USE_OBS_CANVAS);

	uint32_t next_width = 1920;
	uint32_t next_height = 1080;
	if (s->use_obs_canvas) {
		get_obs_canvas_size(&next_width, &next_height);
	} else {
		next_width = valid_dimension(obs_data_get_int(settings, S_WIDTH), s->width ? s->width : 1920);
		next_height = valid_dimension(obs_data_get_int(settings, S_HEIGHT), s->height ? s->height : 1080);
	}
	set_source_dimensions(s, next_width, next_height);
	const int next_render_scale = valid_render_scale((int)obs_data_get_int(settings, S_RENDER_SCALE));
	if (s->render_scale_percent != next_render_scale) {
		s->render_scale_percent = next_render_scale;
		s->render_logged_ok = false;
	}
	s->react_db = float(obs_data_get_double(settings, S_REACT_DB));
	s->peak_db = float(obs_data_get_double(settings, S_PEAK_DB));
	s->attack_ms = float(obs_data_get_int(settings, S_ATTACK_MS));
	s->release_ms = float(obs_data_get_int(settings, S_RELEASE_MS));

	const int old_fft_size = s->fft_size;
	s->fft_size = clamp_pow2((int)obs_data_get_int(settings, S_FFT_SIZE), 512, 8192);
	s->band_count = std::clamp<int>((int)obs_data_get_int(settings, S_BAND_COUNT), 1, 64);
	if (old_fft_size != s->fft_size) {
		s->analysis_fft_size = 0;
		s->previous_raw_bands.fill(0.0f);
		s->previous_kick_energy = 0.0f;
	}

	if (effect_changed) {
		s->effect_path = next_path;
		s->reload_effect = true;
		s->render_logged_ok = false;
		s->render_logged_no_effect = false;
		s->render_logged_no_technique = false;
	}

	for (int i = 1; i <= 16; ++i) {
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_OPTION_PREFIX, i);
		s->options[(size_t)i - 1] = float(obs_data_get_double(settings, key));
	}
	for (int i = 1; i <= 8; ++i) {
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_COLOR_PREFIX, i);
		s->colors[(size_t)i - 1] = uint32_t(obs_data_get_int(settings, key)) & 0xFFFFFFu;
	}

	{
		std::lock_guard<std::mutex> audio_lock(s->audio_mutex);
		if ((int)s->mono_ring.size() != s->fft_size) {
			s->mono_ring.assign((size_t)s->fft_size, 0.0f);
			s->mono_pos = 0;
			s->mono_count = 0;
		}
	}
	attach_audio(s);
	s->initial_update_complete = true;
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new (std::nothrow) audio_shader_source{};
	if (!s)
		return nullptr;
	s->self = source;
	obs_audio_info ai;
	if (obs_get_audio_info(&ai) && ai.samples_per_sec > 0)
		s->sample_rate = int(ai.samples_per_sec);
	obs_enter_graphics();
	s->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
	if (!s->texrender)
		BLOG(LOG_WARNING, "Could not create texrender at source_create; will retry on first render");
	source_update(s, settings);
	return s;
}

static void source_destroy(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;
	s->alive.store(false, std::memory_order_release);
	detach_audio(s);
	for (int i = 0; i < 2000; ++i) {
		if (s->audio_cb_inflight.load(std::memory_order_acquire) == 0)
			break;
		os_sleep_ms(1);
	}
	obs_enter_graphics();
	destroy_effect(s);
	destroy_texrender(s);
	destroy_band_texture(s);
	obs_leave_graphics();
	release_audio_weak(s);
	delete s;
}

static void source_video_tick(void *data, float)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s || !s->use_obs_canvas)
		return;
	uint32_t canvas_w = 1920;
	uint32_t canvas_h = 1080;
	get_obs_canvas_size(&canvas_w, &canvas_h);
	std::lock_guard<std::mutex> lock(s->render_mutex);
	set_source_dimensions(s, canvas_w, canvas_h);
}

static void source_show(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (s) {
		detach_audio(s);
		attach_audio(s);
	}
}

static void source_hide(void *data)
{
	detach_audio(static_cast<audio_shader_source *>(data));
}

extern "C" void register_audio_shader_source(void)
{
	std::memset(&g_source_info, 0, sizeof(g_source_info));
	g_source_info.id = kSourceId;
	g_source_info.type = OBS_SOURCE_TYPE_INPUT;
	g_source_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	g_source_info.icon_type = OBS_ICON_TYPE_COLOR;
	g_source_info.get_name = source_name;
	g_source_info.create = source_create;
	g_source_info.destroy = source_destroy;
	g_source_info.update = source_update;
	g_source_info.get_defaults = source_defaults;
	g_source_info.get_properties = source_properties;
	g_source_info.get_width = source_width;
	g_source_info.get_height = source_height;
	g_source_info.video_render = source_render;
	g_source_info.video_tick = source_video_tick;
	g_source_info.show = source_show;
	g_source_info.hide = source_hide;
	obs_register_source(&g_source_info);
	BLOG(LOG_INFO, "Registered source '%s'", kSourceId);
}
