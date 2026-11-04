#include "plugin.hpp"


struct Dreamcache : Module {
	static const int NUM_STEPS = 8;

	enum ParamId {
		CLOCK_PARAM,
		ENUMS(STEP_PARAM, NUM_STEPS),
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		TRIG_OUTPUT,
		CLOCK_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		CLOCK_LIGHT,
		ENUMS(STEP_LIGHT, NUM_STEPS),
		LIGHTS_LEN
	};

	int step = 0;
	float phase = 0.f;
	dsp::PulseGenerator trigPulse;
	dsp::PulseGenerator clockPulse;

	Dreamcache() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CLOCK_PARAM, -2.f, 4.f, 1.f, "Clock rate", " Hz", 2.f, 1.f);
		configOutput(TRIG_OUTPUT, "Trigger");
		configOutput(CLOCK_OUTPUT, "Clock");

		for (int i = 0; i < NUM_STEPS; i++) {
			configSwitch(STEP_PARAM + i, 0.f, 1.f, 0.f, string::f("Step %d", i + 1), {"Off", "On"});
		}
	}

	bool isStepOn(int i) {
		return params[STEP_PARAM + i].getValue() > 0.5f;
	}

	void advanceStep() {
		step = (step + 1) % NUM_STEPS;
		clockPulse.trigger(1e-3f);
		if (isStepOn(step)) {
			trigPulse.trigger(1e-3f);
		}
	}

	void process(const ProcessArgs& args) override {
		float pitch = params[CLOCK_PARAM].getValue();
		float freq = std::pow(2.f, pitch);
		phase += freq * args.sampleTime;
		if (phase >= 1.f) {
			phase -= 1.f;
			advanceStep();
		}

		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(args.sampleTime) ? 10.f : 0.f);

		lights[CLOCK_LIGHT].setBrightness(phase < 0.5f ? 1.f : 0.f);

		for (int i = 0; i < NUM_STEPS; i++) {
			float brightness = 0.f;
			if (i == step) {
				brightness = isStepOn(i) ? 1.f : 0.35f;
			}
			else if (isStepOn(i)) {
				brightness = 0.15f;
			}
			lights[STEP_LIGHT + i].setBrightness(brightness);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "step", json_integer(step));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* stepJ = json_object_get(rootJ, "step");
		if (stepJ)
			step = json_integer_value(stepJ) % NUM_STEPS;
	}
};


struct PinkLight : GrayModuleLightWidget {
	PinkLight() {
		addBaseColor(nvgRGB(0xff, 0x3d, 0xa8));
	}
};

struct CyanLight : GrayModuleLightWidget {
	CyanLight() {
		addBaseColor(nvgRGB(0x00, 0xe8, 0xf0));
	}
};

/** Static panel label (Rack's SVG renderer ignores <text>). */
struct PanelLabel : widget::TransparentWidget {
	std::string text;
	float fontSize = 11.f;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgFillColor(args.vg, settings::preferDarkPanels ? nvgRGB(0xe0, 0xe0, 0xe0) : nvgRGB(0x3a, 0x2a, 0x4a));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};

static PanelLabel* createPanelLabel(Vec center, std::string text, float fontSize = 11.f) {
	PanelLabel* label = createWidget<PanelLabel>(Vec());
	label->box.size = Vec(44.f, 14.f);
	label->box.pos = center.minus(label->box.size.div(2));
	label->text = std::move(text);
	label->fontSize = fontSize;
	return label;
}

struct ClockRateDisplay : LedDisplay {
	Dreamcache* module;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;

		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/DSEG7ClassicMini-BoldItalic.ttf"));
		if (!font)
			return;

		float freq = 2.f;
		if (module)
			freq = std::pow(2.f, module->params[Dreamcache::CLOCK_PARAM].getValue());

		std::string text;
		if (freq < 10.f)
			text = string::f("%4.2f", freq);
		else
			text = string::f("%4.1f", freq);

		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 12.f);
		nvgTextLetterSpacing(args.vg, 1.5);
		nvgFillColor(args.vg, nvgRGB(0xff, 0x3d, 0xa8));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 1.f, text.c_str(), NULL);
	}
};

struct SquareStepLight : RectangleLight<PinkLight> {
	SquareStepLight() {
		box.size = Vec(26.f, 26.f);
	}
};

struct SquareStepButton : app::SvgSwitch {
	ModuleLightWidget* light;

	SquareStepButton() {
		momentary = false;
		latch = true;
		shadow->opacity = 0.f;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/SquareButton.svg")));
		light = new SquareStepLight;
		light->box.pos = box.size.div(2).minus(light->box.size.div(2));
		addChild(light);
	}

	ModuleLightWidget* getLight() {
		return light;
	}
};


struct DreamcacheWidget : ModuleWidget {
	DreamcacheWidget(Dreamcache* module) {
		setModule(module);
		setPanel(createPanel(
			asset::plugin(pluginInstance, "res/Dreamcache.svg"),
			asset::plugin(pluginInstance, "res/Dreamcache-dark.svg")));

		const float cx = 65.f;

		addChild(createLightCentered<MediumLight<CyanLight>>(Vec(cx, 18.f), module, Dreamcache::CLOCK_LIGHT));
		addParam(createParamCentered<RoundHugeBlackKnob>(Vec(cx, 62.f), module, Dreamcache::CLOCK_PARAM));

		// Display and step pads sit just above the output labels
		ClockRateDisplay* display = createWidget<ClockRateDisplay>(Vec(cx - 35.f, 101.f));
		display->box.size = Vec(70.f, 26.f);
		display->module = module;
		addChild(display);

		// 2 × 4 step pads (40px)
		const float colX[2] = {43.f, 87.f};
		const float rowPitch = 42.f;
		const float originY = 166.f;

		for (int i = 0; i < Dreamcache::NUM_STEPS; i++) {
			int col = i / 4;
			int row = i % 4;
			addParam(createLightParamCentered<SquareStepButton>(
				Vec(colX[col], originY + row * rowPitch),
				module, Dreamcache::STEP_PARAM + i, Dreamcache::STEP_LIGHT + i));
		}

		addOutput(createOutputCentered<PJ301MPort>(Vec(40.f, 352.f), module, Dreamcache::CLOCK_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90.f, 352.f), module, Dreamcache::TRIG_OUTPUT));
		addChild(createPanelLabel(Vec(40.f, 330.f), "clock", 13.f));
		addChild(createPanelLabel(Vec(90.f, 330.f), "trig", 13.f));
	}
};


Model* modelDreamcache = createModel<Dreamcache, DreamcacheWidget>("Dreamcache");
