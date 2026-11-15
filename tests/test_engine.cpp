// Behaviour tests for the Peeks engine (spec §8), on PeeksEngine<float>.
#include "check.hpp"
#include "../src/dsp/PeeksEngine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <chrono>

using namespace peeks;

static float hzToV(float hz) { return std::log2(hz / kC4); }

struct Rig {
	PeeksEngine<float> e;
	Knobs k;
	Rig(float sr = 48000.f, int os = 2) { e.setSampleRate(sr); e.setOversampling(os); }
	Frame<float> step(float a, float b = 0.f, float ping = 0.f) {
		Frame<float> f;
		f.inA = a; f.inB = b; f.ping = ping;
		e.process(f, k);
		return f;
	}
};

// Impulse response of OUT (which = 0), LP1 (1) or LP2 (2), small impulse to stay linear.
static std::vector<float> impulse(Rig& r, int n, int which = 0, bool inB = false) {
	std::vector<float> y(n);
	const float amp = 1e-3f;
	for (int i = 0; i < n; i++) {
		float x = i == 0 ? amp : 0.f;
		auto f = inB ? r.step(0.f, x) : r.step(x);
		y[i] = (which == 0 ? f.out : which == 1 ? f.lp1 : f.lp2) / amp;
	}
	return y;
}

static double dftMag(const std::vector<float>& y, double hz, double sr) {
	double re = 0, im = 0, w = 2 * M_PI * hz / sr;
	double c = std::cos(w), s = std::sin(w), cr = 1, ci = 0;
	for (size_t i = 0; i < y.size(); i++) {
		re += y[i] * cr; im -= y[i] * ci;
		double ncr = cr * c - ci * s; ci = cr * s + ci * c; cr = ncr;
	}
	return std::sqrt(re * re + im * im);
}

// Local maxima of the magnitude response on a 0.5% log grid, strongest first.
static std::vector<double> peaks(const std::vector<float>& ir, double sr, double lo = 20, double hi = 16000) {
	std::vector<double> f, m;
	for (double hz = lo; hz < hi; hz *= 1.005) { f.push_back(hz); m.push_back(dftMag(ir, hz, sr)); }
	std::vector<std::pair<double, double>> pk;
	for (size_t i = 1; i + 1 < m.size(); i++)
		if (m[i] > m[i - 1] && m[i] >= m[i + 1]) pk.push_back({m[i], f[i]});
	std::sort(pk.rbegin(), pk.rend());
	std::vector<double> out;
	for (auto& p : pk) out.push_back(p.second);
	return out;
}

static bool near(double a, double b, double rel) { return std::fabs(a / b - 1) <= rel; }

static void twoPeakCheck(float sr, int os) {
	for (int swap = 0; swap < 2; swap++) {
		Rig r(sr, os);
		r.k.curveA = 1; r.k.peaking = 0.7f;
		r.k.peak1 = hzToV(swap ? 2000 : 300);
		r.k.peak2 = hzToV(swap ? 300 : 2000);
		auto ir = impulse(r, 1 << 15);
		auto pk = peaks(ir, sr);
		CHECK(pk.size() >= 2);
		if (pk.size() < 2) return;
		double a = std::min(pk[0], pk[1]), b = std::max(pk[0], pk[1]);
		INFO("  sr %g os %d swap %d peaks %.1f %.1f\n", sr, os, swap, a, b);
		// 4%: the other filter's slope pulls each peak slightly (DECISIONS 2).
		CHECK(near(a, 300, 0.04));
		CHECK(near(b, 2000, 0.04));
	}
}

TEST("1 two peaks at the knob frequencies, either order") { twoPeakCheck(48000, 2); }

static void singlePeakCheck(float sr, int os) {
	for (float hz : {50.f, 1000.f, 8000.f}) {
		Rig r(sr, os);
		r.k.curveA = 0; r.k.peaking = 0.9f; r.k.peak1 = hzToV(hz);
		auto ir = impulse(r, 1 << 16, 1);
		auto pk = peaks(ir, sr, hz / 3, std::min(hz * 3.0, sr * 0.45));
		CHECK(!pk.empty());
		if (pk.empty()) continue;
		INFO("  sr %g os %d target %g peak %.1f\n", sr, os, hz, pk[0]);
		CHECK(near(pk[0], hz, 0.02));
	}
}

TEST("2 single filter resonance where the knob says") { singlePeakCheck(48000, 2); }

TEST("3 equal peaks cancel") {
	std::mt19937 rng(1);
	std::normal_distribution<float> nd(0, 1);
	for (float hz : {100.f, 700.f, 5000.f}) {
		Rig r;
		r.k.curveA = 1; r.k.peaking = 0.6f; r.k.peak1 = r.k.peak2 = hzToV(hz);
		double so = 0, sl = 0;
		for (int i = 0; i < 48000; i++) {
			auto f = r.step(nd(rng));
			if (i > 4800) { so += f.out * f.out; sl += f.lp1 * f.lp1; }
		}
		double db = 10 * std::log10(so / sl);
		INFO("  %g Hz: OUT is %.1f dB below LP 1\n", hz, -db);
		CHECK(db < -40);
	}
}

TEST("4 DC blocked by the twin peak, passed by the low-pass") {
	Rig r;
	r.k.curveA = 1;
	Frame<float> f;
	for (int i = 0; i < 48000; i++) f = r.step(1.f);
	CHECK(std::fabs(f.out) < 1e-3f);
	Rig q;
	q.k.curveA = 0; q.k.peaking = 0;
	for (int i = 0; i < 48000; i++) f = q.step(1.f);
	CHECK_NEAR(f.out, 1.0, 0.2);
}

static float firstExcursion(float a, float b) {
	Rig r;
	r.k.curveA = 1; r.k.peaking = 0; r.k.peak1 = hzToV(a); r.k.peak2 = hzToV(b);
	float best = 0;
	for (int i = 0; i < 4800; i++) {
		auto f = r.step(1.f);
		if (std::fabs(f.out) > std::fabs(best)) best = f.out;
	}
	return best;
}

TEST("5 crossing the peaks flips the polarity") {
	float lo = firstExcursion(200, 2000), hi = firstExcursion(2000, 200);
	INFO("  excursions %g %g\n", lo, hi);
	CHECK(lo * hi < 0);
	CHECK(std::fabs(lo) > 0.3f);
	CHECK(std::fabs(hi) > 0.3f);
}

TEST("6 input B: curve B full matches input A, curve B closed is LP 2 inverted") {
	Rig a, b, c;
	a.k.curveA = 1; a.k.curveB = 0; b.k.curveA = 0; b.k.curveB = 1; c.k.curveB = 0;
	a.k.peaking = b.k.peaking = c.k.peaking = 0.8f;
	auto ia = impulse(a, 4096), ib = impulse(b, 4096, 0, true);
	auto oc = impulse(c, 4096, 0, true);
	Rig d; d.k.curveB = 0; d.k.peaking = 0.8f;
	auto lc = impulse(d, 4096, 2, true);
	double err = 0, ref = 0, err2 = 0;
	for (int i = 0; i < 4096; i++) { err += std::fabs(ia[i] - ib[i]); ref += std::fabs(ia[i]); err2 += std::fabs(oc[i] + lc[i]); }
	CHECK(err / ref < 1e-4);
	CHECK(err2 / ref < 1e-4);
}

// -60 dB decay time of LP 1 after a ping, in seconds.
static double pingDecay(float peakHz, float sr = 48000, int os = 2, float kMaxV = kMax) {
	Rig r(sr, os);
	r.e.kMaxValue = kMaxV;
	r.k.curveA = 1; r.k.peaking = 1; r.k.peak1 = hzToV(peakHz); r.k.peak2 = hzToV(2000);
	int block = (int)(sr * 0.01f);
	int n = (int)(sr * 12);
	std::vector<float> env;
	float m = 0;
	for (int i = 0; i < n; i++) {
		auto f = r.step(0, 0, i == 0 ? 1.f : 0.f);
		m = std::max(m, std::fabs(f.lp1));
		if ((i + 1) % block == 0) { env.push_back(m); m = 0; }
	}
	size_t pk = std::max_element(env.begin(), env.end()) - env.begin();
	size_t last = pk;
	for (size_t i = pk; i < env.size(); i++) if (env[i] >= env[pk] * 1e-3) last = i;
	return (last - pk) * 0.01;
}

TEST("7 ping rings 4 to 5 s at 200 Hz, shorter higher up") {
	double t200 = pingDecay(200), t800 = pingDecay(800);
	INFO("  decay 200 Hz %.2f s, 800 Hz %.2f s\n", t200, t800);
	CHECK(t200 >= 4.0 && t200 <= 5.0);
	CHECK(t800 >= 0.8 && t800 <= 1.6);
}

TEST("8 Endless self-oscillates at a stable level") {
	Rig r;
	r.k.endless = true; r.k.peaking = 1; r.k.curveA = 1;
	auto rms = [&](int n) { double s = 0; for (int i = 0; i < n; i++) { auto f = r.step(0); s += f.out * f.out; } return std::sqrt(s / n); };
	rms(96000);
	double a = rms(48000);
	for (int i = 0; i < 4; i++) rms(48000);
	double b = rms(48000);
	INFO("  rms %.3f then %.3f\n", a, b);
	CHECK(a > 0.5 && a < 8);
	CHECK(std::fabs(b / a - 1) < 0.1);
}

TEST("9 stability torture") {
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1, 1);
	for (int os : {1, 2, 4}) {
		Rig r(48000, os);
		bool ok = true;
		for (int i = 0; i < 48000 * 20; i++) {
			if (i % 4800 == 0) {
				r.k.peaking = (u(rng) + 1) / 2; r.k.selfFM = (u(rng) + 1) / 2; r.k.endless = u(rng) > 0;
				r.k.curveA = (u(rng) + 1) / 2; r.k.curveB = (u(rng) + 1) / 2;
				r.k.peak1 = 5 * u(rng); r.k.peak2 = 5 * u(rng); r.k.p1Mod = u(rng); r.k.linkMod = (u(rng) + 1) / 2;
				if (i % 48000 == 0) { r.k.peaking = 1; r.k.selfFM = 1; }
			}
			Frame<float> f;
			f.inA = 12 * u(rng); f.inB = 12 * u(rng); f.vOct = 10 * u(rng); f.p1In = 10 * u(rng);
			f.linkIn = 10 * u(rng); f.peakIn = 10 * u(rng); f.ping = u(rng) > 0.99f;
			r.e.process(f, r.k);
			for (float v : {f.out, f.lp1, f.lp2})
				if (!std::isfinite(v) || std::fabs(v) > 10.f) ok = false;
		}
		CHECK(ok);
	}
}

static double harmonicsDb(const std::vector<float>& y, double f0, double sr) {
	double fund = dftMag(y, f0, sr), h = 0;
	for (int n = 2; n <= 8; n++) { double m = dftMag(y, f0 * n, sr); h += m * m; }
	return 20 * std::log10(std::sqrt(h) / fund);
}

TEST("10 self FM adds harmonics") {
	double thd[2];
	for (int s = 0; s < 2; s++) {
		Rig r;
		r.k.curveA = 0; r.k.peaking = 0.3f; r.k.peak1 = hzToV(400); r.k.selfFM = (float)s;
		std::vector<float> y;
		for (int i = 0; i < 48000; i++) {
			auto f = r.step(4.f * std::sin(2 * M_PI * 200 * i / 48000.0));
			if (i >= 24000) y.push_back(f.out);
		}
		thd[s] = harmonicsDb(y, 200, 48000);
	}
	INFO("  THD %.1f dB -> %.1f dB\n", thd[0], thd[1]);
	CHECK(thd[1] - thd[0] >= 6);
}

static double zeroCrossHz(Rig& r, int n, float sr) {
	int crossings = 0; float prev = 0; int first = -1, lastc = 0;
	for (int i = 0; i < n; i++) {
		auto f = r.step(0);
		if (prev <= 0 && f.lp1 > 0) { if (first < 0) first = i; else { crossings++; lastc = i; } }
		prev = f.lp1;
	}
	return crossings ? crossings * sr / (lastc - first) : 0;
}

TEST("11 V/OCT tracking of the self-oscillation") {
	for (float v = -2; v <= 3; v += 1) {
		Rig r;
		r.k.endless = true; r.k.peaking = 1; r.k.curveA = 0; r.k.peak1 = 0; r.k.peak2 = 5.5f;
		auto run = [&](int n) { for (int i = 0; i < n; i++) { Frame<float> f; f.vOct = v; r.e.process(f, r.k); } };
		run(48000 * 3);   // low pitches take longer to build up from the noise floor
		// measure with V/OCT held
		int crossings = 0; float prev = 0; int first = -1, lastc = 0;
		for (int i = 0; i < 48000; i++) {
			Frame<float> f; f.vOct = v; r.e.process(f, r.k);
			if (prev <= 0 && f.lp1 > 0) { if (first < 0) first = i; else { crossings++; lastc = i; } }
			prev = f.lp1;
		}
		double hz = crossings * 48000.0 / (lastc - first);
		double want = kC4 * std::pow(2.0, v);
		INFO("  v %+g: %.1f Hz (want %.1f)\n", v, hz, want);
		CHECK(near(hz, want, 0.02));
	}
	(void)zeroCrossHz;
}

TEST("12 ping fires once per trigger") {
	Rig r;
	r.k.curveA = 0; r.k.peaking = 0; r.k.peak1 = hzToV(8000);
	int pulses = 0; bool above = false;
	for (int i = 0; i < 4800; i++) {
		auto f = r.step(0, 0, i == 100 ? 1.f : 0.f);
		bool a = f.lp1 > 1.f;
		if (a && !above) pulses++;
		above = a;
	}
	CHECK(pulses == 1);
}

TEST("13 sample rates and oversampling") {
	for (float sr : {44100.f, 96000.f})
		for (int os : {1, 2, 4}) { twoPeakCheck(sr, os); singlePeakCheck(sr, os); }
	for (int os : {1, 4}) { twoPeakCheck(48000, os); singlePeakCheck(48000, os); }
	for (float sr : {44100.f, 96000.f}) {
		double t = pingDecay(200, sr, 2);
		INFO("  sr %g decay %.2f\n", sr, t);
		CHECK(t >= 4.0 && t <= 5.0);
	}
}

TEST("14 CPU benchmark (informational)") {
	Rig r;
	r.k.peaking = 0.9f; r.k.selfFM = 0.3f;
	auto t0 = std::chrono::steady_clock::now();
	const int n = 48000 * 4;
	float acc = 0;
	for (int i = 0; i < n; i++) { auto f = r.step(std::sin(i * 0.01f)); acc += f.out; }
	double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  one channel at 2x: %.2f%% of a core at 48 kHz (scalar float; Rack runs 4 per float_4) %g\n", 100 * s / 4, acc * 0);
}

// --calibrate: bisection for kMax so a 200 Hz ping lasts 4.5 s (§4.5).
TEST("calibrate kMax (only with --calibrate)") {
	if (!g_render) return;
	double lo = 7.5, hi = 7.999;
	for (int i = 0; i < 30; i++) {
		double mid = 0.5 * (lo + hi);
		(pingDecay(200, 48000, 2, (float)mid) < 4.5 ? lo : hi) = mid;
	}
	std::printf("  kMax = %.5f gives %.2f s at 200 Hz\n", lo, pingDecay(200, 48000, 2, (float)lo));
}

TEST("debug response (only with -v)") {
	if (!g_verbose) return;
	Rig r;
	r.k.curveA = 1; r.k.peaking = 0.7f; r.k.peak1 = hzToV(300); r.k.peak2 = hzToV(2000);
	auto ir = impulse(r, 1 << 15);
	for (double hz : {100., 200., 250., 280., 300., 320., 500., 1000., 1800., 2000., 2200., 4000.})
		std::printf("  %6.0f Hz  %.3f\n", hz, dftMag(ir, hz, 48000));
}
