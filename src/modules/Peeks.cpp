// Peeks: twin-peak resonant filter (spec §4, §5). A thin wrapper around peeks::PeeksEngine,
// run as float_4 so four polyphony channels share one engine.
#include "../plugin.hpp"
#include "../dsp/PeeksEngine.hpp"
#include "../generated/Layout.hpp"

using simd::float_4;

// Peak knobs: Hz plus the nearest note name (§0.5).
struct PeakQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float v = getValue();
		float hz = peeks::kC4 * std::pow(2.f, v);
		static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		int midi = (int)std::round(60.f + 12.f * v);
		int n = ((midi % 12) + 12) % 12;
		int oct = (int)std::floor(midi / 12.f) - 1;
		return string::f(hz < 1000.f ? "%.1f Hz (%s%d)" : "%.0f Hz (%s%d)", hz, names[n], oct);
	}
	void setDisplayValueString(std::string s) override {
		float hz = std::atof(s.c_str());
		if (hz > 0.f)
			setValue(std::log2(hz / peeks::kC4));
	}
};

struct Peeks : Module {
	enum ParamId {
		PEAK1_PARAM, PEAK2_PARAM, P1MOD_PARAM, LINKMOD_PARAM, P2MOD_PARAM,
		CURVEA_PARAM, PEAKING_PARAM, CURVEB_PARAM, SELFFM_PARAM, PEAKCV_PARAM,
		PING_PARAM, LEVEL_PARAM, PARAMS_LEN
	};
	enum InputId {
		INA_INPUT, INB_INPUT, PING_INPUT, VOCT_INPUT, PEAKCV_INPUT,
		P1MOD_INPUT, LINKMOD_INPUT, P2MOD_INPUT, VCACV_INPUT, VCAIN_INPUT, INPUTS_LEN
	};
	enum OutputId { LP1_OUTPUT, LP2_OUTPUT, OUT_OUTPUT, VCA_OUTPUT, OUTPUTS_LEN };
	enum LightId { LP1_LIGHT, LP2_LIGHT, PING_LIGHT, OUT_LIGHT, LIGHTS_LEN };

	// Knobs smoothed with a 2 ms one-pole (§5); index = ParamId.
	static constexpr int SMOOTHED[] = {PEAK1_PARAM, PEAK2_PARAM, P1MOD_PARAM, LINKMOD_PARAM, P2MOD_PARAM,
	                                   CURVEA_PARAM, PEAKING_PARAM, CURVEB_PARAM, SELFFM_PARAM, PEAKCV_PARAM};

	peeks::PeeksEngine<float_4> engines[4];
	dsp::TSchmittTrigger<float> pingTrig[16];
	dsp::BooleanTrigger pingButton;
	dsp::PulseGenerator pingFlash;
	dsp::ClockDivider lightDiv;
	float smooth[PARAMS_LEN] = {};
	float levelSmooth = 1.f;
	float knobCoef = 0.f, vcaCoef = 0.f;
	bool smoothInit = false;
	int oversampling = 2;
	bool endless = false;

	Peeks() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<PeakQuantity>(PEAK1_PARAM, peeks::kPeakMinV, peeks::kPeakMaxV, peeks::kPeak1Default, "Peak 1");
		configParam<PeakQuantity>(PEAK2_PARAM, peeks::kPeakMinV, peeks::kPeakMaxV, peeks::kPeak2Default, "Peak 2");
		configParam(P1MOD_PARAM, -1.f, 1.f, 0.f, "Peak 1 CV amount (1 V/oct at full)", "%", 0.f, 100.f);
		configParam(P2MOD_PARAM, -1.f, 1.f, 0.f, "Peak 2 CV amount (1 V/oct at full)", "%", 0.f, 100.f);
		configParam(LINKMOD_PARAM, 0.f, 1.f, 0.f, "Link CV amount, both peaks (1 V/oct at full)", "%", 0.f, 100.f);
		configParam(PEAKING_PARAM, 0.f, 1.f, 0.5f, "Peaking", "%", 0.f, 100.f);
		configParam(PEAKCV_PARAM, -1.f, 1.f, 0.f, "Peaking CV amount (10 V = full range)", "%", 0.f, 100.f);
		configParam(CURVEA_PARAM, 0.f, 1.f, 1.f, "Curve A (low-pass to twin peak)", "%", 0.f, 100.f);
		configParam(CURVEB_PARAM, 0.f, 1.f, 1.f, "Curve B (low-pass to twin peak)", "%", 0.f, 100.f);
		configParam(SELFFM_PARAM, 0.f, 1.f, 0.f, "Self FM", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 1.f, "VCA level", "%", 0.f, 100.f);
		configButton(PING_PARAM, "Ping (strike the filter)");

		configInput(INA_INPUT, "In A (filter 1, and filter 2 by Curve A)");
		configInput(INB_INPUT, "In B (filter 2, and filter 1 by Curve B)");
		configInput(PING_INPUT, "Ping trigger");
		configInput(VOCT_INPUT, "V/oct, both peaks");
		configInput(PEAKCV_INPUT, "Peaking CV");
		configInput(P1MOD_INPUT, "Peak 1 CV");
		configInput(LINKMOD_INPUT, "Link CV, both peaks");
		configInput(P2MOD_INPUT, "Peak 2 CV");
		configInput(VCACV_INPUT, "VCA CV (0 to 10 V, normalled to 10 V)");
		configInput(VCAIN_INPUT, "VCA (normalled to OUT)");

		configOutput(LP1_OUTPUT, "LP 1 (filter 1 low-pass)");
		configOutput(LP2_OUTPUT, "LP 2 (filter 2 low-pass)");
		configOutput(OUT_OUTPUT, "Filter (LP 1 minus LP 2)");
		configOutput(VCA_OUTPUT, "VCA");

		configLight(LP1_LIGHT, "LP 1 level");
		configLight(LP2_LIGHT, "LP 2 level");
		configLight(OUT_LIGHT, "Output level");

		configBypass(INA_INPUT, OUT_OUTPUT);
		lightDiv.setDivision(32);
		applyOversampling();
		onSampleRateChange();
	}

	void applyOversampling() {
		for (auto& e : engines) e.setOversampling(oversampling);
	}

	void onSampleRateChange() override {
		float sr = APP->engine->getSampleRate();
		for (auto& e : engines) e.setSampleRate(sr);
		knobCoef = 1.f - std::exp(-1.f / (peeks::kKnobSmooth * sr));
		vcaCoef = 1.f - std::exp(-1.f / (peeks::kVcaSmooth * sr));
	}

	void onReset() override {
		oversampling = 2;
		endless = false;
		applyOversampling();
		for (auto& e : engines) e.reset();
		smoothInit = false;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "oversampling", json_integer(oversampling));
		json_object_set_new(root, "endless", json_boolean(endless));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "oversampling")) oversampling = (int)json_integer_value(j);
		if (oversampling != 1 && oversampling != 4) oversampling = 2;
		if (json_t* j = json_object_get(root, "endless")) endless = json_boolean_value(j);
		applyOversampling();
	}

	// Rack voltage, made safe: NaN to 0, clamped (§0.6).
	float_4 safe(int id, int c, float lim) {
		float_4 v = inputs[id].getPolyVoltageSimd<float_4>(c);
		return simd::ifelse(v == v, simd::clamp(v, -lim, lim), float_4(0.f));
	}

	void process(const ProcessArgs& args) override {
		// Knob smoothing (§5).
		for (int id : SMOOTHED) {
			float target = params[id].getValue();
			smooth[id] = smoothInit ? smooth[id] + (target - smooth[id]) * knobCoef : target;
		}
		smoothInit = true;

		peeks::Knobs kn;
		kn.peak1 = smooth[PEAK1_PARAM];
		kn.peak2 = smooth[PEAK2_PARAM];
		kn.p1Mod = smooth[P1MOD_PARAM];
		kn.p2Mod = smooth[P2MOD_PARAM];
		kn.linkMod = smooth[LINKMOD_PARAM];
		kn.peaking = smooth[PEAKING_PARAM];
		kn.peakCv = smooth[PEAKCV_PARAM];
		kn.curveA = smooth[CURVEA_PARAM];
		kn.curveB = smooth[CURVEB_PARAM];
		kn.selfFM = smooth[SELFFM_PARAM];
		kn.endless = endless;

		// Polyphony (§2).
		int n = 1;
		for (int id = INA_INPUT; id <= P2MOD_INPUT; id++) n = std::max(n, inputs[id].getChannels());

		bool buttonPing = pingButton.process(params[PING_PARAM].getValue() > 0.f);
		if (buttonPing) pingFlash.trigger(0.05f);
		int pingCh = inputs[PING_INPUT].getChannels();

		for (int c = 0; c < n; c += 4) {
			peeks::Frame<float_4> fr;
			fr.inA = safe(INA_INPUT, c, 12.f);
			fr.inB = safe(INB_INPUT, c, 12.f);
			fr.vOct = safe(VOCT_INPUT, c, 10.f);
			fr.p1In = safe(P1MOD_INPUT, c, 10.f);
			fr.p2In = safe(P2MOD_INPUT, c, 10.f);
			fr.linkIn = safe(LINKMOD_INPUT, c, 10.f);
			fr.peakIn = safe(PEAKCV_INPUT, c, 10.f);
			float ping[4] = {};
			for (int i = 0; i < 4 && c + i < n; i++) {
				bool p = buttonPing;
				if (pingCh > 0) {
					float v = inputs[PING_INPUT].getPolyVoltage(c + i);
					if (pingTrig[c + i].process(v, peeks::kTrigLow, peeks::kTrigHigh)) {
						p = true;
						if (c + i == 0 || pingCh == 1) pingFlash.trigger(0.05f);
					}
				}
				ping[i] = p ? 1.f : 0.f;
			}
			fr.ping = float_4::load(ping);

			auto& e = engines[c / 4];
			e.process(fr, kn);
			// Non-finite guard (§0.6).
			if (simd::movemask(fr.out == fr.out) != 0xF) {
				e.reset();
				fr.out = fr.lp1 = fr.lp2 = 0.f;
			}
			outputs[LP1_OUTPUT].setVoltageSimd(fr.lp1, c);
			outputs[LP2_OUTPUT].setVoltageSimd(fr.lp2, c);
			outputs[OUT_OUTPUT].setVoltageSimd(fr.out, c);
		}
		outputs[LP1_OUTPUT].setChannels(n);
		outputs[LP2_OUTPUT].setChannels(n);
		outputs[OUT_OUTPUT].setChannels(n);

		// VCA (§4.8): level knob smoothed, CV not.
		levelSmooth += (params[LEVEL_PARAM].getValue() - levelSmooth) * vcaCoef;
		bool vcaIn = inputs[VCAIN_INPUT].isConnected();
		bool vcaCv = inputs[VCACV_INPUT].isConnected();
		int vn = vcaIn ? std::max(inputs[VCAIN_INPUT].getChannels(), vcaCv ? inputs[VCACV_INPUT].getChannels() : 1) : n;
		for (int c = 0; c < vn; c += 4) {
			float_4 x = vcaIn ? safe(VCAIN_INPUT, c, 12.f) : outputs[OUT_OUTPUT].getVoltageSimd<float_4>(c);
			float_4 g = vcaCv ? simd::clamp(safe(VCACV_INPUT, c, 10.f) * 0.1f, 0.f, 1.f) : float_4(1.f);
			outputs[VCA_OUTPUT].setVoltageSimd(x * g * levelSmooth, c);
		}
		outputs[VCA_OUTPUT].setChannels(vn);

		// Lights (§4.10).
		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			lights[LP1_LIGHT].setBrightnessSmooth(std::fabs(outputs[LP1_OUTPUT].getVoltage(0)) / 5.f, dt);
			lights[LP2_LIGHT].setBrightnessSmooth(std::fabs(outputs[LP2_OUTPUT].getVoltage(0)) / 5.f, dt);
			lights[OUT_LIGHT].setBrightnessSmooth(std::fabs(outputs[OUT_OUTPUT].getVoltage(0)) / 5.f, dt);
		}
		lights[PING_LIGHT].setBrightness(pingFlash.process(args.sampleTime) ? 1.f : 0.f);
	}
};

constexpr int Peeks::SMOOTHED[];

struct PeeksWidget : ModuleWidget {
	PeeksWidget(Peeks* module) {
		pkScaffold(this, module, "Peeks");
		placePeeks(this, module);
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = getModule<Peeks>();
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Oversampling", {"1x", "2x (default)", "4x"},
			[=]() { return m->oversampling == 1 ? 0 : m->oversampling == 4 ? 2 : 1; },
			[=](int i) { m->oversampling = i == 0 ? 1 : i == 2 ? 4 : 2; m->applyOversampling(); }));
		menu->addChild(createIndexSubmenuItem("Ring", {"Normal (long pings)", "Endless (self-oscillates at full Peaking)"},
			[=]() { return m->endless ? 1 : 0; },
			[=](int i) { m->endless = i == 1; }));
	}
};

Model* modelPeeks = createModel<Peeks, PeeksWidget>("Peeks");
