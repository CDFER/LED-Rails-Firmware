#include "brightness.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ledStorageSync.h"

Preferences preferences;
SemaphoreHandle_t fastLEDPreferencesMutex = nullptr;
BrightnessManager brightnessManager;

extern TaskHandle_t ledTaskHandle;

// --- COMMON THREAD-SAFE FRONTEND METHODS ---
// These execute extremely fast in any task (like the Button Task) and never touch FastLED or EEPROM.
// They use lock-free task notifications to tell the backend LED loop what to do.

void BrightnessManager::increase() {
	if (ledTaskHandle) {
		xTaskNotify(ledTaskHandle, EVT_INCREASE, eSetBits);
	}
}

void BrightnessManager::decrease() {
	if (ledTaskHandle) {
		xTaskNotify(ledTaskHandle, EVT_DECREASE, eSetBits);
	}
}

void BrightnessManager::toggle() {
	powerEnabled = !powerEnabled;
	if (ledTaskHandle) {
		xTaskNotify(ledTaskHandle, EVT_UPDATE_BRIGHTNESS, eSetBits);
	}
}

void BrightnessManager::setPower(bool powerOnRequested) {
	if (powerEnabled != powerOnRequested) {
		powerEnabled = powerOnRequested;
		if (ledTaskHandle) {
			xTaskNotify(ledTaskHandle, EVT_UPDATE_BRIGHTNESS, eSetBits);
		}
	}
}

bool BrightnessManager::isOn() {
	return powerEnabled;
}

// --- SENSOR-SPECIFIC IMPLEMENTATION ---
#if defined(LIGHT_SENSOR)
#include <Wire.h>

LTR303 lightSensor;

BrightnessManager::BrightnessManager() : brightnessValue(0.1f), powerEnabled(true), ambientLux(0.0f), bucketIndex(0) {
	buckets[0] = { 0.0f, 0.0f };     // Min (0 lux)
	buckets[1] = { 1000.0f, 0.005f };  // Dark (0-1000 lux)
	buckets[2] = { 5000.0f, 0.25f };  // Indoor (1000-2000 lux)
	buckets[3] = { 100000.0f, 1.0f };  // Outdoor (5000-100000 lux)
}

void BrightnessManager::begin() {
	Wire.begin(SDA_PIN, SCL_PIN, 50000);
	lightSensor.begin(GAIN_48X, EXPOSURE_100ms, true, Wire);
	lightSensor.startPeriodicMeasurement();
	load();
	FastLED.setBrightness(gammaCorrectedBrightness(brightnessValue));  // Start with LEDs at default brightness
}

void BrightnessManager::save() {
	Preferences localPrefs;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPrefs.begin("brightness", false);
	for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
		localPrefs.putFloat(("lux" + String(bucketNumber)).c_str(), buckets[bucketNumber].luxMax);
		localPrefs.putFloat(("bright" + String(bucketNumber)).c_str(), buckets[bucketNumber].brightnessMax);
	}
	localPrefs.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void BrightnessManager::load() {
	Preferences localPrefs;
	localPrefs.begin("brightness", true);
	if (!localPrefs.isKey("lux3")) {
		// Legacy format: only 3 buckets stored, migrate to 4 buckets
		buckets[0] = { 0.0f, 0.0f };
		for (int bucketNumber = 1; bucketNumber < numBuckets; bucketNumber++) {
			int legacyIndex = bucketNumber - 1;
			buckets[bucketNumber].luxMax =
			    localPrefs.getFloat(("lux" + String(legacyIndex)).c_str(), buckets[bucketNumber].luxMax);
			buckets[bucketNumber].brightnessMax =
			    localPrefs.getFloat(("bright" + String(legacyIndex)).c_str(), buckets[bucketNumber].brightnessMax);
		}
		this->save();  // Save the migrated buckets back to Preferences
		Serial.println("Migrated legacy brightness buckets to new format");
	} else {
		for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
			buckets[bucketNumber].luxMax =
			    localPrefs.getFloat(("lux" + String(bucketNumber)).c_str(), buckets[bucketNumber].luxMax);
			buckets[bucketNumber].brightnessMax =
			    localPrefs.getFloat(("bright" + String(bucketNumber)).c_str(), buckets[bucketNumber].brightnessMax);
		}
	}
	localPrefs.end();
}

void BrightnessManager::update() {
	// 1. Process instant task notifications
	uint32_t pendingEvents = 0;
	if (xTaskNotifyWait(0x00, ULONG_MAX, &pendingEvents, 0) == pdTRUE) {
		if (pendingEvents & EVT_INCREASE) {
			adjustBuckets(BRIGHTNESS_STEP / 255.0f);
			savePending = true;
			buttonPressed = true;
		}
		if (pendingEvents & EVT_DECREASE) {
			adjustBuckets(-BRIGHTNESS_STEP / 255.0f);
			savePending = true;
			buttonPressed = true;
		}
		if (pendingEvents & EVT_UPDATE_BRIGHTNESS) {
			setBrightness();
		}
	}

	// 2. Perform background Lux checks at interval
	if (xTaskGetTickCount() - lastUpdateTick >= pdMS_TO_TICKS(100)) {
		double measuredLux;
		if (lightSensor.getApproximateLux(measuredLux)) {
			if (measuredLux > ambientLux) {
				ambientLux = float(measuredLux) * luxUpSmoothingFactor + ambientLux * (1.0f - luxUpSmoothingFactor);
			} else {
				ambientLux = float(measuredLux) * luxDownSmoothingFactor + ambientLux * (1.0f - luxDownSmoothingFactor);
			}
			bucketIndex = getAmbientBucketIndex(ambientLux);
			brightnessValue = calculateBrightnessForAmbient(ambientLux, bucketIndex);
			setBrightness();
		}
		lastUpdateTick = xTaskGetTickCount();

		if (savePending) {
			save();
			savePending = false;
		}
	}
}

uint8_t BrightnessManager::gammaCorrectedBrightness(float inputBrightness) {
	const float gammaExponent = 2.2f;
	const float normalizedBrightness = constrain(inputBrightness, 0.0f, 1.0f);
	const float gammaCorrectedValue = pow(normalizedBrightness, gammaExponent);
	const float outputBrightness =
	    MIN_BRIGHTNESS + gammaCorrectedValue * (MAX_BRIGHTNESS - MIN_BRIGHTNESS);
	return static_cast<uint8_t>(constrain(outputBrightness, float(MIN_BRIGHTNESS), float(MAX_BRIGHTNESS)));
}

void BrightnessManager::setBrightness() {
	if (powerEnabled) {
		if (FastLED.getBrightness() == 0) {
			FastLED.setBrightness(MIN_BRIGHTNESS);  // Start from minimum brightness to avoid fade-in issues
		} else {
			uint8_t targetBrightness = gammaCorrectedBrightness(brightnessValue);
			uint8_t previousBrightness = FastLED.getBrightness();

			if (buttonPressed) {  // If the brightness change was triggered by a button press, apply it immediately
				buttonPressed = false;
				FastLED.setBrightness(targetBrightness);
				Serial.printf("Button Pressed: Setting Brightness to %.0f%%, brightnessValue = %.2f\n",
				              targetBrightness / 255.0f * 100.0f,
				              brightnessValue);
				Serial.printf("Current Brightness: %i\n", FastLED.getBrightness());
			} else if (abs(targetBrightness - previousBrightness) > BRIGHTNESS_HYSTERESIS || targetBrightness == 0
			           || targetBrightness == 255) {
				targetBrightness = constrain(targetBrightness, previousBrightness - 1, previousBrightness + 2);
				FastLED.setBrightness(targetBrightness);
				Serial.printf("Current Brightness: %i\n", FastLED.getBrightness());
			}
		}
	}
}

void BrightnessManager::printBuckets() {
	Serial.println("Brightness Curve:");
	for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
		Serial.printf(
		    "%.0f lux =\t %.0f%%,\n", getLuxForBucket(bucketNumber), getBrightnessForBucket(bucketNumber) * 100.0f);
	}
}

void BrightnessManager::adjustBuckets(float delta) {
	float luxMin = getLuxForBucket(bucketIndex - 1);
	float luxMax = getLuxForBucket(bucketIndex);

	float interpolationRatio = 0.0f;
	if (luxMax > luxMin) {
		interpolationRatio = constrain((ambientLux - luxMin) / (luxMax - luxMin), 0.0f, 1.0f);
	}

	float upperDelta = delta * interpolationRatio;
	buckets[bucketIndex].brightnessMax += upperDelta;
	buckets[bucketIndex].brightnessMax = constrain(buckets[bucketIndex].brightnessMax, 0.0f, 1.0f);

	if (bucketIndex > 0) {
		float lowerDelta = delta * (1.0f - interpolationRatio);
		buckets[bucketIndex - 1].brightnessMax += lowerDelta;
		buckets[bucketIndex - 1].brightnessMax = constrain(buckets[bucketIndex - 1].brightnessMax, 0.0f, 1.0f);
	}

	// Ensure brightness limits never decrease as ambient light increases.
	// If one bucket increases, propagate that minimum through later buckets.
	for (int bucketNumber = 1; bucketNumber < numBuckets; bucketNumber++) {
		if (buckets[bucketNumber].brightnessMax < buckets[bucketNumber - 1].brightnessMax) {
			buckets[bucketNumber].brightnessMax = buckets[bucketNumber - 1].brightnessMax;
		}
	}

	brightnessValue = calculateBrightnessForAmbient(ambientLux, bucketIndex);
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

float BrightnessManager::getLuxForBucket(int bucketIndex) {
	if (bucketIndex < 0)
		return 0.0f;
	if (bucketIndex >= numBuckets)
		return 1000000.0f;
	return buckets[bucketIndex].luxMax;
}

float BrightnessManager::getBrightnessForBucket(int bucketIndex) {
	if (bucketIndex < 0)
		return 0.0f;
	if (bucketIndex >= numBuckets)
		return 1.0f;
	return buckets[bucketIndex].brightnessMax;
}

int BrightnessManager::getAmbientBucketIndex(float lux) {
	for (int candidateBucketIndex = numBuckets - 1; candidateBucketIndex >= 0; candidateBucketIndex--) {
		if (lux > getLuxForBucket(candidateBucketIndex - 1))
			return candidateBucketIndex;
	}
	return 0;
}

float BrightnessManager::calculateBrightnessForAmbient(float lux, int bucketIndex) {
	float luxMin = getLuxForBucket(bucketIndex - 1);
	float luxMax = getLuxForBucket(bucketIndex);
	float brightnessMin = getBrightnessForBucket(bucketIndex - 1);
	float brightnessMax = getBrightnessForBucket(bucketIndex);

	lux = constrain(lux, luxMin, luxMax);
	float newBrightness = mapFloat(lux, luxMin, luxMax, brightnessMin, brightnessMax);
	return constrain(newBrightness, 0.0f, 1.0f);
}

// --- NO LIGHT SENSOR IMPLEMENTATION ---
#else

BrightnessManager::BrightnessManager() : brightnessValue(MIN_BRIGHTNESS + BRIGHTNESS_STEP), powerEnabled(true) {}

void BrightnessManager::begin() {
	load();
	setBrightness();
}

void BrightnessManager::update() {
	// 1. Process instant task notifications
	uint32_t pendingEvents = 0;
	if (xTaskNotifyWait(0x00, ULONG_MAX, &pendingEvents, 0) == pdTRUE) {
		if (pendingEvents & EVT_INCREASE) {
			brightnessValue += BRIGHTNESS_STEP;
			brightnessValue = constrain(brightnessValue, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
			setBrightness();
			savePending = true;
		}
		if (pendingEvents & EVT_DECREASE) {
			brightnessValue -= BRIGHTNESS_STEP;
			brightnessValue = constrain(brightnessValue, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
			setBrightness();
			savePending = true;
		}
		if (pendingEvents & EVT_UPDATE_BRIGHTNESS) {
			setBrightness();
		}
	}

	// 2. Perform background rate-limited EEPROM saves
	if (xTaskGetTickCount() - lastUpdateTick >= pdMS_TO_TICKS(100)) {
		if (savePending) {
			save();
			savePending = false;
		}
		lastUpdateTick = xTaskGetTickCount();
	}
}

void BrightnessManager::save() {
	Preferences localPrefs;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPrefs.begin("brightness");
	localPrefs.putInt("brightness", int(brightnessValue));
	localPrefs.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void BrightnessManager::load() {
	Preferences localPrefs;
	localPrefs.begin("brightness");
	brightnessValue = float(localPrefs.getInt("brightness", int(brightnessValue)));
	localPrefs.end();
}

void BrightnessManager::setBrightness() {
	if (powerEnabled) {
		const float gammaExponent = 2.2f;
		const float normalizedBrightness = constrain(brightnessValue / 255.0f, 0.0f, 1.0f);
		const float gammaCorrectedValue = pow(normalizedBrightness, gammaExponent);
		const float outputBrightness =
		    MIN_BRIGHTNESS + gammaCorrectedValue * (MAX_BRIGHTNESS - MIN_BRIGHTNESS);
		const uint8_t gammaCorrectedBrightnessValue =
		    static_cast<uint8_t>(constrain(outputBrightness, float(MIN_BRIGHTNESS), float(MAX_BRIGHTNESS)));
		FastLED.setBrightness(gammaCorrectedBrightnessValue);
		Serial.printf("Brightness set to %0.0f/255\n", brightnessValue);
	}
}

#endif
