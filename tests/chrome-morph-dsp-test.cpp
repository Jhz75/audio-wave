// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/includes/chrome-morph-dsp.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using namespace chrome_morph;
struct Result {
	Features last;
	float max_kick = 0;
	size_t first_kick = 0, packets = 0;
};
Result run(const std::vector<float> &l, const std::vector<float> &r, size_t block = 480, unsigned sr = 48000)
{
	Analyzer a;
	a.configure(sr, 2);
	Result result;
	for (size_t off = 0; off < l.size(); off += block) {
		const float *input[8] = {l.data() + off, r.data() + off};
		a.process(input, std::min(block, l.size() - off), false, 1, [&](const Features &f) {
			++result.packets;
			if (!result.first_kick && f.kicks)
				result.first_kick = result.packets;
			result.last = f;
			result.max_kick = std::max(result.max_kick, f.kick);
		});
	}
	return result;
}
std::vector<float> tone(float hz, float amplitude = 0.18f, unsigned sr = 48000)
{
	std::vector<float> v(sr * 2);
	for (size_t i = 0; i < v.size(); ++i)
		v[i] = amplitude * std::sin(float(2 * pi) * hz * float(i) / float(sr));
	return v;
}

int main()
{
	for (unsigned sr : {44100u, 48000u, 96000u}) {
		auto low = tone(65, 0.18f, sr), mid = tone(1100, 0.18f, sr), high = tone(8200, 0.18f, sr);
		const auto b = run(low, low, 480, sr).last, m = run(mid, mid, 480, sr).last,
			   h = run(high, high, 480, sr).last;
		std::cout << "bands @" << sr << ": low=" << b.bass << "," << b.mid << "," << b.high << " mid=" << m.bass
			  << "," << m.mid << "," << m.high << " high=" << h.bass << "," << h.mid << "," << h.high
			  << '\n';
		assert(b.bass > 0.7f && b.bass > b.mid * 2 && b.high < 0.05f);
		assert(m.mid > 0.7f && m.mid > m.bass * 2 && m.mid > m.high * 2);
		assert(h.high > 0.7f && h.high > h.mid * 2 && h.bass < 0.05f);
	}
	const auto signal = tone(65);
	auto opposite = signal;
	for (float &v : opposite)
		v = -v;
	const auto normal = run(signal, signal), inverted = run(signal, opposite), small = run(signal, signal, 73);
	assert(std::fabs(normal.last.bass - inverted.last.bass) < 1e-6f);
	assert(std::fabs(normal.last.level - inverted.last.level) < 1e-6f);
	assert(std::fabs(normal.last.bass - small.last.bass) < 1e-6f && normal.last.kicks == small.last.kicks);
	assert(normal.last.kicks <= 2); // Sustained bass must not become a rhythmic kick train.
	assert(run(tone(8200), tone(8200)).last.kicks == 0);
	Analyzer mono;
	mono.configure(48000, 1);
	Features mono_state{};
	const float *mono_input[8] = {signal.data()};
	mono.process(mono_input, signal.size(), false, 1, [&](const Features &f) { mono_state = f; });
	assert(std::fabs(mono_state.bass - normal.last.bass) < 1e-6f);
	for (float amplitude : {0.015f, 0.08f, 0.4f}) {
		const auto v = tone(1100, amplitude);
		const auto f = run(v, v).last;
		assert(f.mid > 0.28f && f.mid < 0.97f); // no permanent saturation / dead quiet input
	}
	for (float bpm : {120.0f, 150.0f, 190.0f}) {
		const size_t n = 48000 * 8;
		std::vector<float> kick(n);
		for (size_t i = 0; i < n; ++i) {
			const float t = float(i) / 48000, age = std::fmod(t, 60 / bpm);
			kick[i] = 0.42f * std::sin(float(2 * pi) * 65 * t) * std::exp(-age * 28) +
				  0.012f * std::sin(float(2 * pi) * 42 * t);
		}
		const auto out = run(kick, kick);
		const auto expected = uint32_t(std::ceil(8 * bpm / 60));
		std::cout << "kick " << bpm << " BPM: " << out.last.kicks << "/" << expected
			  << " first detection=" << double(out.first_kick) * 128 / 48 << " ms\n";
		assert(out.last.kicks >= expected - 1 && out.last.kicks <= expected + 1);
		assert(out.first_kick * 128 < 48000 / 20); // under 50 ms algorithmic onset on this signal
	}
	// Silence, muted input, non-finite samples and filter tails.
	Analyzer a;
	a.configure(48000, 2);
	Features last{};
	const float *input[8] = {signal.data(), signal.data()};
	a.process(input, signal.size(), false, 1, [&](const Features &f) { last = f; });
	a.process(nullptr, 48000 * 3, true, 2, [&](const Features &f) { last = f; });
	assert(last.bass < 1e-5f && last.mid < 1e-5f && last.high < 1e-5f && last.kick < 1e-5f);
	std::array<float, 128> nan{};
	nan.fill(std::numeric_limits<float>::quiet_NaN());
	const float *bad[8] = {nan.data(), nan.data()};
	a.process(bad, nan.size(), false, 3,
		  [&](const Features &f) { assert(std::isfinite(f.level) && std::isfinite(f.bass)); });
	FeatureQueue<8> q;
	Features f{};
	for (uint32_t i = 0; i < 7; ++i) {
		f.kicks = i;
		assert(q.push(f));
	}
	assert(!q.push(f));
	for (uint32_t i = 0; i < 7; ++i) {
		assert(q.pop(f));
		assert(f.kicks == i);
	}
	assert(!q.pop(f));
	FeatureQueue<256> threaded;
	std::thread producer([&] {
		for (uint32_t i = 0; i < 100000; ++i) {
			Features v{};
			v.kicks = i;
			while (!threaded.push(v))
				std::this_thread::yield();
		}
	});
	for (uint32_t i = 0; i < 100000; ++i) {
		while (!threaded.pop(f))
			std::this_thread::yield();
		assert(f.kicks == i);
	}
	producer.join();
	Motion motion;
	f = {0.7f, 0.7f, 0.5f, 0.8f, 0.8f, 0, 0};
	float max_split = 0;
	for (int i = 0; i < 3600; ++i) {
		motion.advance(1.0f / 60, f);
		max_split = std::max(max_split, motion.split);
	}
	assert(max_split > 0.95f);
	for (int i = 0; i < 600; ++i)
		motion.advance(1.0f / 60, Features{});
	assert(motion.split < 1e-4f && motion.opening < 1e-4f);
	std::cout << "PASS: bands, stereo phase, block invariance, gain, kicks, silence, queue, topology\n";
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 5; ++i)
		run(signal, signal);
	std::cout << "DSP wall time for 10 s stereo: "
		  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
		  << " ms (this host)\n";
}
