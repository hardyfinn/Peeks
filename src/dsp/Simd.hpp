// Small set of maths helpers with one overload for float (host tests) and, when built inside
// Rack, one for rack::simd::float_4 (spec §6). The engine templates call only these.
#pragma once
#include <cmath>
#include <cstdint>

namespace peeks {

inline float vexp(float x) { return std::exp(x); }
inline float vlog(float x) { return std::log(x); }
inline float vtan(float x) { return std::tan(x); }
inline float vcos(float x) { return std::cos(x); }
inline float vsqrt(float x) { return std::sqrt(x); }
inline float vmin(float a, float b) { return a < b ? a : b; }
inline float vmax(float a, float b) { return a > b ? a : b; }
inline float vclamp(float x, float lo, float hi) { return vmin(vmax(x, lo), hi); }

}  // namespace peeks

#ifdef PEEKS_RACK
#include <simd/Vector.hpp>
#include <simd/functions.hpp>
namespace peeks {
using rack::simd::float_4;
inline float_4 vexp(float_4 x) { return rack::simd::exp(x); }
inline float_4 vlog(float_4 x) { return rack::simd::log(x); }
inline float_4 vtan(float_4 x) { return rack::simd::tan(x); }
inline float_4 vcos(float_4 x) { return rack::simd::cos(x); }
inline float_4 vsqrt(float_4 x) { return rack::simd::sqrt(x); }
inline float_4 vmin(float_4 a, float_4 b) { return rack::simd::fmin(a, b); }
inline float_4 vmax(float_4 a, float_4 b) { return rack::simd::fmax(a, b); }
inline float_4 vclamp(float_4 x, float_4 lo, float_4 hi) { return vmin(vmax(x, lo), hi); }
}  // namespace peeks
#endif

namespace peeks {

// tanh, rational approximation (Pade 7/6), accurate to better than 1e-4 on |x| < 4.97 and
// clamped to +-1 outside it (spec §4.3).
template <typename T>
inline T fastTanh(T x) {
	x = vclamp(x, T(-4.97f), T(4.97f));
	T x2 = x * x;
	T num = x * (T(135135.f) + x2 * (T(17325.f) + x2 * (T(378.f) + x2)));
	T den = T(135135.f) + x2 * (T(62370.f) + x2 * (T(3150.f) + x2 * T(28.f)));
	return vclamp(num / den, T(-1.f), T(1.f));
}

// level * tanh(v / level)
template <typename T>
inline T softClip(T v, float level) {
	return T(level) * fastTanh(v * T(1.f / level));
}

}  // namespace peeks
