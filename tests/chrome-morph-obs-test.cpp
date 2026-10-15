// SPDX-License-Identifier: GPL-2.0-or-later
// Native libobs integration test. Run on Linux with Xvfb and software OpenGL.
// Arguments: libobs data dir, libobs-opengl library, plugin library, plugin data dir, output.rgba
#include <obs-module.h>
#include <obs-nix-platform.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <X11/Xlib.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	assert(argc == 6);
	Display *display = XOpenDisplay(nullptr);
	assert(display);
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	obs_set_nix_platform_display(display);
	assert(obs_startup("en-US", nullptr, nullptr));
	const std::string libobs_data = std::string(argv[1]) + "/";
	obs_add_data_path(libobs_data.c_str());
	obs_audio_info ai{};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	assert(obs_reset_audio(&ai));
	obs_video_info vi{};
	vi.graphics_module = argv[2];
	vi.fps_num = 60;
	vi.fps_den = 1;
	vi.base_width = vi.output_width = 768;
	vi.base_height = vi.output_height = 768;
	vi.output_format = VIDEO_FORMAT_RGBA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_FULL;
	vi.scale_type = OBS_SCALE_BILINEAR;
	assert(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS);
	obs_module_t *module = nullptr;
	assert(obs_open_module(&module, argv[3], argv[4]) == MODULE_SUCCESS);
	assert(obs_init_module(module));
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "demo", true);
	obs_data_set_int(settings, "quality", 1);
	obs_source_t *source = obs_source_create("chrome_morph_source_v1", "Chrome Morph test", settings, nullptr);
	assert(source);
	obs_source_inc_showing(source);
	os_sleep_ms(120);
	obs_enter_graphics();
	auto *rt = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	assert(gs_texrender_begin(rt, 768, 768));
	vec4 clear{};
	gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
	gs_ortho(0, 768, 0, 768, -100, 100);
	gs_matrix_push();
	gs_matrix_identity();
	obs_source_video_render(source);
	gs_matrix_pop();
	gs_texrender_end(rt);
	auto *stage = gs_stagesurface_create(768, 768, GS_RGBA);
	gs_stage_texture(stage, gs_texrender_get_texture(rt));
	uint8_t *bytes = nullptr;
	uint32_t stride = 0;
	assert(gs_stagesurface_map(stage, &bytes, &stride));
	FILE *out = std::fopen(argv[5], "wb");
	assert(out);
	size_t opaque = 0, transparent = 0;
	for (size_t y = 0; y < 768; ++y) {
		std::fwrite(bytes + y * stride, 1, 768 * 4, out);
		for (size_t x = 0; x < 768; ++x) {
			const uint8_t alpha = bytes[y * stride + x * 4 + 3];
			opaque += alpha == 255;
			transparent += alpha == 0;
		}
	}
	std::fclose(out);
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(rt);
	obs_leave_graphics();
	std::printf("NATIVE OBS: opaque=%zu transparent=%zu\n", opaque, transparent);
	assert(opaque > 20000 && transparent > 300000);
	// Preserve the legacy type, update quality without creating a new source, then hide/destroy.
	obs_data_t *legacy = obs_get_source_defaults("audio_shader_engine_source");
	assert(legacy);
	obs_data_release(legacy);
	obs_data_set_int(settings, "quality", 0);
	obs_source_update(source, settings);
	os_sleep_ms(80);                             // Active video-source updates are deferred to OBS' video thread.
	assert(obs_source_get_width(source) == 768); // Quality must not change the scene's framing.
	obs_source_info fake{};
	fake.id = "chrome_morph_test_audio";
	fake.type = OBS_SOURCE_TYPE_INPUT;
	fake.output_flags = OBS_SOURCE_AUDIO;
	fake.get_name = [](void *) {
		return "Synthetic DJ input";
	};
	fake.create = [](obs_data_t *, obs_source_t *) -> void * {
		return new int(0);
	};
	fake.destroy = [](void *p) {
		delete static_cast<int *>(p);
	};
	obs_register_source(&fake);
	obs_source_t *dj = obs_source_create(fake.id, "Test DJ", nullptr, nullptr);
	assert(dj);
	obs_data_set_bool(settings, "demo", false);
	obs_data_set_string(settings, "audio_uuid", obs_source_get_uuid(dj));
	obs_source_update(source, settings);
	os_sleep_ms(80);
	float l[480], r[480];
	obs_source_audio input{};
	input.frames = 480;
	input.samples_per_sec = 48000;
	input.format = AUDIO_FORMAT_FLOAT_PLANAR;
	input.speakers = SPEAKERS_STEREO;
	input.data[0] = reinterpret_cast<const uint8_t *>(l);
	input.data[1] = reinterpret_cast<const uint8_t *>(r);
	for (int block = 0; block < 30; ++block) {
		for (int i = 0; i < 480; ++i) {
			l[i] = 0.3f * std::sin(float(block * 480 + i) * 0.0085f);
			r[i] = -l[i];
		}
		input.timestamp = os_gettime_ns();
		obs_source_output_audio(dj, &input);
		os_sleep_ms(10);
	}
	obs_properties_t *props = obs_source_properties(source);
	std::string status = obs_property_description(obs_properties_get(props, "status"));
	std::printf("Native callback status: %s\n", status.c_str());
	assert(status.find("Audio reçu") != std::string::npos);
	obs_properties_destroy(props);
	os_sleep_ms(250);
	props = obs_source_properties(source);
	status = obs_property_description(obs_properties_get(props, "status"));
	assert(status.find("En attente de son") != std::string::npos);
	obs_properties_destroy(props);
	obs_source_dec_showing(source);
	obs_source_release(source);
	obs_data_release(settings);
	obs_source_release(dj);
	obs_shutdown();
	XCloseDisplay(display);
	std::puts("PASS: native effect compilation, source registration, rendering, alpha, quality and lifecycle");
}
