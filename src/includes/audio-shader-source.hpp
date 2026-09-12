#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <atomic>
#include <array>
#include <complex>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

struct audio_shader_source {
	obs_source_t *self = nullptr;

	std::string audio_source_name;
	obs_weak_source_t *audio_weak = nullptr;

	std::atomic<bool> alive{true};
	std::atomic<uint32_t> audio_cb_inflight{0};

	std::mutex audio_mutex;
	std::vector<float> mono_ring;
	size_t mono_pos = 0;
	size_t mono_count = 0;
	float raw_level = 0.0f;
	float raw_peak = 0.0f;

	std::mutex render_mutex;

	uint32_t width = 1920;
	uint32_t height = 1080;
	bool use_obs_canvas = true;
	int render_scale_percent = 100;
	uint32_t logged_width = 0;
	uint32_t logged_height = 0;
	uint32_t logged_render_width = 0;
	uint32_t logged_render_height = 0;

	float react_db = -82.0f;
	float peak_db = -28.0f;
	float attack_ms = 14.0f;
	float release_ms = 140.0f;
	uint64_t last_ts_ns = 0;

	float level = 0.0f;
	float peak = 0.0f;

	// Legacy three-band uniforms. Kept for full backwards compatibility.
	float bass = 0.0f;
	float mid = 0.0f;
	float treble = 0.0f;

	// VFX v1 musical analysis. These are exposed as additional shader uniforms.
	float sub = 0.0f;
	float low = 0.0f;
	float low_mid = 0.0f;
	float mid_vfx = 0.0f;
	float high_mid = 0.0f;
	float high = 0.0f;
	float transient = 0.0f;
	float kick = 0.0f;
	float previous_kick_energy = 0.0f;
	std::array<float, 64> previous_raw_bands{};
	std::array<float, 64> bands{};

	int fft_size = 4096;
	int band_count = 64;
	int sample_rate = 48000;

	// Reusable analysis buffers. They are owned by the render thread and avoid
	// allocating/cos() work every rendered frame.
	int analysis_fft_size = 0;
	std::vector<float> fft_snapshot;
	std::vector<float> hann_window;
	std::vector<std::complex<float>> fft_work;

	std::string effect_path;
	std::string active_effect_path;
	gs_effect_t *effect = nullptr;
	std::string effect_error;
	bool reload_effect = true;
	bool render_logged_ok = false;
	bool render_logged_no_effect = false;
	bool render_logged_no_technique = false;

	gs_texrender_t *texrender = nullptr;

	gs_texture_t *band_texture = nullptr;
	std::array<uint8_t, 64 * 4> band_texture_pixels{};

	std::array<float, 16> options{};
	std::array<uint32_t, 8> colors{0xFFFFFFu, 0xFFD200u, 0xBB509Du, 0xAC3CFFu, 0x38D9FFu, 0xFF6B35u, 0x7CFF6Bu, 0x111111u};
};

extern "C" void register_audio_shader_source(void);
