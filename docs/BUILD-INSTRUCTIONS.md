# Build instructions: PEEKS, a twin-peak resonant filter for VCV Rack

**Audience:** an Opus model implementing this plugin from scratch.
**Owner:** Hardy. Fluent in VCV Rack, does not read C++. Build the whole plugin in one go, test it yourself, hand over the finished result. No milestone check-ins.

**What it is:** a VCV Rack 2 plugin, brand and slug `Peeks`, with one module, **Peeks** (16 HP): two parallel resonant 3-pole low-pass filters whose outputs are subtracted, giving a band-pass with a resonant peak at each edge. Each peak has its own frequency and CV; the two peaks can cross, and when they meet the pass band closes. At high PEAKING it rings like a struck object when pinged. It is a behavioural re-imagining of the twin-peak filter designed by Rob Hordijk.

Research and sources: `docs/RESEARCH.md` (copy of `/mnt/project-files/peeks-vcv/01-research-and-proposal.md`).

**Decisions used here (Fable's defaults, 2026-11-15; Hardy may override any before the build):**

| Question | Answer used here |
|---|---|
| Faithfulness | Behavioural, from the published descriptions (Hordijk's 2009 Blippoo Box paper, the Hordijk Modular blog, the SDIY wiki, the Epoch Modular TWINPEAK manual). No schematic is used. Where no number is published, this spec gives an estimate marked **[est]**, kept in one constants file and listed in the accuracy ledger (§11) |
| Reference panel | The Hordijk Modular TwinPeak (two inputs, CURVE A/B, PEAKING, VCA), plus the Blippoo Box self-FM distortion, plus the Epoch's resonance CV |
| Names | Plugin `Peeks`, module `Peeks`, slug `Peeks`. "Hordijk", "Twin Peak", "TwinPeak", "Blippoo", "Epoch" never appear in plugin, module, panel, tag or preset names. The manual may say "inspired by Rob Hordijk's twin-peak filter" once |
| Additions | PING trigger input and button, LP 1 and LP 2 outputs, PEAK CV input, polyphony up to 16 channels, oversampling menu, Ring Normal/Endless menu |
| Look | Clean flat panel (§3). Text labels on every control and jack, no symbols |
| Repo | `hardyfinn/Peeks` (created by Hardy 2026-11-15), work on `main` |
| Builds | Apple silicon Mac only (Hardy's standing preference) |

---

## 0. Ground rules

1. **Behaviour first.** Implement the behaviour described here exactly. Where a number is marked [est], use it as given and log it.
2. **All tunable constants in one place:** `src/dsp/Constants.hpp`, one named `constexpr` per [est] value, each with a comment naming the section and saying "estimate".
3. **DSP separate from Rack.** The engine lives in `src/dsp/` as plain C++17 with no Rack includes, templated on the sample type so it runs as `float` in tests and `simd::float_4` in the module. Unit-tested on the host. `src/modules/Peeks.cpp` is a thin wrapper.
4. **No allocation, locks, file I/O or logging in `process()`.**
5. **Every param has a tooltip with a unit, every port a name.** Frequency knobs show Hz and the note name (e.g. `Peak 1: 440.0 Hz (A4)`).
6. **Stability.** Clamp every state variable to ±50; end each `process()` with an `isfinite` guard per channel that resets that channel's state to 0. Outputs stay within ±10 V.
7. **Ambiguity:** pick the reading closest to §2–§5, write it in `docs/DECISIONS.md`, list it in the hand-over.
8. **CPU at 48 kHz, 2× oversampling:** under 1.0% of one core for 1 channel, under 3% for 16 channels (4 × `float_4`).
9. **Persistence:** menu choices save to the patch via `dataToJson`/`dataFromJson`.
10. Licence GPL-3.0-or-later.

---

## 1. Setup

- **VCV Rack 2.x**, newest Rack SDK. Read https://vcvrack.com/manual/PluginDevelopmentTutorial, https://vcvrack.com/manual/Panel, https://vcvrack.com/manual/DSP and https://vcvrack.com/manual/Polyphony first.
- `make`, `make dist`, `make install` with `RACK_DIR` set.
- Unit tests with **doctest** in `tests/`; `make test` builds `tests/*.cpp` with `src/dsp/*.cpp` as a host executable and runs it.
- `.github/workflows/build.yml`: reuse the working workflow from `hardyfinn/tetrax` or `hardyfinn/silverbox` (both build Mac arm64 green). Build macOS arm64 only, run `make test` on the runner, upload the `.vcvplugin` artifact on every push, attach it to a release on tags `v*`. The Rack SDK download is blocked from the cloud container, so Rack builds happen only on Actions; `make test` runs locally.

```
plugin.json  Makefile  README.md  LICENSE
docs/  BUILD-INSTRUCTIONS.md (this file)  RESEARCH.md  DECISIONS.md  MANUAL.md  Peeks-Manual.pdf
res/   Peeks.svg  components/
scripts/ make_panel.py  make_manual_pdf.sh  render_demo.cpp
src/
  plugin.hpp  plugin.cpp
  dsp/  Constants.hpp  Saturate.hpp  ThreePole.hpp  PeeksEngine.hpp  Oversampler.hpp
  modules/  Peeks.cpp
  widgets/  PeeksComponents.hpp  PanelLayout.hpp (generated)
tests/ test_threepole.cpp  test_engine.cpp  test_main.cpp
```

`plugin.json`: slug `Peeks`, name `Peeks`, brand `Peeks`, version `2.0.0`, author Hardy. Module: slug `Peeks`, name `Peeks`, description "Twin-peak resonant filter: two resonant low-pass filters subtracted, crossable peaks, pingable", tags `Filter`, `Polyphonic`, `Dual`. Make sure Rack's browser finds it under "Peeks", "filter", "twin", "resonator", "ping", "band-pass", "formant" (put the extra words in the description).

---

## 2. Signal conventions

| Signal | Level |
|---|---|
| IN A, IN B | Audio, nominal ±5 V, accepted up to ±12 V (saturation in §4.3 handles the rest) |
| PING input | Trigger. Schmitt: high above 1.5 V, low below 0.5 V [est] |
| V/OCT | 1 V/oct, added to both peaks, ±10 V |
| P1 MOD, P2 MOD, LINK MOD | ±10 V; at full attenuverter travel 1 V = 1 octave (Epoch: "approximately 1V/O") |
| PEAK CV | ±10 V; at full attenuverter travel 10 V = full PEAKING range |
| VCA CV | 0 to 10 V unipolar, negative clipped to 0. Normalled to +10 V |
| VCA IN | Audio, normalled to OUT |
| OUT, LP 1, LP 2, VCA OUT | Audio, nominal ±5 V, soft ceiling ±10 V |

Polyphony: channel count N = max(channels of IN A, IN B, PING, V/OCT, P1 MOD, P2 MOD, LINK MOD, PEAK CV), at least 1. Monophonic CVs apply to all channels. All outputs carry N channels. VCA channels: N if VCA IN is unpatched, else max(N, channels of VCA IN, VCA CV).

---

## 3. Panel (16 HP = 81.28 mm × 128.5 mm)

Style, as in Tetrax:
- **Flat matte off-white** background `#F2F1ED`, **near-black** text and lines `#1B1B1B`, accent **deep violet** `#5B3F8C` for section headings and the thin rules under them.
- No gradients, textures, brushed metal, drop shadows or drawn screws. Rack's standard black screws.
- One sans-serif typeface converted to paths (Inter or DejaVu Sans), labels 7 pt, section headings 8 pt bold capitals, module name `PEEKS` 12 pt bold at the top, nothing else at the bottom.
- Knobs: `RoundHugeBlackKnob` for PEAK 1/2, `RoundLargeBlackKnob` for PEAKING, `RoundBlackKnob` for CURVE A/B and SELF FM, `RoundSmallBlackKnob` for the rest. Jacks `PJ301MPort`. Outputs sit on a dark rounded rectangle labelled OUTPUTS. PING button: `VCVButton` with a white light.
- Every control and jack has a printed text label. No icons.
- A thin line diagram above the jacks is **not** wanted; keep it clean.
- Generate the SVG with `scripts/make_panel.py` (text to paths), keeping coordinates in the script and exporting `src/widgets/PanelLayout.hpp` so panel and widgets never drift.

Layout (mm, centres; adjust slightly if labels collide, keep the order):

| Row (y) | Contents (x) |
|---|---|
| 24 | **PEAK 1** (20.3), **PEAK 2** (61.0). Small LED under each (y 34) showing that filter's output level |
| 44 | **P1 MOD** (14), **LINK MOD** (40.6), **P2 MOD** (67.3) |
| 60 | **CURVE A** (14), **PEAKING** (40.6), **CURVE B** (67.3) |
| 76 | **SELF FM** (14), **PEAK CV** (32), **PING** button (49), **LEVEL** (67.3) |
| 92 | Jacks: **IN A** (10), **IN B** (25.5), **PING** (41), **V/OCT** (56.5), **PEAK CV** (72) |
| 104 | Jacks: **P1 MOD** (10), **LINK MOD** (25.5), **P2 MOD** (41), **VCA CV** (56.5), **VCA IN** (72) |
| 116 | Outputs: **LP 1** (10), **LP 2** (25.5), **OUT** (41), **VCA OUT** (72); OUT gets a small LED |

Section headings (accent): PEAKS over rows 24–44, SHAPE over 60–76, INPUTS over 92, CV over 104, OUTPUTS over 116.

---

## 4. The engine (`PeeksEngine.hpp`, `ThreePole.hpp`)

### 4.1 Block diagram

```
IN A ─────────────┬──────────────────────────► (+) ─► FILTER 1 (3-pole LP, peak f1) ─► LP 1 ─┐
                  └─ × cA ─► (−) ┐               ▲                                            (+) ─► OUT
IN B ─────────────┬──────────────┼──► (−) ─► FILTER 2 (3-pole LP, peak f2) ─► y2 ─► LP 2     │
                  └─ × cB ───────┼────────────────┘                          (−1 sums in) ───┘
PING ─► impulse ──► added to IN A
```

Exactly:

```
x1 = A + cB · B          (filter 1 input)
x2 = B + cA · A          (filter 2 input)
LP1 = F1(x1)             LP2 = F2(x2)
OUT = LP1 − LP2
```

so for input A alone: OUT = F1(A) − cA·F2(A): CURVE A = 0 gives a resonant low-pass at peak 1; CURVE A = 1 gives the twin-peak band-pass. For input B alone: OUT = cB·F1(B) − F2(B): CURVE B = 0 gives a resonant low-pass at peak 2 (inverted polarity, matching the hardware's inverted input); CURVE B = 1 gives the twin-peak band-pass (same response as A, opposite sign). The LP 1 and LP 2 jacks carry LP1 and LP2 (LP 2 not inverted). This follows the Hordijk blog ("Input A is sent to Filter 1 and the Curve A knob sets how much of that signal goes into Filter 2") with the subtraction done after the filters, which is equivalent.

Consequences that the tests must confirm (§8): equal peaks with both curves at 1 cancel; DC is blocked when the curve is 1; crossing the peaks flips the output polarity; nothing breaks when f1 > f2 or f1 < f2.

### 4.2 Each filter: 3-pole resonant low-pass

Three identical one-pole low-pass stages in series with negative feedback from the third stage to the input, gain k (the paper: "phase shifts of 60° after the first pole and 120° after the second", so the loop reaches 180° at the third).

Analog prototype: each stage H(s) = ωc / (s + ωc). The loop phase reaches −180° where each stage gives −60°, i.e. at **ω_peak = √3 · ωc**. Each stage's gain there is 1/2, so the loop gain is k/8 and **k = 8 is the oscillation threshold**.

Therefore, for a requested peak frequency f_peak: **stage cutoff fc = f_peak / √3** (constant `kPeakRatio = 1.7320508`). The PEAK knob shows f_peak.

Discretise with Zavalishin TPT (trapezoidal, zero-delay feedback) one-poles at the oversampled rate fs':

```
g  = tan(π · fc / fs')            (clamp fc to 0.45 · fs' before tan)
G  = g / (1 + g)
stage(v, s): y = G · (v − s) + s ;  s ← 2y − s     // standard TPT one-pole, s is the state
```

Solve the feedback loop without a unit delay. Each stage's output is y_i = G·x_i + S_i with S_i = (1 − G)·s_i, so the cascade output is y3 = G³·u + Σ with **Σ = G²·S1 + G·S2 + S3** (depends only on the states). Then:

```
u_lin = (x − k·Σ) / (1 + k·G³)            // linear zero-delay solution
y3_est = G³·u_lin + Σ
u = sat(x − k · y3_est)                   // one-step nonlinear correction (§4.3)
run the three stages on u, each stage input passed through satStage() (§4.3), update states
LP = y3 · makeup(k)
```

`makeup(k) = 1 + kMakeup · k` with kMakeup = 0.5 [est]: a 3-pole loop's DC gain is 1/(1+k), so without makeup the low end drops by 19 dB at full resonance. Partial makeup keeps the "thinning" character without the level collapse. Both filters use the same k and makeup, so cancellation at equal peaks still holds.

### 4.3 Saturation (`Saturate.hpp`)

- Loop input: `sat(v) = kSatV · tanh(v / kSatV)` with kSatV = 6 V [est]. This bounds the ringing and makes very hot pings and self-oscillation round and stable.
- Stage inputs: `satStage(v) = kStageV · tanh(v / kStageV)` with kStageV = 12 V [est], a gentle OTA-like softness that only shows at big levels.
- Use a fast rational tanh approximation valid to ±1% over ±5 (e.g. Padé 7/6 clamped), vectorised for `float_4`.

### 4.4 Frequency control

Per channel, per peak, in volts (octaves) relative to C4 = 261.626 Hz:

```
v1 = knob1 + V/OCT + p1mod_knob · P1MOD_in + link_knob · LINKMOD_in + selfFM1
v2 = knob2 + V/OCT + p2mod_knob · P2MOD_in + link_knob · LINKMOD_in + selfFM2
f_peak = 261.626 · 2^v ,  clamped to [8 Hz, min(20 kHz, 0.4 · fs')]
```

- PEAK 1 / PEAK 2 knobs: v from −4.0 to +5.5 (16.35 Hz to 11.8 kHz) [est; no published range]. Defaults: PEAK 1 = −1.0 (130.8 Hz), PEAK 2 = +1.0 (523.3 Hz).
- P1 MOD, P2 MOD: attenuverters −1…+1, default 0 (no effect at noon, as on the hardware).
- LINK MOD: attenuator 0…1, default 0. (Hordijk "CUTOFF MOD", Epoch "Dual Mod".)
- Exponential tracking is exact (the Epoch tracks "5 octaves at 1 or 2%"; we do better and say so in the manual).
- Compute g with a fast 2^x and tan approximation per sample at the oversampled rate (audio-rate FM must work, the blog describes "metallic" sounds from an oscillator into CUTOFF MOD). A cached `tan` lookup with linear interpolation over normalised frequency is fine.

### 4.5 PEAKING (shared resonance)

```
p = clamp(PEAKING_knob + PEAKCV_knob · PEAKCV_in / 10, 0, 1)
k = kMax · p^kPeakCurve
```

- kPeakCurve = 1.6 [est], so the lower half of the knob is musical and the top fifth is the ringing zone.
- **kMax is calibrated**, not guessed: choose kMax so that with Ring = Normal, PEAKING = 1, CURVE A = 1, peak 1 = 200 Hz, peak 2 = 2 kHz, a single PING decays by 60 dB in **4.5 s ± 0.5 s** measured on LP 1 (the Epoch manual: "when either peak is tuned to approximately 200hz, it will sustain for 4 to 5 seconds"). Because decay in cycles is constant, higher peaks ring for shorter times, as the hardware does ("longer duration at low frequencies"). Find kMax by bisection in a test helper at 48 kHz/2×, write the value into Constants.hpp, and keep the test (§8) that checks it. Expect kMax just below 8 (around 7.98).
- **Ring = Endless** (menu): above p = 0.9, k rises linearly from kMax·0.9^1.6 to 8.6 at p = 1 [est] (below 0.9 the law is unchanged). The filter then self-oscillates at the top of the knob, held at a stable level by `sat()`. The hardware is not documented to self-oscillate continuously; this is an option, default Normal.
- Add a tiny DC-free noise floor (1e-6 V white noise) to each loop input so Endless starts oscillating without an input.

### 4.6 SELF FM (Blippoo-style distortion)

The Blippoo paper feeds the resonator's second-pole output back to its own frequency control through one resistor, giving harmonic distortion. Per filter:

```
selfFM_i = SELF_FM_knob · kSelfFMDepth · y2_i(previous sample) / 5 V    (in octaves)
```

kSelfFMDepth = 2.0 octaves at full knob and 5 V [est]. Knob 0…1, default 0. One-sample delay at the oversampled rate is fine here. Clamp the resulting v as §4.4.

### 4.7 PING

- PING input (Schmitt, §2) and PING button each fire a pulse into IN A's path (so it reaches filter 1 directly and filter 2 through CURVE A, exactly like a pulse patched into IN A).
- Pulse: a raised-cosine of 0.25 ms width and 5 V peak [est], generated at the oversampled rate. Per channel for polyphonic PING; the button pings all channels.

### 4.8 VCA

```
cv = VCA CV patched ? clamp(VCA_CV_in, 0, 10) / 10 : 1
VCA_OUT = (VCA IN patched ? VCA_IN : OUT) · LEVEL_knob · cv
```

LEVEL knob 0…1, default 1, linear [est]; response curve of the hardware VCA is not documented. Apply a one-pole 1 ms smoothing to `LEVEL_knob · cv` to avoid zipper clicks, but let audio-rate CV through above that (i.e. smooth only the knob, not the CV). The VCA runs at the base rate, not oversampled.

### 4.9 Output stage and oversampling

- Oversampling menu: 1×, **2× (default)**, 4×. Upsample inputs (A, B, and the per-sample CV sums) with a polyphase half-band FIR (or Rack's `dsp::Upsampler`/`Decimator`); decimate LP1, LP2 with a matching filter. OUT = LP1 − LP2 computed after decimation.
- Output soft ceiling: `10 · tanh(v / 10)` on OUT, LP 1, LP 2, VCA OUT.
- No DC blocker on OUT (CURVE = 0 low-pass must pass DC).
- Bypass (Rack's module bypass): IN A to OUT.

### 4.10 Lights

- PEAK 1 / PEAK 2 LEDs: envelope follower (10 ms attack, 200 ms release) of |LP1| and |LP2| of channel 0, brightness = level / 5 V.
- OUT LED: the same for OUT. PING button light flashes 50 ms on each ping.

---

## 5. Parameters (exact list)

| Param | Range | Default | Tooltip |
|---|---|---|---|
| PEAK1 | −4 … 5.5 V | −1 | "Peak 1" Hz + note |
| PEAK2 | −4 … 5.5 V | +1 | "Peak 2" Hz + note |
| P1MOD | −1 … 1 | 0 | "Peak 1 CV amount" %, "1 V/oct at full" |
| P2MOD | −1 … 1 | 0 | "Peak 2 CV amount" % |
| LINKMOD | 0 … 1 | 0 | "Link CV amount (both peaks)" % |
| PEAKING | 0 … 1 | 0.5 | "Peaking" % |
| PEAKCV | −1 … 1 | 0 | "Peaking CV amount" % |
| CURVEA | 0 … 1 | 1 | "Curve A (low-pass to twin peak)" % |
| CURVEB | 0 … 1 | 1 | "Curve B (low-pass to twin peak)" % |
| SELFFM | 0 … 1 | 0 | "Self FM" % |
| LEVEL | 0 … 1 | 1 | "VCA level" % |
| PING | button | | "Ping" |

Right-click menu: **Oversampling** (1×, 2×, 4×), **Ring** (Normal, Endless). Both saved with the patch.

Knob smoothing: PEAK and MOD knobs feed v directly (they are exponential already); add a 2 ms one-pole on knob values only, so turning a knob does not zipper. CVs are never smoothed.

---

## 6. Polyphony and SIMD

Process channels in groups of 4 with `simd::float_4`; all engine code is templated on T. Per-channel state: 3 stage states × 2 filters, previous y2 × 2 for self-FM, ping pulse phase, Schmitt state, oversampler states. Allocate for 16 channels up front.

---

## 7. Leaning on existing modules (document in the manual)

Peeks is a filter only. For the recipes use free Library modules and say why in each:
- **Sources:** VCV VCO (saw, pulse, audio-rate FM into the MOD inputs), VCV Noise.
- **Ping sources and rhythm:** VCV Clock / LFO pulse, VCV SEQ-3, Bernoulli-style gates (VCV Fundamental `Random` or any S&H for random peak CV).
- **Chaos à la Blippoo:** two VCV VCOs cross-modulating, plus a shift-register "rungler" style source if the user has one (Hardy's Tudor and Fallen plugins have suitable chaotic CV modules; check the names in those repos before citing them, otherwise use VCV Random).
- **Envelopes for the VCA:** VCV ADSR.
- **Space:** Valley Plateau.

---

## 8. Behaviour tests (doctest, on `PeeksEngine<float>` at 48 kHz, 2×)

1. **Peak positions:** CURVE A = 1, PEAKING = 0.7, peaks at 300 Hz and 2 kHz. Measure magnitude response with a sine sweep (or impulse + FFT, 2^16 points). Two local maxima within 3% of 300 Hz and 2 kHz. Repeat with peaks swapped: same maxima.
2. **Single filter resonance frequency:** filter alone, PEAKING = 0.9, f_peak = 1 kHz: response peak within 2% of 1 kHz (checks √3 mapping and prewarp). Repeat at 50 Hz and 8 kHz.
3. **Cancellation:** peaks equal (any value 100 Hz…5 kHz), CURVE A = 1: RMS of OUT for white noise input is at least 40 dB below RMS of LP 1.
4. **DC blocking / passing:** CURVE A = 1: DC input gives |OUT| < 1 mV after 1 s. CURVE A = 0: DC 1 V input gives OUT within 20% of 1 V · makeup/(1+k) at PEAKING 0 (≈1 V).
5. **Polarity flip on crossing:** CURVE A = 1, PEAKING = 0, input a 1 V step. With f1 = 200, f2 = 2000 the OUT step response's first major excursion has one sign; with f1 = 2000, f2 = 200 the opposite sign.
6. **Curve B symmetry:** IN B with CURVE B = 1 gives OUT equal to −(IN A with CURVE A = 1) within 1e-4 relative.
7. **Ping decay calibration:** §4.5 setup, −60 dB decay time on LP 1 in 4.0…5.0 s. With peak 1 = 800 Hz, decay is between 0.8 and 1.6 s (roughly a quarter).
8. **Endless:** Ring = Endless, PEAKING = 1, no input: after 2 s, OUT RMS > 0.5 V and < 8 V, stable (no growth over the next 5 s by more than 10%).
9. **Stability torture:** 60 s of random inputs (±12 V audio, ±10 V CVs at audio rate, random PEAKING and SELF FM including 1): all outputs finite and within ±10 V.
10. **Self FM adds harmonics:** 200 Hz sine into IN A, CURVE A = 0, PEAKING 0.3, peak 1 = 400 Hz: THD of OUT increases by at least 6 dB going from SELF FM 0 to 1.
11. **V/OCT tracking:** Endless self-oscillation at PEAK 1 = 0 V; V/OCT −2…+3 V: oscillation frequency follows 2^v within 2% (filter 2 placed far away, CURVE A = 0).
12. **Ping input:** a trigger produces exactly one pulse; a held high gate produces one; polyphonic triggers ping only their own channel.
13. **Sample rates:** tests 1, 2 and 7 also pass at 44.1 kHz and 96 kHz, and at 1× and 4×.
14. **CPU benchmark (host, informational):** samples/second for 1 and 16 channels at 2×, printed by `make test`.

General module tests (manual check in the hand-over, plus whatever can be automated): plugin loads; module found by every word in §1; all tooltips and port names present; save/load restores menu choices; sample-rate change keeps peaks in tune; CPU within §0 on the Mac build.

---

## 9. Demo render (`scripts/render_demo.cpp`)

A small host program linking `src/dsp/` that writes `docs/peeks-demo.wav` (48 kHz, 16-bit stereo, ~30 s): (a) clocked pings with random peaks (left = OUT, right = LP 2), (b) a saw through the twin peaks sweeping together until they cancel and apart again, (c) audio-rate FM metallic tones, (d) Endless self-oscillation with SELF FM. Copy to `/mnt/project-files/peeks-vcv/peeks-demo.wav` if that folder is reachable.

---

## 10. Manual (`docs/MANUAL.md` and `docs/Peeks-Manual.pdf`)

Same structure as the Silverbox, Tudor and Tetrax manuals: one line saying Peeks is inspired by the twin-peak filter designed by the late Rob Hordijk (Blippoo Box, Hordijk Modular) and that this is an independent behavioural re-imagining; a labelled panel drawing; a short "how it works" with a simple drawing of the two low-pass curves and their difference; every control in plain English; the signal table; what is documented versus estimated (from the ledger); and **patch recipes** (modules needed and settings for each):

1. **Struck resonator:** clock into PING, PEAKING 100%, CURVE A full, peaks 150 Hz and 1.2 kHz. Two random CVs (S&H on the clock) into P1 MOD and P2 MOD at about 40%: pitched wooden and metallic hits that never repeat.
2. **Closing the gap:** saw into IN A, CURVE A full, PEAKING 70%. Slow LFO into P1 MOD (+) and P2 MOD (−): the two peaks sweep towards each other until the sound vanishes, then reopen.
3. **Talking filter:** saw or pulse at 110 Hz, peaks at about 700 Hz and 1.2 kHz (an "ah"), then 300 Hz and 2.3 kHz ("ee"); two sequenced CV rows into P1 and P2 MOD for vowel sequences.
4. **Two filters in one:** CURVE A and CURVE B at 0, a bass into IN A and a lead into IN B, take LP 1 and LP 2 separately: two independent resonant low-passes. Mix both through OUT for the inverted-sum blend.
5. **Metallic FM:** pulse VCO into IN A, a sine VCO at audio rate into LINK MOD at 30–60% (the Hordijk blog recipe): clangy, bell-like tones.
6. **Blippoo-ish chaos:** two VCOs cross-modulating, a comparator/pulse from one into PING, the other's triangle into P1 MOD, a stepped random into P2 MOD, SELF FM 40%: the restless chattering resonator sound.
7. **Self-oscillating pair:** Ring = Endless, PEAKING full, no input, V/OCT from a sequencer: two sine-ish voices whose difference you hear at OUT; detune PEAK 2 for beating.
8. **Percussion kit from noise:** short noise bursts (noise through VCV VCA with a fast ADSR) into IN A, three sequencer rows into P1 MOD, LINK MOD and PEAK CV: kick, tom and snare-ish hits from one module.
9. **VCA as a low-pass gate feel:** ADSR into VCA CV, pings through VCA OUT, PEAKING 85%: short plucks.

Build the PDF with `scripts/make_manual_pdf.sh` (as in the Tetrax repo). Copy it to `/mnt/project-files/peeks-vcv/Peeks-Manual.pdf` if that folder is reachable.

---

## 11. Order of work

1. Repo skeleton, plugin.json, CI (Mac arm64), `make test` harness. Copy this file to `docs/BUILD-INSTRUCTIONS.md` and the research doc to `docs/RESEARCH.md`.
2. `Saturate`, `ThreePole` + tests 2, 9 (single filter).
3. `PeeksEngine` (routing, curves, ping, self FM, VCA, oversampling) + tests 1–14; calibrate kMax (§4.5).
4. Panel (`make_panel.py`), module wrapper, SIMD polyphony, lights, menus, persistence.
5. CI green, tag `v2.0.0`.
6. Demo render, manual and PDF, README with install steps.
7. Hand-over: what was built, every [est] value used and the calibrated kMax, every ambiguity decided, CI run link, release link, demo path.

---

## 12. Accuracy ledger

| Item | Value used | Basis |
|---|---|---|
| Two parallel 3-pole low-passes, outputs subtracted | As §4.1 | **Documented** (Blippoo paper, Hordijk blog, SDIY) |
| Shared resonance, separate peak frequencies, per-peak and linked mod | As §4.4, §4.5 | **Documented** (blog, SDIY, Epoch) |
| Peaks can cross; polarity flips; equal peaks cancel | Emerges from §4.1 | **Documented** (paper, blog, Epoch) |
| Curve: input crossfeed, closed = low-pass | §4.1 | **Documented** (blog); Epoch's knob runs the other way |
| MOD depth 1 V/oct at full | §2 | **Documented** approx. (Epoch) |
| Ping decay ~4.5 s at 200 Hz, longer at low frequencies | kMax calibrated | **Documented** (Epoch) |
| 60° per pole, feedback oscillation at k = 8, peak at √3·fc | §4.2 | Paper's 60°/120° figures + standard 3-pole maths |
| Filter core (one-pole TPT with tanh) | §4.2, §4.3 | [est]; core not published |
| Peak range | 16.35 Hz … 11.8 kHz | [est] |
| Makeup gain | 1 + 0.5 k | [est] |
| Loop / stage saturation | 6 V / 12 V tanh | [est] |
| PEAKING law | k = kMax · p^1.6 | [est] |
| Endless top | k up to 8.6 | [est]; hardware not documented to self-oscillate continuously |
| Self FM | 2 oct per 5 V at full, from stage 2 | Topology **documented** (paper); depth [est] |
| PING pulse | 0.25 ms raised cosine, 5 V | [est]; our addition |
| Trigger threshold | 1.5 V / 0.5 V | [est] |
| VCA | Linear, CV 0–10 V, normalled to +10 V | Normalling of VCA IN to filter out **documented** (SDIY); law [est] |
| Tracking | Exact | Hardware tracks ~5 octaves at 1–2% (Epoch); ours is better, stated in manual |

Each [est] value lives in `src/dsp/Constants.hpp`. If a test or listening suggests a better value, change it there and note it in DECISIONS.
