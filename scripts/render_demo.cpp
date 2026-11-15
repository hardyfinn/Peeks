// Renders docs/peeks-demo.wav (spec §9): four short scenes through the Peeks engine.
//   make demo
#include "../src/dsp/PeeksEngine.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace peeks;

static const float SR = 48000.f;
static float hzToV(float hz) { return std::log2(hz / kC4); }

struct Out {
	std::vector<float> l, r;
	void add(float a, float b) { l.push_back(a); r.push_back(b); }
};

static void writeWav(const char* path, const Out& o) {
	FILE* f = std::fopen(path, "wb");
	if (!f) { std::perror(path); return; }
	uint32_t n = o.l.size(), bytes = n * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32((uint32_t)SR); u32((uint32_t)SR * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (uint32_t i = 0; i < n; i++) {
		for (float v : {o.l[i], o.r[i]}) {
			float s = std::tanh(v / 8.f) * 0.9f;   // 8 V ~ full scale
			int16_t q = (int16_t)std::lround(s * 32767.f);
			std::fwrite(&q, 2, 1, f);
		}
	}
	std::fclose(f);
}

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "peeks-demo.wav";
	Out o;
	std::mt19937 rng(3);
	std::uniform_real_distribution<float> u(-1, 1);

	// (a) Clocked pings with random peaks. Left OUT, right LP 2.
	{
		PeeksEngine<float> e; e.setSampleRate(SR);
		Knobs k; k.peaking = 1.f; k.curveA = 1.f; k.peak1 = hzToV(180); k.peak2 = hzToV(1400);
		k.p1Mod = 0.5f; k.p2Mod = 0.5f;
		float r1 = 0, r2 = 0;
		int step = (int)(SR * 0.1875f);
		for (int i = 0; i < SR * 8; i++) {
			Frame<float> f;
			bool hit = i % step == 0 && (u(rng) > -0.4f);
			if (i % step == 0) { r1 = 2 * u(rng); r2 = 2 * u(rng); }
			f.ping = hit ? 1.f : 0.f; f.p1In = r1; f.p2In = r2;
			e.process(f, k);
			o.add(f.out * 2.0f, f.lp2 * 1.4f);
		}
	}
	// (b) Saw through the twin peaks: peaks close in until the sound vanishes, then reopen.
	{
		PeeksEngine<float> e; e.setSampleRate(SR);
		Knobs k; k.peaking = 0.75f; k.curveA = 1.f; k.peak1 = hzToV(700); k.peak2 = hzToV(700);
		k.p1Mod = -1.f; k.p2Mod = 1.f;
		float ph = 0;
		for (int i = 0; i < SR * 8; i++) {
			float t = i / SR;
			float spread = 1.6f * std::fabs(std::cos(3.14159265f * t / 8.f));   // 1.6 oct -> 0 -> 1.6
			ph += 110.f / SR; if (ph >= 1) ph -= 1;
			Frame<float> f; f.inA = 5.f * (2 * ph - 1); f.p1In = spread; f.p2In = spread;
			e.process(f, k);
			o.add(f.out * 0.8f, f.out * 0.8f);
		}
	}
	// (c) Metallic audio-rate FM: pulse into IN A, sine into LINK MOD.
	{
		PeeksEngine<float> e; e.setSampleRate(SR);
		Knobs k; k.peaking = 0.9f; k.curveA = 1.f; k.peak1 = hzToV(400); k.peak2 = hzToV(1900); k.linkMod = 0.5f;
		float ph = 0, mph = 0;
		for (int i = 0; i < SR * 7; i++) {
			float t = i / SR;
			float note = (int)(t / 0.875f) % 4;
			float hz = 55.f * std::pow(2.f, note * 5.f / 12.f);
			ph += hz / SR; if (ph >= 1) ph -= 1;
			mph += hz * 2.76f / SR; if (mph >= 1) mph -= 1;
			float env = std::exp(-std::fmod(t, 0.875f) * 3.f);
			Frame<float> f; f.inA = (ph < 0.2f ? 5.f : -1.f) * env; f.linkIn = 3.f * std::sin(6.2831853f * mph);
			e.process(f, k);
			o.add(f.out * 0.8f, f.lp1 * 0.4f + f.out * 0.4f);
		}
	}
	// (d) Endless self-oscillation, sequenced, with self FM.
	{
		PeeksEngine<float> e; e.setSampleRate(SR);
		Knobs k; k.endless = true; k.peaking = 1.f; k.curveA = 1.f; k.peak1 = hzToV(110); k.peak2 = hzToV(166);
		const float seq[8] = {0, 0, 7, 3, 0, 10, 7, 5};
		for (int i = 0; i < SR * 8; i++) {
			float t = i / SR;
			k.selfFM = 0.5f * (1 - std::cos(6.2831853f * t / 8.f));
			Frame<float> f; f.vOct = seq[(int)(t * 4) % 8] / 12.f;
			e.process(f, k);
			float fade = std::min(1.f, std::min(t * 4, (8 - t) * 2));
			o.add(f.lp1 * 2.0f * fade, f.lp2 * 2.0f * fade);
		}
	}
	writeWav(path, o);
	std::printf("wrote %s (%.1f s)\n", path, o.l.size() / SR);
}
