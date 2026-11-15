// The Peeks engine (spec §4): two resonant 3-pole low-pass filters, outputs subtracted.
// Plain C++17, no Rack includes; templated on the sample type so it runs as float in the host
// tests and as rack::simd::float_4 (four polyphony channels) in the module.
#pragma once
#include "Constants.hpp"
#include "Oversampler.hpp"
#include "Simd.hpp"

namespace peeks {

// Mono settings shared by every channel (knob values, already smoothed by the caller).
struct Knobs {
	float peak1 = kPeak1Default;   // volts (octaves) from C4
	float peak2 = kPeak2Default;
	float p1Mod = 0.f;             // -1..1 attenuverter
	float p2Mod = 0.f;
	float linkMod = 0.f;           // 0..1
	float peaking = 0.5f;          // 0..1
	float peakCv = 0.f;            // -1..1 attenuverter
	float curveA = 1.f;            // 0..1
	float curveB = 1.f;
	float selfFM = 0.f;            // 0..1
	bool endless = false;          // Ring: Endless (§4.5)
};

// Per-channel signals for one base-rate sample.
template <typename T>
struct Frame {
	// inputs
	T inA = T(0.f), inB = T(0.f);
	T vOct = T(0.f), p1In = T(0.f), p2In = T(0.f), linkIn = T(0.f), peakIn = T(0.f);
	T ping = T(0.f);               // 1 on the sample a ping fires, else 0
	// outputs
	T lp1 = T(0.f), lp2 = T(0.f), out = T(0.f);
};

// PEAKING (0..1) to loop gain k (§4.5).
template <typename T>
inline T peakingToK(T p, bool endless, float kMaxV = kMax) {
	p = vclamp(p, T(0.f), T(1.f));
	auto powCurve = [](T x) { return vexp(T(kPeakCurve) * vlog(vmax(x, T(1e-6f)))); };
	if (!endless)
		return T(kMaxV) * powCurve(p);
	T k0 = T(kMaxV) * powCurve(vmin(p, T(kEndlessKnee)));
	T over = vmax(p - T(kEndlessKnee), T(0.f)) * T(1.f / (1.f - kEndlessKnee));
	return k0 + over * (T(kEndlessTop) - k0);
}

// Peak frequency / stage cutoff for loop gain k (see docs/DECISIONS.md 1). For the loop
// H = 1 / ((1 + s)^3 + k) the magnitude peak sits at w^2 = sqrt(2k) - 1, which reaches the
// 60-degrees-per-pole point sqrt(3) at k = 8. Below k = 2 there is little peak, and the knob then
// sets the stage cutoff itself (ratio 1). Above 8 (Endless) the oscillation sits at sqrt(3).
template <typename T>
inline T peakRatio(T k) {
	T w2 = vsqrt(T(2.f) * vmax(k, T(0.f))) - T(1.f);
	return vsqrt(vclamp(w2, T(1.f), T(3.f)));
}

// One resonant 3-pole low-pass: three TPT one-poles in a zero-delay negative feedback loop (§4.2).
template <typename T>
struct ThreePole {
	T s1 = T(0.f), s2 = T(0.f), s3 = T(0.f);
	T y2 = T(0.f);   // second stage output, for self FM (§4.6)

	void reset() { s1 = s2 = s3 = y2 = T(0.f); }

	// g: TPT coefficient for the stage cutoff; k: loop gain. Returns the third stage output.
	T process(T x, T g, T k) {
		T G = g / (T(1.f) + g);
		T oneMinusG = T(1.f) - G;
		T G2 = G * G, G3 = G2 * G;
		T S1 = oneMinusG * s1, S2 = oneMinusG * s2, S3 = oneMinusG * s3;
		T sigma = G2 * S1 + G * S2 + S3;
		T uLin = (x - k * sigma) / (T(1.f) + k * G3);
		T y3est = G3 * uLin + sigma;
		T u = softClip(x - k * y3est, kSatV);

		T v1 = (softClip(u, kStageV) - s1) * G;
		T y1 = v1 + s1;
		s1 = y1 + v1;
		T v2 = (softClip(y1, kStageV) - s2) * G;
		T yy2 = v2 + s2;
		s2 = yy2 + v2;
		T v3 = (softClip(yy2, kStageV) - s3) * G;
		T y3 = v3 + s3;
		s3 = y3 + v3;
		y2 = yy2;

		const T lim = T(kStateClamp);
		s1 = vclamp(s1, -lim, lim);
		s2 = vclamp(s2, -lim, lim);
		s3 = vclamp(s3, -lim, lim);
		return y3;
	}
};

template <typename T>
struct PeeksEngine {
	float sampleRate = 48000.f;
	int factor = 2;
	ThreePole<T> f1, f2;
	Oversampler<T> up1, up2, down1, down2;
	T prevV1 = T(kPeak1Default), prevV2 = T(kPeak2Default);
	T pingPos = T(1e3f);   // seconds since the last ping
	uint32_t noiseState = 0x12345678u;
	float kMaxValue = kMax;   // overridden only by the calibration helper

	PeeksEngine() { setOversampling(2); }

	void setSampleRate(float sr) { sampleRate = sr; }

	void setOversampling(int f) {
		factor = (f == 1 || f == 4) ? f : 2;
		for (auto* o : {&up1, &up2, &down1, &down2}) {
			o->factor = factor;
			o->reset();
		}
	}

	void reset() {
		f1.reset();
		f2.reset();
		for (auto* o : {&up1, &up2, &down1, &down2}) o->reset();
		pingPos = T(1e3f);
	}

	// Latency of the oversampling filters in base-rate samples (0 at 1x).
	int latency() const { return factor == 1 ? 0 : factor == 2 ? 2 * kHbDelay : 2 * kHbDelay + kHbDelay; }

	float noise() {
		noiseState = noiseState * 1664525u + 1013904223u;
		return (float)(noiseState >> 8) * (1.f / 8388608.f) - 1.f;
	}

	// Stage coefficient for a peak at v volts (octaves from C4) at the oversampled rate (§4.2, §4.4).
	// invRatio = stage cutoff / peak frequency, from peakRatio(k).
	T coef(T v, T invRatio, float osRate) const {
		T f = T(kC4) * vexp(v * T(0.69314718f));
		float maxHz = kMaxHz < kMaxNyquistFrac * osRate ? kMaxHz : kMaxNyquistFrac * osRate;
		f = vclamp(f, T(kMinHz), T(maxHz));
		return vtan(f * T(3.14159265f / osRate)) * invRatio;
	}

	void process(Frame<T>& fr, const Knobs& kn) {
		const float osRate = sampleRate * factor;
		const float dt = 1.f / osRate;

		// §4.4 frequency in volts
		T common = fr.vOct + T(kn.linkMod) * fr.linkIn;
		T v1 = T(kn.peak1) + common + T(kn.p1Mod) * fr.p1In;
		T v2 = T(kn.peak2) + common + T(kn.p2Mod) * fr.p2In;
		v1 = vclamp(v1, T(-12.f), T(12.f));
		v2 = vclamp(v2, T(-12.f), T(12.f));

		// §4.5 loop gain
		T p = T(kn.peaking) + T(kn.peakCv * 0.1f) * fr.peakIn;
		T k = peakingToK(p, kn.endless, kMaxValue);
		T makeup = T(1.f) + T(kMakeup) * k;
		T invRatio = T(1.f) / peakRatio(k);

		// §4.1 routing: x1 = A + cB B, x2 = B + cA A
		T cA = T(kn.curveA), cB = T(kn.curveB);
		T x1 = fr.inA + cB * fr.inB;
		T x2 = fr.inB + cA * fr.inA;

		T x1s[4], x2s[4], y1s[4], y2s[4];
		up1.upsample(x1, x1s);
		up2.upsample(x2, x2s);

		// §4.7 ping
		pingPos = pingPos * (T(1.f) - fr.ping);
		const T selfAmt = T(kn.selfFM * kSelfFMDepth / 5.f);

		for (int i = 0; i < factor; i++) {
			T frac = T((i + 1) / (float)factor);
			T vv1 = prevV1 + (v1 - prevV1) * frac + selfAmt * f1.y2;
			T vv2 = prevV2 + (v2 - prevV2) * frac + selfAmt * f2.y2;

			// Smooth bump 16 ph^2 (1 - ph)^2: same shape as a raised cosine, no trig (§4.7).
			T ph = vmin(pingPos * T(1.f / kPingWidth), T(1.f));
			T bump = ph * (T(1.f) - ph);
			T pulse = T(16.f * kPingVolts) * bump * bump;
			pingPos = vmin(pingPos + T(dt), T(1e3f));

			// Noise floor only in Endless, so it can start by itself (§4.5).
			T n = T(kn.endless ? kNoiseFloor * noise() : 0.f);
			y1s[i] = f1.process(x1s[i] + pulse + n, coef(vv1, invRatio, osRate), k) * makeup;
			y2s[i] = f2.process(x2s[i] + cA * pulse - n, coef(vv2, invRatio, osRate), k) * makeup;
		}
		prevV1 = v1;
		prevV2 = v2;

		T lp1 = down1.downsample(y1s);
		T lp2 = down2.downsample(y2s);
		fr.lp1 = softClip(lp1, kOutCeiling);
		fr.lp2 = softClip(lp2, kOutCeiling);
		fr.out = softClip(lp1 - lp2, kOutCeiling);
	}
};

}  // namespace peeks
