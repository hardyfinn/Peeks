// Every tunable number of the Peeks engine (spec §0.2). Values marked "estimate" are not
// published for the original filter; see the accuracy ledger in docs/BUILD-INSTRUCTIONS.md §12.
#pragma once

namespace peeks {

// §4.2 Partial makeup gain 1 + kMakeup * k (estimate).
constexpr float kMakeup = 0.5f;
// §4.3 Loop-input saturation level in volts (estimate).
constexpr float kSatV = 6.f;
// §4.3 Stage-input saturation level in volts (estimate).
constexpr float kStageV = 12.f;

// §4.4 Peak knob range in volts (octaves) around C4 (estimate: 16.35 Hz to 11.8 kHz).
constexpr float kC4 = 261.6256f;
constexpr float kPeakMinV = -4.f;
constexpr float kPeakMaxV = 5.5f;
constexpr float kPeak1Default = -1.f;
constexpr float kPeak2Default = 1.f;
// §4.4 Frequency clamps.
constexpr float kMinHz = 8.f;
constexpr float kMaxHz = 20000.f;
constexpr float kMaxNyquistFrac = 0.4f;   // of the oversampled rate

// §4.5 PEAKING law k = kMax * p^kPeakCurve (curve is an estimate).
constexpr float kPeakCurve = 1.6f;
// §4.5 kMax, calibrated so a ping at 200 Hz decays by 60 dB in about 4.5 s (Epoch manual).
// Found by tests/test_engine.cpp "calibrate kMax" (run with --calibrate).
constexpr float kMax = 7.9508f;
// §4.5 Ring = Endless: above p = kEndlessKnee, k rises linearly to kEndlessTop (estimate).
constexpr float kEndlessKnee = 0.9f;
constexpr float kEndlessTop = 8.6f;
// §4.5 Noise floor injected into each loop in Endless so it starts by itself (volts).
constexpr float kNoiseFloor = 1e-6f;

// §4.6 Self FM: octaves per 5 V of the second stage's output at full knob (estimate).
constexpr float kSelfFMDepth = 2.f;

// §4.7 PING pulse: raised cosine, width in seconds and peak in volts (estimate).
constexpr float kPingWidth = 0.00025f;
constexpr float kPingVolts = 5.f;

// §2 Trigger thresholds (estimate).
constexpr float kTrigHigh = 1.5f;
constexpr float kTrigLow = 0.5f;

// §4.9 Output soft ceiling.
constexpr float kOutCeiling = 10.f;
// §0.6 State clamp.
constexpr float kStateClamp = 50.f;

// §5 Knob smoothing time constant (seconds), and VCA level smoothing.
constexpr float kKnobSmooth = 0.002f;
constexpr float kVcaSmooth = 0.001f;

}  // namespace peeks
