#include "brightness.h"

Preferences preferences;

BrightnessManager brightness;

#if defined(LIGHT_SENSOR)
	#include <Wire.h>

LTR303 lightSensor;

BrightnessManager::BrightnessManager() : brightness(0.1f), ambientLux(0.0f), bucketIndex(0), powerOn(true) {
	buckets[0] = { 1000.0f, 0.0f };	 // Dark (0-1000 lux)
	if (String(CITY_CODE) == "mel") {
		buckets[1] = { 5000.0f, 0.2f };	 // Indoor (1000-2000 lux)
	} else {
		buckets[1] = { 5000.0f, 0.1f };	 // Indoor (1000-5000 lux)
	}
	buckets[2] = { 100000.0f, 1.0f };  // Outdoor (5000-100000 lux)
}

void BrightnessManager::begin() {
	Wire.begin(SDA_PIN, SCL_PIN, 50000);
	lightSensor.begin(GAIN_48X, EXPOSURE_100ms, true, Wire);
	lightSensor.startPeriodicMeasurement();
	load(preferences);
	FastLED.setBrightness(gammaCorrectedBrightness(
		brightness));  // Start with LEDs at default brightness until we get a reading from the light sensor
}

void BrightnessManager::increase() {
	adjustBuckets(BRIGHTNESS_STEP / 255.0f);
	savePending = true;
	buttonPressed = true;
}

void BrightnessManager::decrease() {
	adjustBuckets(-BRIGHTNESS_STEP / 255.0f);
	savePending = true;
	buttonPressed = true;
}

void BrightnessManager::toggle() {
	powerOn = !powerOn;
	setBrightness();
}

void BrightnessManager::setPower(bool on) {
	powerOn = on;
	setBrightness();
}

void BrightnessManager::save(Preferences& prefs) {
	Serial.println("Saving brightness buckets to Preferences");
	prefs.begin("brightness", false);
	for (int i = 0; i < numBuckets; i++) {
		prefs.putFloat(("lux" + String(i)).c_str(), buckets[i].luxMax);
		prefs.putFloat(("bright" + String(i)).c_str(), buckets[i].brightnessMax);
	}
	prefs.end();
}

void BrightnessManager::load(Preferences& prefs) {
	prefs.begin("brightness", true);
	for (int i = 0; i < numBuckets; i++) {
		buckets[i].luxMax = prefs.getFloat(("lux" + String(i)).c_str(), buckets[i].luxMax);
		buckets[i].brightnessMax = prefs.getFloat(("bright" + String(i)).c_str(), buckets[i].brightnessMax);
	}
	prefs.end();
	printBuckets();
}

void BrightnessManager::update() {
	if (xTaskGetTickCount() - lastUpdate >= pdMS_TO_TICKS(100)) {
		double newLux;
		if (lightSensor.getApproximateLux(newLux)) {
			if (newLux > ambientLux) {
				ambientLux = float(newLux) * luxUpSmoothingFactor + ambientLux * (1.0f - luxUpSmoothingFactor);
			} else {
				ambientLux = float(newLux) * luxDownSmoothingFactor + ambientLux * (1.0f - luxDownSmoothingFactor);
			}
			bucketIndex = getAmbientBucketIndex(ambientLux);
			brightness = calculateBrightnessForAmbient(ambientLux, bucketIndex);
			setBrightness();
		}
		lastUpdate = xTaskGetTickCount();

		if (savePending) {
			save(preferences);
			savePending = false;
		}
	}
}

uint8_t BrightnessManager::gammaCorrectedBrightness(float _brightness) {
	float scaledBrightness = mapFloat(_brightness, 0.0f, 1.0f, MIN_BRIGHTNESS / 255.0f, MAX_BRIGHTNESS / 255.0f);
	float gamma = 2.2f;
	return static_cast<uint8_t>(pow(scaledBrightness, gamma) * 255.0f);
}

void BrightnessManager::setBrightness() {
	if (powerOn) {
		if (FastLED.getBrightness() == 0) {
			FastLED.setBrightness(gammaCorrectedBrightness(MIN_BRIGHTNESS / 255.0f));
		} else {
			uint8_t newBrightness = gammaCorrectedBrightness(brightness);
			uint8_t prevBrightness =
				constrain(FastLED.getBrightness(), gammaCorrectedBrightness(0.0), gammaCorrectedBrightness(1.0));

			if (abs(newBrightness - prevBrightness) > BRIGHTNESS_HYSTERESIS || newBrightness == 0.0f || newBrightness == 1.0f) {
				if (buttonPressed) {  // If the brightness change was triggered by a button press, apply it immediately without smoothing
					newBrightness = gammaCorrectedBrightness(brightness);
					buttonPressed = false;
				} else {
					newBrightness = constrain(newBrightness, prevBrightness - 1, prevBrightness + 2);
				}
				FastLED.setBrightness(newBrightness);
			}
		}
	} else {
		FastLED.setBrightness(0);
	}
}

bool BrightnessManager::isOn() {
	return powerOn;
}

void BrightnessManager::printBuckets() {
	for (int i = 0; i < numBuckets; i++) {
		Serial.printf("{%d: {lux: %.0f-%.0f, bright: %.2f-%.2f}},",
					  i,
					  getLuxForBucket(i - 1),
					  getLuxForBucket(i),
					  getBrightnessForBucket(i - 1),
					  getBrightnessForBucket(i));
	}
	Serial.println();
}

void BrightnessManager::adjustBuckets(float delta) {
	float luxMin = getLuxForBucket(bucketIndex - 1);
	float luxMax = getLuxForBucket(bucketIndex);
	float ratio = (ambientLux - luxMin) / (luxMax - luxMin);

	float upperDelta = delta * ratio;
	buckets[bucketIndex].brightnessMax += upperDelta;
	buckets[bucketIndex].brightnessMax = constrain(buckets[bucketIndex].brightnessMax, 0.0f, 1.0f);

	if (bucketIndex > 0) {
		float lowerDelta = delta * (1.0f - ratio);
		buckets[bucketIndex - 1].brightnessMax += lowerDelta;
		buckets[bucketIndex - 1].brightnessMax = constrain(buckets[bucketIndex - 1].brightnessMax, 0.0f, 1.0f);
	}

	brightness = calculateBrightnessForAmbient(ambientLux, bucketIndex);
	setBrightness();
	printBuckets();
}

float BrightnessManager::mapFloat(float value, float inMin, float inMax, float outMin, float outMax) {
	if (inMax == inMin) {
		return outMin;
	}
	float normalized = (value - inMin) / (inMax - inMin);
	return outMin + normalized * (outMax - outMin);
}

float BrightnessManager::getLuxForBucket(int index) {
	if (index < 0)
		return 0.0f;
	if (index >= numBuckets)
		return 1000000.0f;
	return buckets[index].luxMax;
}

float BrightnessManager::getBrightnessForBucket(int index) {
	if (index < 0)
		return 0.0f;
	if (index >= numBuckets)
		return 1.0f;
	return buckets[index].brightnessMax;
}

int BrightnessManager::getAmbientBucketIndex(float lux) {
	for (uint8_t i = numBuckets - 1; i >= 0; i--) {
		if (lux > getLuxForBucket(i - 1))
			return i;
	}
	return 0;
}

float BrightnessManager::calculateBrightnessForAmbient(float lux, int index) {
	float luxMin = getLuxForBucket(index - 1);
	float luxMax = getLuxForBucket(index);
	float brightnessMin = getBrightnessForBucket(index - 1);
	float brightnessMax = getBrightnessForBucket(index);

	lux = constrain(lux, luxMin, luxMax);
	float newBrightness = mapFloat(lux, luxMin, luxMax, brightnessMin, brightnessMax);
	return constrain(newBrightness, 0.0f, 1.0f);
}

#else

BrightnessManager::BrightnessManager() : brightness(MIN_BRIGHTNESS + BRIGHTNESS_STEP), powerOn(true) {}

void BrightnessManager::begin() {
	load(preferences);
	setBrightness();
}

void BrightnessManager::increase() {
	brightness += BRIGHTNESS_STEP;
	brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
	setBrightness();
	savePending = true;
}

void BrightnessManager::decrease() {
	brightness -= BRIGHTNESS_STEP;
	brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
	setBrightness();
	savePending = true;
}

void BrightnessManager::toggle() {
	powerOn = !powerOn;
	setBrightness();
}

void BrightnessManager::setPower(bool on) {
	powerOn = on;
	setBrightness();
}

void BrightnessManager::update() {
	if (xTaskGetTickCount() - lastUpdate >= pdMS_TO_TICKS(100)) {
		if (savePending) {
			save(preferences);
			savePending = false;
		}
	}
}

void BrightnessManager::save(Preferences& prefs) {
	prefs.begin("brightness");
	prefs.putInt("brightness", int(brightness));
	prefs.end();
}

void BrightnessManager::load(Preferences& prefs) {
	prefs.begin("brightness");
	brightness = float(prefs.getInt("brightness", int(brightness)));
	prefs.end();
}

void BrightnessManager::setBrightness() {
	float gamma = 2.2f;
	uint8_t gammaBrightness = static_cast<uint8_t>(pow((brightness / 255.0f), gamma) * 255.0f);
	FastLED.setBrightness(powerOn ? gammaBrightness : 0);
	Serial.printf("Brightness set to %0.0f/255\n", brightness);
}

bool BrightnessManager::isOn() {
	return powerOn;
}

#endif
