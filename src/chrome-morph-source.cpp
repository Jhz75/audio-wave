// SPDX-License-Identifier: GPL-2.0-or-later
#include "includes/chrome-morph-dsp.hpp"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <mutex>
#include <new>
#include <string>

namespace {
using namespace chrome_morph;
constexpr const char *source_id = "chrome_morph_source_v1";
struct Source {
	obs_source_t *self = nullptr;
	obs_weak_source_t *target = nullptr;
	std::string audio_uuid, error;
	std::mutex lifecycle_mutex, state_mutex;
	Analyzer analyzer;
	FeatureQueue<> queue;
	Features features;
	Motion motion;
	std::atomic<uint32_t> size{768};
	float intensity = 1;
	int quality = 1;
	bool demo = false, dirty = true, ready = false;
	uint64_t next_retry = 0, last_audio = 0;
	float age_ms = 0;
	gs_effect_t *effect = nullptr;
	gs_texrender_t *render = nullptr;
	gs_eparam_t *resolution = nullptr, *audio = nullptr, *shape = nullptr, *rotation = nullptr, *steps = nullptr;
};

void capture(void *data, obs_source_t *, const audio_data *audio_data, bool muted)
{
	auto *s = static_cast<Source *>(data);
	if (!audio_data || !audio_data->frames)
		return;
	const float *input[8]{};
	for (size_t c = 0; c < 8; ++c)
		input[c] = reinterpret_cast<const float *>(audio_data->data[c]);
	s->analyzer.process(input, audio_data->frames, muted, os_gettime_ns(),
			    [s](const Features &f) { s->queue.push(f); });
}

// Lifecycle mutex always precedes state mutex. Capture takes neither.
// OBS serializes callback removal with delivery using source->audio_cb_mutex.
void detach(Source *s)
{
	if (!s->target)
		return;
	obs_source_t *target = obs_weak_source_get_source(s->target);
	if (target) {
		obs_source_remove_audio_capture_callback(target, capture, s);
		obs_source_release(target);
	}
	obs_weak_source_release(s->target);
	s->target = nullptr;
}

void attach(Source *s)
{
	if (s->target || s->audio_uuid.empty() || s->demo || !obs_source_showing(s->self))
		return;
	obs_source_t *target = obs_get_source_by_uuid(s->audio_uuid.c_str());
	if (!target)
		return;
	if (target == s->self || !(obs_source_get_output_flags(target) & OBS_SOURCE_AUDIO)) {
		obs_source_release(target);
		return;
	}
	obs_audio_info ai{};
	obs_get_audio_info(&ai);
	{
		std::lock_guard<std::mutex> lock(s->state_mutex);
		s->queue.reset_quiescent();
		s->analyzer.configure(ai.samples_per_sec, unsigned(get_audio_channels(ai.speakers)));
		s->features = {};
		s->last_audio = 0;
	}
	s->target = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, capture, s);
	obs_source_release(target);
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "quality", 1);
	obs_data_set_default_double(settings, "intensity", 1.0);
	obs_data_set_default_bool(settings, "demo", false);
}

void update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<Source *>(data);
	std::lock_guard<std::mutex> life(s->lifecycle_mutex);
	const std::string selected = obs_data_get_string(settings, "audio_uuid");
	const bool demo = obs_data_get_bool(settings, "demo");
	const bool reattach = selected != s->audio_uuid || demo != s->demo;
	if (reattach)
		detach(s);
	{
		std::lock_guard<std::mutex> lock(s->state_mutex);
		s->audio_uuid = selected;
		s->demo = demo;
		s->quality = std::clamp(int(obs_data_get_int(settings, "quality")), 0, 2);
		s->size.store(s->quality == 0 ? 576u : s->quality == 1 ? 768u : 1024u);
		s->intensity = std::clamp(float(obs_data_get_double(settings, "intensity")), 0.5f, 1.5f);
		if (reattach) {
			s->features = {};
			s->last_audio = 0;
			s->queue.reset_quiescent();
		}
		s->dirty = true;
	}
	attach(s);
}

void *create(obs_data_t *settings, obs_source_t *self)
{
	auto *s = new (std::nothrow) Source;
	if (!s)
		return nullptr;
	s->self = self;
	update(s, settings);
	return s;
}

void destroy(void *data)
{
	auto *s = static_cast<Source *>(data);
	{
		std::lock_guard<std::mutex> life(s->lifecycle_mutex);
		detach(s);
	}
	obs_enter_graphics();
	if (s->effect)
		gs_effect_destroy(s->effect);
	if (s->render)
		gs_texrender_destroy(s->render);
	obs_leave_graphics();
	delete s;
}

void show(void *data)
{
	auto *s = static_cast<Source *>(data);
	std::lock_guard<std::mutex> life(s->lifecycle_mutex);
	attach(s);
}
void hide(void *data)
{
	auto *s = static_cast<Source *>(data);
	std::lock_guard<std::mutex> life(s->lifecycle_mutex);
	detach(s);
}

void tick(void *data, float dt)
{
	auto *s = static_cast<Source *>(data);
	if (!obs_source_showing(s->self))
		return;
	const uint64_t now = os_gettime_ns();
	{
		std::lock_guard<std::mutex> lock(s->state_mutex);
		Features packet{};
		float max_kick = 0;
		while (s->queue.pop(packet)) {
			if (now >= packet.received_ns && now - packet.received_ns < 100000000ULL)
				max_kick = std::max(max_kick, packet.kick);
			s->features = packet;
			s->last_audio = packet.received_ns;
		}
		s->features.kick = std::max(s->features.kick, max_kick);
		if (s->demo) {
			const float t = s->motion.time;
			const float pulse = std::exp(-std::fmod(t * 2.5f, 1.0f) * 15);
			s->features = {0.55f + 0.15f * std::sin(t),
				       0.5f + 0.25f * std::sin(t * 0.7f),
				       0.3f + 0.2f * std::sin(t * 2.1f),
				       pulse,
				       0.5f,
				       0,
				       now};
			s->last_audio = now;
		}
		s->age_ms = s->last_audio ? float(double(now - s->last_audio) / 1e6) : -1;
		if (s->age_ms < 0 || s->age_ms > 100) {
			const float decay = std::exp(-std::clamp(dt, 0.0f, 0.1f) / 0.12f);
			s->features.bass *= decay;
			s->features.mid *= decay;
			s->features.high *= decay;
			s->features.kick *= decay;
			s->features.level *= decay;
		}
		Features movement = s->features;
		movement.bass = unit(movement.bass * s->intensity);
		movement.mid = unit(movement.mid * s->intensity);
		movement.high = unit(movement.high * s->intensity);
		s->motion.advance(dt, movement);
		s->dirty = true;
	}
	if (now >= s->next_retry) {
		s->next_retry = now + 1000000000ULL;
		std::lock_guard<std::mutex> life(s->lifecycle_mutex);
		if (s->target && obs_weak_source_expired(s->target))
			detach(s);
		attach(s);
	}
}

void set4(gs_eparam_t *p, float a, float b, float c, float d)
{
	vec4 v;
	vec4_set(&v, a, b, c, d);
	gs_effect_set_vec4(p, &v);
}

bool prepare(Source *s)
{
	if (!s->render)
		s->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (s->effect)
		return s->render != nullptr;
	if (!s->error.empty())
		return false; // explicit reload button avoids compile storms.
	char *path = obs_module_file("effects/chrome-morph.effect");
	char *error = nullptr;
	if (path)
		s->effect = gs_effect_create_from_file(path, &error);
	bfree(path);
	if (!s->effect) {
		s->error = error ? error : "chrome-morph.effect introuvable";
		blog(LOG_ERROR, "[CHROME MORPH] %s", s->error.c_str());
		bfree(error);
		return false;
	}
	bfree(error);
	s->resolution = gs_effect_get_param_by_name(s->effect, "resolution");
	s->audio = gs_effect_get_param_by_name(s->effect, "audio");
	s->shape = gs_effect_get_param_by_name(s->effect, "shape");
	s->rotation = gs_effect_get_param_by_name(s->effect, "rotation");
	s->steps = gs_effect_get_param_by_name(s->effect, "steps");
	if (!s->resolution || !s->audio || !s->shape || !s->rotation || !s->steps ||
	    !gs_effect_get_technique(s->effect, "Draw")) {
		s->error = "Shader incomplet : uniforms ou technique Draw manquants";
		gs_effect_destroy(s->effect);
		s->effect = nullptr;
		return false;
	}
	blog(LOG_INFO, "[CHROME MORPH] Shader ready; native cached RGBA; automatic four-band IIR");
	return s->render != nullptr;
}

void render(void *data, gs_effect_t *)
{
	auto *s = static_cast<Source *>(data);
	std::lock_guard<std::mutex> lock(s->state_mutex);
	if (!prepare(s))
		return;
	const uint32_t size = s->size.load();
	if (s->dirty) {
		gs_texrender_reset(s->render);
		if (!gs_texrender_begin(s->render, size, size)) {
			s->ready = false;
			return;
		}
		vec4 clear{};
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, float(size), 0, float(size), -100, 100);
		gs_blend_state_push();
		gs_enable_blending(false);
		vec2 dimensions;
		vec2_set(&dimensions, float(size), float(size));
		gs_effect_set_vec2(s->resolution, &dimensions);
		const auto &f = s->features;
		const auto &m = s->motion;
		set4(s->audio, f.kick, f.bass, f.mid, f.high);
		set4(s->shape, m.time, m.split, m.opening, s->intensity);
		const float yaw = std::sin(m.time * 0.17f) * 0.48f;
		const float roll = std::sin(m.time * 0.21f) * 0.22f;
		set4(s->rotation, std::cos(yaw), std::sin(yaw), std::cos(roll), std::sin(roll));
		gs_effect_set_float(s->steps, s->quality == 0 ? 64.0f : s->quality == 1 ? 88.0f : 112.0f);
		while (gs_effect_loop(s->effect, "Draw"))
			gs_draw_sprite(nullptr, 0, size, size);
		gs_blend_state_pop();
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(s->render);
		s->dirty = false;
		s->ready = true;
	}
	if (!s->ready)
		return;
	gs_texture_t *texture = gs_texrender_get_texture(s->render);
	if (!texture)
		return;
	gs_effect_t *draw = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(draw, "image"), texture);
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(draw, "Draw"))
		gs_draw_sprite(texture, 0, size, size);
	gs_blend_state_pop();
}

bool enum_audio(void *data, obs_source_t *source)
{
	const uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) && !(flags & OBS_SOURCE_COMPOSITE))
		obs_property_list_add_string(static_cast<obs_property_t *>(data), obs_source_get_name(source),
					     obs_source_get_uuid(source));
	return true;
}

bool reload(obs_properties_t *props, obs_property_t *, void *)
{
	auto *s = static_cast<Source *>(obs_properties_get_param(props));
	if (!s)
		return false;
	// Same order as render: graphics context, then state mutex.
	obs_enter_graphics();
	{
		std::lock_guard<std::mutex> lock(s->state_mutex);
		if (s->effect)
			gs_effect_destroy(s->effect);
		s->effect = nullptr;
		s->error.clear();
		s->dirty = true;
		s->ready = false;
	}
	obs_leave_graphics();
	return true;
}

obs_properties_t *properties(void *data)
{
	auto *s = static_cast<Source *>(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, s, nullptr);
	auto *audio = obs_properties_add_list(props, "audio_uuid", "Source audio du set DJ", OBS_COMBO_TYPE_LIST,
					      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio, "Choisir la source DJ…", "");
	obs_enum_sources(enum_audio, audio);
	auto *quality = obs_properties_add_list(props, "quality", "Qualité / surface de calcul", OBS_COMBO_TYPE_LIST,
						OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(quality, "Économe · 576 × 576 / 64 pas", 0);
	obs_property_list_add_int(quality, "Équilibré · 768 × 768 / 88 pas", 1);
	obs_property_list_add_int(quality, "Détaillé · 1024 × 1024 / 112 pas", 2);
	obs_properties_add_float_slider(props, "intensity", "Intensité", 0.5, 1.5, 0.05);
	obs_properties_add_bool(props, "demo", "Démonstration sans audio (désactiver pour le set)");
	std::string status = "Niveaux automatiques · fond transparent · SDR · dimensions et position dans OBS";
	if (s) {
		std::lock_guard<std::mutex> lock(s->state_mutex);
		if (!s->error.empty())
			status = "Erreur : " + s->error;
		else if (s->demo)
			status = "DÉMO : mouvements synthétiques, aucun son capturé";
		else if (s->age_ms < 0 || s->age_ms > 100)
			status = "En attente de son : vérifier la source choisie et sa mise en sourdine";
		else
			status = "Audio reçu · adaptation automatique · âge du dernier bloc : " +
				 std::to_string(int(s->age_ms)) + " ms (hors latence matérielle)";
	}
	obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
	obs_properties_add_button(props, "reload", "Recharger le shader", reload);
	return props;
}
} // namespace

extern "C" void register_chrome_morph_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_COLOR;
	info.get_name = [](void *) {
		return "CHROME MORPH · Prototype";
	};
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = defaults;
	info.get_properties = properties;
	info.get_width = [](void *data) {
		return static_cast<Source *>(data)->size.load();
	};
	info.get_height = info.get_width;
	info.video_render = render;
	info.video_tick = tick;
	info.show = show;
	info.hide = hide;
	obs_register_source(&info);
}
