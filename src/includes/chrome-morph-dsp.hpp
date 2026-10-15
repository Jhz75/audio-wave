// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chrome_morph {
constexpr double pi = 3.14159265358979323846;
inline float unit(float x)
{
	return std::clamp(x, 0.0f, 1.0f);
}
inline float follow(float current, float target, float dt, float attack, float release)
{
	return target + (current - target) * std::exp(-dt / (target > current ? attack : release));
}

struct Features {
	float bass = 0, mid = 0, high = 0, kick = 0, level = 0;
	uint32_t kicks = 0; // Cumulative: events survive skipped video frames / a full queue.
	uint64_t received_ns = 0;
};

// Exactly one audio producer and one video consumer. No locks or allocation in push/pop.
template<size_t Capacity = 256> class FeatureQueue {
	static_assert(Capacity > 1);
	static_assert(std::atomic<size_t>::is_always_lock_free);
	std::array<Features, Capacity> slots{};
	alignas(64) std::atomic<size_t> write{0};
	alignas(64) std::atomic<size_t> read{0};

public:
	bool push(const Features &value)
	{
		const size_t w = write.load(std::memory_order_relaxed);
		const size_t next = (w + 1) % Capacity;
		if (next == read.load(std::memory_order_acquire))
			return false;
		slots[w] = value;
		write.store(next, std::memory_order_release);
		return true;
	}
	bool pop(Features &value)
	{
		const size_t r = read.load(std::memory_order_relaxed);
		if (r == write.load(std::memory_order_acquire))
			return false;
		value = slots[r];
		read.store((r + 1) % Capacity, std::memory_order_release);
		return true;
	}
	// Only when producer has been detached AND consumer is excluded by the state mutex.
	void reset_quiescent()
	{
		read.store(0);
		write.store(0);
	}
};

struct Biquad {
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
	void configure(float rate, float hz, bool highpass)
	{
		const double w = 2 * pi * std::clamp(double(hz / rate), 0.0001, 0.45);
		const double c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0);
		const double norm = 1 / (1 + alpha);
		b0 = float((highpass ? 1 + c : 1 - c) * 0.5 * norm);
		b1 = float((highpass ? -(1 + c) : 1 - c) * norm);
		b2 = b0;
		a1 = float(-2 * c * norm);
		a2 = float((1 - alpha) * norm);
		z1 = z2 = 0;
	}
	float tick(float x)
	{
		const float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		// Prevent costly denormals on long silence without changing audible dynamics.
		if (std::fabs(z1) < 1e-20f)
			z1 = 0;
		if (std::fabs(z2) < 1e-20f)
			z2 = 0;
		return y;
	}
};

// Sample-driven four-band IIR analysis. The kick band is an onset heuristic,
// not instrument/source separation. Both stereo channels contribute POWER.
class Analyzer {
	struct Band {
		Biquad highpass, lowpass, highpass2, lowpass2;
	};
	std::array<std::array<Band, 8>, 4> filters{};
	std::array<double, 4> power{};
	std::array<float, 3> reference{0.04f, 0.04f, 0.025f};
	float rate = 48000, fast = 0, slow = 0, refractory = 0;
	unsigned channels = 2, hop = 128, filled = 0;
	double full_power = 0;
	Features state{};

public:
	void configure(unsigned sample_rate, unsigned channel_count)
	{
		*this = Analyzer{};
		rate = float(std::clamp(sample_rate, 8000u, 192000u));
		channels = std::clamp(channel_count, 1u, 8u);
		hop = std::max(16u, unsigned(std::lround(rate / 375))); // ~2.67 ms, all sample rates.
		const float lo[4] = {40, 30, 220, 2800};
		const float hi[4] = {140, 220, 2800, 14000};
		for (size_t b = 0; b < 4; ++b)
			for (unsigned c = 0; c < channels; ++c) {
				filters[b][c].highpass.configure(rate, std::min(lo[b], rate * 0.35f), true);
				filters[b][c].lowpass.configure(rate, std::min(hi[b], rate * 0.45f), false);
				filters[b][c].highpass2 = filters[b][c].highpass;
				filters[b][c].lowpass2 = filters[b][c].lowpass;
			}
	}

	template<class Sink>
	void process(const float *const *input, size_t frames, bool muted, uint64_t now, Sink &&sink)
	{
		for (size_t i = 0; i < frames; ++i) {
			for (unsigned c = 0; c < channels; ++c) {
				float x = !muted && input && input[c] ? input[c][i] : 0;
				x = std::isfinite(x) ? std::clamp(x, -8.0f, 8.0f) : 0;
				full_power += double(x) * x;
				for (size_t b = 0; b < 4; ++b) {
					auto &f = filters[b][c];
					const float y =
						f.lowpass2.tick(f.highpass2.tick(f.lowpass.tick(f.highpass.tick(x))));
					power[b] += double(y) * y;
				}
			}
			if (++filled < hop)
				continue;
			const float dt = float(hop) / rate;
			std::array<float, 4> rms{};
			for (size_t b = 0; b < 4; ++b) {
				rms[b] = float(std::sqrt(power[b] / double(hop * channels)));
				power[b] = 0;
			}
			const float previous_fast = fast;
			fast = follow(fast, rms[0], dt, 0.008f, 0.035f);
			slow = follow(slow, fast, dt, 0.20f, 0.28f);
			refractory = std::max(0.0f, refractory - dt);
			if (!muted && refractory <= 0 && fast > std::max(0.003f, slow * 1.65f) &&
			    fast > previous_fast + std::max(0.00015f, slow * 0.025f)) {
				state.kick = unit((fast / std::max(slow, 0.002f) - 1) * 0.65f);
				++state.kicks;
				refractory = 0.13f;
			} else
				state.kick *= std::exp(-dt / 0.075f);
			float *output[3] = {&state.bass, &state.mid, &state.high};
			const float floors[3] = {0.04f, 0.04f, 0.025f};
			for (size_t b = 0; b < 3; ++b) {
				const float r = rms[b + 1];
				reference[b] = follow(reference[b], std::max(floors[b], r), dt, 0.045f, 8.0f);
				const float gate = unit((r - 0.0006f) / 0.0024f);
				const float target = gate * (1 - std::exp(-1.6f * r / reference[b]));
				*output[b] = follow(*output[b], muted ? 0 : target, dt, 0.012f, b == 2 ? 0.10f : 0.18f);
			}
			state.level = unit(float(std::sqrt(full_power / double(hop * channels))) * 3);
			state.received_ns = now;
			sink(state);
			filled = 0;
			full_power = 0;
		}
	}
};

struct Motion {
	float time = 0, phase = 0, split = 0, opening = 0;
	void advance(float dt, const Features &f)
	{
		dt = std::clamp(dt, 0.0f, 0.1f);
		time += dt * (0.35f + 0.65f * f.mid);
		phase += dt * (0.25f + 0.45f * f.mid) * unit(f.bass + f.mid);
		const float cycle = unit((std::sin(phase) - 0.22f) / 0.65f);
		const float target = cycle * cycle * (3 - 2 * cycle) * unit(f.bass * 1.8f);
		split = follow(split, target, dt, 0.6f, 0.8f);
		opening = follow(opening, unit((f.mid - 0.12f) * 1.4f) * (1 - split * 0.75f), dt, 0.4f, 0.65f);
	}
};
} // namespace chrome_morph
