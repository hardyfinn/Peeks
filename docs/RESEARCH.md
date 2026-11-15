# Peeks: research and proposal

**For:** Hardy. **Date:** 2026-11-15. **Status:** design only; build when Hardy says "switch to Opus and build".
**Repo:** `hardyfinn/Peeks` (Hardy, 2026-11-15). Plugin and module are both called **Peeks**.

## 1. What the original is

The filter Hardy means is the twin-peak filter designed by the late Rob Hordijk. It exists in three related forms:

| Form | What it is | Controls |
|---|---|---|
| **Blippoo Box resonator** (2009 paper, *Leonardo Music Journal* 19) | Fixed "complex resonator" inside a chaotic instrument, pinged by the Box's comparator pulse train | Resonance fixed "at the point of near oscillation"; peaks moved by the chaotic core; a one-resistor self-FM distortion |
| **Hordijk Modular TwinPeak** (Rob's own modular system) | Panel filter with two inputs and a VCA | CUTOFF PEAK 1, CUTOFF PEAK 2, PEAK 1 MOD, PEAK 2 MOD, CUTOFF MOD, PEAKING, CURVE A, CURVE B, VCA |
| **Epoch Modular TWINPEAK** (Eurorack, licensed) | 12 HP Eurorack version | Peak A, Peak B, Mod A, Mod B, Dual Mod, Res/Ping, Curve, LVL/Fade |

Peeks follows the **Hordijk Modular panel** (best documented, two inputs, curve knobs, VCA), with the Blippoo self-FM and a few Rack extras.

## 2. How it works (and where each fact comes from)

**Documented** (source in brackets):

1. **Two parallel resonant low-pass filters, 3-pole (18 dB/oct) each** [Blippoo paper: "two parallel, highly resonant 3-pole low-pass filters"; Hordijk Modular blog: "two independent 18 dB lowpass filters in parallel"; SDIY wiki]. The Epoch manual says "12 dB"; we treat that as Epoch's own variant.
2. **The second filter's output is subtracted from the first** [paper]. In the modular it is done by inverting one filter's input, which "has exactly the same effect" [blog]. The result is a band-pass with **a resonant peak at each edge** of the pass band.
3. **One shared resonance control, PEAKING** [blog, SDIY].
4. **Separate frequency knob per peak**, plus a per-peak MOD attenuverter (no effect at 12 o'clock) and a linked CUTOFF MOD and 1 V/oct input that move both peaks together [blog, SDIY, Epoch].
5. **The peaks can cross.** It does not matter which is higher; the output is still band-pass, with **polarity inverted** when peak 2 is above peak 1 [blog, Epoch].
6. **When both peaks are tuned the same, the pass band closes** and the input is almost fully blocked [paper, Epoch]. Moving the peaks together and apart is the signature gesture.
7. **CURVE knobs** set how much of each input also goes to the other filter: closed, input A goes only to filter 1 (plain resonant low-pass); open, it goes to both and you get the twin-peak band-pass [blog, SDIY].
8. **Pinging:** at maximum PEAKING the filter rings strongly from pulses, gates and triggers, "tilting momentarily into self-oscillation"; pings last longer at low frequencies, **4 to 5 seconds at about 200 Hz** [Epoch].
9. **3-pole phase:** 60° per pole, so three poles give 180° and the feedback can make it ring [paper].
10. **Self-FM distortion:** in the Blippoo, the second pole's output is fed back to the resonator's own frequency control "via only one single resistor", giving harmonic distortion [paper].
11. **VCA** after the filter, its input normalled to the filter output [SDIY].
12. Audio-rate modulation of the peaks gives "metallic" sounds [blog].

**Not documented, so estimated** (all marked [est] in the build spec and kept in one constants file):
- The filter core (OTA, transistor, etc.). We model each 3-pole as three saturating one-pole stages in a negative feedback loop, which is the textbook way to get the paper's 60° per pole behaviour.
- Cutoff range: we use about 16 Hz to 12 kHz per peak.
- Resonance law, saturation level, self-FM depth, VCA response, mod depths beyond "about 1 V/oct" [Epoch].
- Continuous self-oscillation: no source says it sustains forever. Default stays just under it (long pings); a right-click option lets it sing.

## 3. The proposal: one module, Peeks (16 HP)

**Controls**
- **PEAK 1**, **PEAK 2**: the two peak frequencies (big knobs).
- **P1 MOD**, **P2 MOD**: attenuverters for each peak's CV; **LINK MOD**: attenuator for a CV that moves both.
- **PEAKING**: shared resonance, from gentle to long ringing pings.
- **CURVE A**, **CURVE B**: low-pass to twin-peak for each input.
- **SELF FM**: the Blippoo's grit, each filter modulating its own frequency.
- **PEAK CV**: attenuverter for a PEAKING CV input (from the Epoch; the Hordijk panel has none).
- **PING** button and **LEVEL** (VCA).

**Jacks:** IN A, IN B, PING (trigger), V/OCT, P1 MOD, LINK MOD, P2 MOD, PEAK CV, VCA CV, VCA IN; outputs OUT, LP 1, LP 2, VCA OUT.

**Additions over the hardware**, each one easy to ignore:
- PING trigger input and button: strike the filter without patching a pulse source.
- LP 1 and LP 2 outputs: each filter on its own, for stereo or for patching your own mix.
- PEAK CV input.
- Polyphony up to 16 channels.
- Oversampling choice (1×, 2× default, 4×) and a "Ring: Normal / Endless" option.

**Look:** same clean flat off-white panels as Tetrax, text labels only, one accent colour. The panel never says Hordijk or Twin Peak; the manual says once that it is inspired by Rob Hordijk's twin-peak filter.

**Searchable as:** Peeks, filter, twin, resonator, ping, band-pass, formant.

## 4. Defaults chosen (Hardy can override any)

| Question | Default |
|---|---|
| Which original | Hordijk Modular panel, plus Blippoo self-FM, plus Epoch's resonance CV |
| Number of modules | One |
| Curve direction | Knob fully left = low-pass (Hordijk "closed"); Epoch is the opposite |
| Ringing | Max PEAKING just under self-oscillation, calibrated to the Epoch's 4 to 5 s ping at 200 Hz; "Endless" in the menu |
| VCA | Included, CV normalled to +10 V so it passes sound when nothing is patched |
| Width | 16 HP |
| Builds | Apple silicon Mac only |

## Sources
- Rob Hordijk, "The Blippoo Box: A Chaotic Electronic Music Instrument, Bent by Design", *Leonardo Music Journal* 19 (2009): https://community.vcvrack.com/uploads/short-url/spfMxEMl5POk9MrzaXrnGa3dsSg.pdf
- The Hordijk Modular blog, "The TwinPeak filter": https://thehordijkmodular.blogspot.com/2018/02/the-twinpeak-filter-best-filter-ever-in.html
- Synth DIY Wiki, Rob Hordijk Twinpeak: https://sdiy.info/wiki/Rob_Hordijk_Twinpeak
- Epoch Modular TWINPEAK user manual: https://manualmachine.com/epochmodular/twinpeak/21462107-user-manual
- Web approximation (not a circuit model, used only for sanity checks): https://hordijk.netlify.app/
