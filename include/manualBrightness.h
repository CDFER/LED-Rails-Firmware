#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>

// Preferences is in main.cpp
extern Preferences preferences;

class BrightnessManager {
  public:
	BrightnessManager() {}

	void begin() {
		load(preferences);
		setBrightness();
	}

	// Increase adjustment for current ambient bucket
	void increase() {
		brightness += BRIGHTNESS_STEP;
		brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		setBrightness();
	}

	void decrease() {
		brightness -= BRIGHTNESS_STEP;
		brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		setBrightness();
	}

	void toggle() {
		powerOn = !powerOn;
		setBrightness();
	}

	void save(Preferences &preferences) {
		preferences.begin("brightness");
		preferences.putInt("brightness", int(brightness));
		preferences.end();
	}

	void load(Preferences &preferences) {
		preferences.begin("brightness");
		brightness = float(preferences.getInt("brightness", brightness));
		preferences.end();
	}

	void update() {
		yield();  // Allow background tasks to run
	}

	void setBrightness() {
		// Apply gamma correction for perceived brightness
		float gamma = 2.2f;
		uint8_t gammaBrightness = static_cast<uint8_t>(pow((brightness / 255.0f), gamma) * 255.0f);

		// Update the LEDs
		FastLED.setBrightness(powerOn ? gammaBrightness : 0);

		Serial.printf("Brightness set to %0.0f/255\n", brightness);

		save(preferences);
	}


  private:
	float brightness = MIN_BRIGHTNESS + BRIGHTNESS_STEP;  // Current brightness level (0-255)
	bool powerOn = true;								  // Power state
};