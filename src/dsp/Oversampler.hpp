// Polyphase half-band up- and down-samplers for 2x and 4x oversampling (spec §4.9).
// 79-tap Kaiser (beta 10) half-band: passband flat to 0.21 fs', stopband below -87 dB from 0.29 fs'.
#pragma once
#include "Simd.hpp"

namespace peeks {

// Odd-offset taps (first half; the filter is symmetric). The centre tap is 0.5.
static const float kHalfband[20] = {
	-2.8986536967e-06f,
	1.7008657849e-05f,
	-5.2234698427e-05f,
	1.2539275036e-04f,
	-2.6083179110e-04f,
	4.9190718437e-04f,
	-8.6234600631e-04f,
	1.4275638686e-03f,
	-2.2561875139e-03f,
	3.4323757578e-03f,
	-5.0601325509e-03f,
	7.2719509654e-03f,
	-1.0246523688e-02f,
	1.4245832583e-02f,
	-1.9696500894e-02f,
	2.7383912716e-02f,
	-3.8983391605e-02f,
	5.8869521854e-02f,
	-1.0316278273e-01f,
	3.1731850284e-01f
};
constexpr int kHbTaps = 40;     // odd-offset taps in the full filter
constexpr int kHbDelay = 19;    // centre tap delay in base-rate samples

template <typename T>
struct HbHistory {
	T buf[2 * kHbTaps];
	int pos = 0;
	void reset() { for (auto& b : buf) b = T(0.f); pos = 0; }
	void push(T x) {
		pos = (pos == 0) ? kHbTaps - 1 : pos - 1;
		buf[pos] = x;
		buf[pos + kHbTaps] = x;
	}
	// i samples ago (0 = newest)
	T at(int i) const { return buf[pos + i]; }
	T convolve() const {
		T acc = T(0.f);
		for (int i = 0; i < 20; i++)
			acc += T(kHalfband[i]) * (at(i) + at(kHbTaps - 1 - i));
		return acc;
	}
};

// One 2x stage: x at rate fs -> two samples at 2 fs.
template <typename T>
struct Up2 {
	HbHistory<T> h;
	void reset() { h.reset(); }
	void process(T x, T* out2) {
		h.push(x);
		out2[0] = T(2.f) * h.convolve();
		out2[1] = h.at(kHbDelay);
	}
};

// One 2x stage: two samples at 2 fs -> one at fs.
template <typename T>
struct Down2 {
	HbHistory<T> odd, even;
	void reset() { odd.reset(); even.reset(); }
	T process(const T* in2) {
		even.push(in2[0]);
		odd.push(in2[1]);
		return odd.convolve() + T(0.5f) * even.at(kHbDelay);
	}
};

// 1x, 2x or 4x.
template <typename T>
struct Oversampler {
	int factor = 2;
	Up2<T> upA, upB;
	Down2<T> downA, downB;
	void reset() {
		upA.reset(); upB.reset();
		downA.reset(); downB.reset();
	}
	void upsample(T x, T* out) {
		if (factor == 1) { out[0] = x; return; }
		if (factor == 2) { upA.process(x, out); return; }
		T mid[2];
		upA.process(x, mid);
		upB.process(mid[0], out);
		upB.process(mid[1], out + 2);
	}
	T downsample(const T* in) {
		if (factor == 1) return in[0];
		if (factor == 2) return downA.process(in);
		T mid[2];
		mid[0] = downB.process(in);
		mid[1] = downB.process(in + 2);
		return downA.process(mid);
	}
};

}  // namespace peeks
