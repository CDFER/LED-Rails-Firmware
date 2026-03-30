#include <Arduino.h>
#include <FastLED.h>
#include <LTR303.h>
#include <Preferences.h>

// Preferences is in main.cpp
extern Preferences preferences;

LTR303 lightSensor;

// Configurable bucket system
struct BrightnessBucket {
	float luxMax;		  // Maximum lux for this bucket
	float brightnessMax;  // Brightness at maximum lux (0-1)
};

class BrightnessManager {
  public:
	BrightnessManager() {
		buckets[0] = { 1000.0f, 0.0f };	 // Dark (0-1000 lux)
		if (CITY_CODE == "mel") {
			buckets[1] = { 5000.0f, 0.2f };	 // Indoor (1000-2000 lux)
		} else {
			buckets[1] = { 5000.0f, 0.1f };	 // Indoor (1000-5000 lux)
		}
		buckets[2] = { 100000.0f, 1.0f };  // Outdoor (5000-100000 lux)
	}

	void begin() {
		Wire.begin(SDA_PIN, SCL_PIN, 50000);
		lightSensor.begin(GAIN_48X, EXPOSURE_100ms, true, Wire);
		lightSensor.startPeriodicMeasurement();
		loadBuckets(preferences);
		FastLED.setBrightness(gammaCorrectedBrightness(
			0.0f));	 // Start with LEDs at minimum brightness until we get a reading from the light sensor
	}

	void increase() {
		adjustBuckets(BRIGHTNESS_STEP / 255.0f);
	}

	void decrease() {
		adjustBuckets(-BRIGHTNESS_STEP / 255.0f);
	}

	void toggle() {
		powerOn = !powerOn;
		setBrightness();
	}

	void setPower(bool on) {
		powerOn = on;
		setBrightness();
	}

	void saveBuckets(Preferences &preferences) {
		preferences.begin("brightness", false);
		for (int i = 0; i < numBuckets; i++) {
			preferences.putFloat(("lux" + String(i)).c_str(), buckets[i].luxMax);
			preferences.putFloat(("bright" + String(i)).c_str(), buckets[i].brightnessMax);
		}
		preferences.end();
	}

	void loadBuckets(Preferences &preferences) {
		preferences.begin("brightness", true);
		for (int i = 0; i < numBuckets; i++) {
			buckets[i].luxMax = preferences.getFloat(("lux" + String(i)).c_str(), buckets[i].luxMax);
			buckets[i].brightnessMax = preferences.getFloat(("bright" + String(i)).c_str(), buckets[i].brightnessMax);
		}
		preferences.end();
		printBuckets();
	}

	// Get brightness from light sensor and adjust brightness
	void update() {
		double lux;
		if (lightSensor.getApproximateLux(lux)) {
			ambientLux = float(lux) * luxSmoothingFactor + ambientLux * (1.0f - luxSmoothingFactor);
			bucketIndex = getAmbientBucketIndex(ambientLux);
			brightness = calculateBrightnessForAmbient(ambientLux, bucketIndex);
			setBrightness();
			// Serial.printf("Ambient Lux: %.0f, Bucket: %d, Brightness: %.2f\n", ambientLux, bucketIndex, brightness);
		}
	}

	uint8_t gammaCorrectedBrightness(float _brightness) {
		float scaledBrightness = mapFloat(_brightness, 0.0f, 1.0f, MIN_BRIGHTNESS / 255.0f, MAX_BRIGHTNESS / 255.0f);
		float gamma = 2.2f;
		return static_cast<uint8_t>(pow(scaledBrightness, gamma) * 255.0f);
	}

	void setBrightness() {
		if (powerOn) {
			uint8_t newBrightness = gammaCorrectedBrightness(brightness);
			uint8_t prevBrightness =
				constrain(FastLED.getBrightness(), gammaCorrectedBrightness(0.0), gammaCorrectedBrightness(1.0));

			// Apply Hysteresis to brightness
			if (abs(newBrightness - prevBrightness) > BRIGHTNESS_HYSTERESIS || brightness == 0.0f || brightness == 1.0f
				|| FastLED.getBrightness() == 0) {
				// newBrightness =
				// 	constrain(newBrightness,
				// 			  prevBrightness - 1,
				// 			  prevBrightness + 1);	// Limit brightness changes to 1 step at a time for smooth transitions
				FastLED.setBrightness(newBrightness);
			}
		} else {
			FastLED.setBrightness(0);
		}
	}

	bool isOn() {
		return powerOn;
	}


  private:
	float brightness = 0.0f;  // Current brightness level (0-1) -> MIN_BRIGHTNESS to MAX_BRIGHTNESS
	float ambientLux = 0.0f;  // Current ambient light in lux
	int bucketIndex = 0;	  // Current ambient bucket index
	bool powerOn = true;	  // Power state
	BrightnessBucket buckets[3];
	const int numBuckets = sizeof(buckets) / sizeof(buckets[0]);
	const float luxSmoothingFactor = 0.05f;	 // Smoothing factor for lux readings (0-1)

	void printBuckets() {
		for (int i = 0; i < numBuckets; i++) {
			Serial.printf("{%d: {lux: %.0f-%.0f, bright: %.2f-%.2f}},",
						  i,
						  getLuxForBucket(i - 1),
						  getLuxForBucket(i),
						  getBrightnessForBucket(i - 1),
						  getBrightnessForBucket(i));
		}
		Serial.println();
		Serial.printf("Current ambient: %.0flux, Bucket: %d, Brightness: %.0f%%\n", ambientLux, bucketIndex, brightness * 100.0f);
	}

	// Adjusts the brightness max for the current lux bucket and the one below it (interpolated)
	void adjustBuckets(float delta) {
		float luxMin = getLuxForBucket(bucketIndex - 1);
		float luxMax = getLuxForBucket(bucketIndex);

		// Normalize ambientLux within the bucket range [0, 1]
		float ratio = (ambientLux - luxMin) / (luxMax - luxMin);

		// Closer to upper bucket -> apply more delta to it
		float upperDelta = delta * ratio;
		buckets[bucketIndex].brightnessMax += upperDelta;
		buckets[bucketIndex].brightnessMax = constrain(buckets[bucketIndex].brightnessMax, 0.0f, 1.0f);

		// Apply more adjustment to the bucket that ambientLux is closer to
		if (bucketIndex > 0) {
			// Closer to lower bucket -> apply more delta to it
			float lowerDelta = delta * (1.0f - ratio);
			buckets[bucketIndex - 1].brightnessMax += lowerDelta;
			buckets[bucketIndex - 1].brightnessMax = constrain(buckets[bucketIndex - 1].brightnessMax, 0.0f, 1.0f);
		}

		brightness = calculateBrightnessForAmbient(ambientLux, bucketIndex);
		setBrightness();

		saveBuckets(preferences);

		printBuckets();
	}

	// Map a value from one range to another (standard Arduino map() works only for integers)
	float mapFloat(float value, float inMin, float inMax, float outMin, float outMax) {
		// Avoid division by zero
		if (inMax == inMin) {
			return outMin;
		}

		// Normalize value to 0-1 range
		float normalized = (value - inMin) / (inMax - inMin);

		// Scale to output range
		return outMin + normalized * (outMax - outMin);
	}

	// Get maximum lux for a given bucket index
	float getLuxForBucket(int index) {
		if (index < 0) {
			return 0.0f;
		}
		if (index >= numBuckets) {
			return 1000000.0f;	// Return high lux if above all thresholds
		}
		return buckets[index].luxMax;
	}

	// Calculate brightness based on current ambient lux (interpolate within buckets)
	float getBrightnessForBucket(int index) {
		if (index < 0) {
			return 0.0f;
		}
		if (index >= numBuckets) {
			return 1.0f;
		}
		return buckets[index].brightnessMax;
	}

	// Get current ambient bucket index based on lux
	int getAmbientBucketIndex(float lux) {
		for (uint8_t i = numBuckets - 1; i >= 0; i--) {
			if (lux > getLuxForBucket(i - 1)) {
				return i;
			}
		}
		return 0;
	}

	// Calculate brightness based on current ambient lux (interpolate between buckets)
	float calculateBrightnessForAmbient(float lux, int index) {
		float luxMin = getLuxForBucket(index - 1);
		float luxMax = getLuxForBucket(index);
		float brightnessMin = getBrightnessForBucket(index - 1);
		float brightnessMax = getBrightnessForBucket(index);

		lux = constrain(lux, luxMin, luxMax);

		float newBrightness = mapFloat(lux, luxMin, luxMax, brightnessMin, brightnessMax);

		newBrightness = constrain(newBrightness, 0.0f, 1.0f);

		// Linear interpolation
		return newBrightness;
	}
};