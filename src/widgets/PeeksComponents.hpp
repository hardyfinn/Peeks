// Knobs, jack, toggle, buttons and lights. SVGs come from scripts/make_panels.py.
#pragma once
#include "../plugin.hpp"

inline std::shared_ptr<window::Svg> pkSvg(const std::string& name) {
	return window::Svg::load(asset::plugin(pluginInstance, "res/components/" + name + ".svg"));
}

struct PkKnobBase : app::SvgKnob {
	PkKnobBase() { minAngle = -0.75f * M_PI; maxAngle = 0.75f * M_PI; }
};
struct PkKnobHuge : PkKnobBase { PkKnobHuge() { setSvg(pkSvg("PkKnobHuge")); shadow->opacity = 0.15f; } };
struct PkKnobLarge : PkKnobBase { PkKnobLarge() { setSvg(pkSvg("PkKnobLarge")); shadow->opacity = 0.15f; } };
struct PkKnob : PkKnobBase { PkKnob() { setSvg(pkSvg("PkKnob")); shadow->opacity = 0.15f; } };
struct PkKnobSmall : PkKnobBase { PkKnobSmall() { setSvg(pkSvg("PkKnobSmall")); shadow->opacity = 0.15f; } };
struct PkTrim : PkKnobBase { PkTrim() { setSvg(pkSvg("PkTrim")); shadow->opacity = 0.1f; } };

struct PkJack : app::SvgPort { PkJack() { setSvg(pkSvg("PkJack")); shadow->opacity = 0.1f; } };

struct PkToggle2 : app::SvgSwitch {
	PkToggle2() { addFrame(pkSvg("Toggle2_0")); addFrame(pkSvg("Toggle2_1")); shadow->opacity = 0.f; }
};

struct PkButton : app::SvgSwitch {
	PkButton() { momentary = true; addFrame(pkSvg("PkButton_0")); addFrame(pkSvg("PkButton_1")); shadow->opacity = 0.f; }
};

template <typename TBase>
struct PkButtonLight : TBase {
	PkButtonLight() { this->box.size = mm2px(math::Vec(2.4f, 2.4f)); }
};

struct PkVioletLight : GrayModuleLightWidget {
	PkVioletLight() { addBaseColor(nvgRGB(0xa9, 0x84, 0xf0)); }
};

// Panel and screws (Rack's black screws, spec §3).
template <class W>
inline void pkScaffold(W* w, Module* m, const std::string& slug) {
	w->setModule(m);
	w->setPanel(createPanel(asset::plugin(pluginInstance, "res/" + slug + ".svg")));
	float width = w->box.size.x;
	w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewBlack>(Vec(width - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	if (width > 4 * RACK_GRID_WIDTH) {
		w->addChild(createWidget<ScrewBlack>(Vec(width - 2 * RACK_GRID_WIDTH, 0)));
		w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
}

