#include "brightness.h"
#include <cmath>
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
	bool currentPowerState = powerEnabled.load();
	while (!powerEnabled.compare_exchange_weak(currentPowerState, !currentPowerState)) {
	}
	if (ledTaskHandle) {
		xTaskNotify(ledTaskHandle, EVT_UPDATE_BRIGHTNESS, eSetBits);
	}
}

void BrightnessManager::setPower(bool powerOnRequested) {
	bool expectedPowerState = !powerOnRequested;
	if (powerEnabled.compare_exchange_strong(expectedPowerState, powerOnRequested)) {
		if (ledTaskHandle) {
			xTaskNotify(ledTaskHandle, EVT_UPDATE_BRIGHTNESS, eSetBits);
		}
	}
}

bool BrightnessManager::isOn() {
	return powerEnabled.load();
}

uint8_t BrightnessManager::getBrightness() const {
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
#if defined(LIGHT_SENSOR)
	const uint8_t outputBrightness = brightnessValue * 100.0f;  // Convert to 0-100 scale for output
#else
	const uint8_t outputBrightness = static_cast<uint8_t>(
	    map(brightnessValue, MIN_BRIGHTNESS, MAX_BRIGHTNESS, 0.0f, 100.0f));  // Convert to 0-100 scale for output
#endif
	xSemaphoreGive(fastLEDPreferencesMutex);
	return outputBrightness;
}

// --- SENSOR-SPECIFIC IMPLEMENTATION ---
#if defined(LIGHT_SENSOR)
#include <Wire.h>

LTR303 lightSensor;

namespace {
constexpr TickType_t LightSensorNoDataTimeout = pdMS_TO_TICKS(3000);
std::atomic<TickType_t> lightSensorLastValidReadTick{ 0 };
std::atomic<bool> lightSensorInitialized{ false };

bool initializeLightSensor() {
	lightSensor.endPeriodicMeasurement();  // Ensure sensor is in idle state before re-initializing
	const uint8_t error = lightSensor.begin(GAIN_48X, EXPOSURE_400ms, true, Wire);
	if (error != 0) {
		lightSensorInitialized = false;
		Serial.printf("LTR303 initialization failed: %s (%u)\n", lightSensor.getErrorText(error), error);
		return false;
	}

	const uint8_t startError = lightSensor.startPeriodicMeasurement();
	if (startError != 0) {
		lightSensorInitialized = false;
		Serial.printf(
		    "LTR303 startPeriodicMeasurement failed: %s (%u)\n", lightSensor.getErrorText(startError), startError);
		return false;
	}

	lightSensorInitialized = true;
	lightSensorLastValidReadTick = xTaskGetTickCount();
	// Serial.println("LTR303 initialized");
	return true;
}
}

bool BrightnessManager::isLightSensorConnected() {
	std::lock_guard<std::mutex> lock(lightSensorMutex);
	lightSensorInitialized = lightSensor.isConnected(Wire, &Serial);
	lightSensorLastValidReadTick = xTaskGetTickCount();
	return lightSensorInitialized;
}

BrightnessManager::BrightnessManager() : brightnessValue(0.1f), powerEnabled(true), ambientLux(0.0f), bucketIndex(0) {
	buckets[0] = { 0.0f, 0.0f };       // Min (0 lux)
	buckets[1] = { 1000.0f, 0.005f };  // Dark (0-1000 lux)
	buckets[2] = { 5000.0f, 0.25f };   // Indoor (1000-5000 lux)
	buckets[3] = { 100000.0f, 1.0f };  // Outdoor (5000-100000 lux)
}

float BrightnessManager::getAmbientLux() const {
	return ambientLux;
}

float BrightnessManager::getAmbientBrightness() const {
	return brightnessValue * 100.0f;
}

bool BrightnessManager::getBrightnessCurve(BrightnessCurve& curve) const {
	std::lock_guard<std::mutex> lock(curveMutex);
	for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
		curve.buckets[bucketNumber] = buckets[bucketNumber];
	}
	return true;
}

bool BrightnessManager::isValidBrightnessCurve(const BrightnessCurve& curve) const {
	float previousLux = -1.0f;
	float previousBrightness = 0.0f;
	for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
		const BrightnessBucket& bucket = curve.buckets[bucketNumber];
		if (!std::isfinite(bucket.luxMax) || !std::isfinite(bucket.brightnessMax) || bucket.luxMax < 0.0f
		    || bucket.brightnessMax < 0.0f || bucket.brightnessMax > 1.0f) {
			return false;
		}

		if (bucketNumber == 0) {
			if (bucket.luxMax != 0.0f) {
				return false;
			}
		} else if (
		    bucket.luxMax <= previousLux || bucket.luxMax >= 1000000.0f || bucket.brightnessMax < previousBrightness) {
			return false;
		}

		previousLux = bucket.luxMax;
		previousBrightness = bucket.brightnessMax;
	}
	return true;
}

bool BrightnessManager::requestBrightnessCurve(const BrightnessCurve& curve) {
	if (!isValidBrightnessCurve(curve)) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(curveMutex);
		pendingCurve = curve;
		curveUpdatePending = true;
	}

	if (ledTaskHandle) {
		xTaskNotify(ledTaskHandle, EVT_UPDATE_CURVE, eSetBits);
	}
	return true;
}

void BrightnessManager::begin() {
	Wire.begin(SDA_PIN, SCL_PIN, 50000);
	Wire.setTimeOut(50);
	load();
	FastLED.setBrightness(gammaCorrectedBrightness(brightnessValue));  // Start with LEDs at default brightness
	initializeLightSensor();
}

void BrightnessManager::save() {
	BrightnessCurve curve;
	getBrightnessCurve(curve);

	Preferences localPrefs;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPrefs.begin("brightness", false);
	for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
		localPrefs.putFloat(("lux" + String(bucketNumber)).c_str(), curve.buckets[bucketNumber].luxMax);
		localPrefs.putFloat(("bright" + String(bucketNumber)).c_str(), curve.buckets[bucketNumber].brightnessMax);
	}
	localPrefs.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void BrightnessManager::load() {
	BrightnessCurve loadedCurve;
	getBrightnessCurve(loadedCurve);

	Preferences localPrefs;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPrefs.begin("brightness", true);
	bool migrated = false;
	if (!localPrefs.isKey("lux3")) {
		// Legacy format: only 3 buckets stored, migrate to 4 buckets
		loadedCurve.buckets[0] = { 0.0f, 0.0f };
		for (int bucketNumber = 1; bucketNumber < numBuckets; bucketNumber++) {
			int legacyIndex = bucketNumber - 1;
			loadedCurve.buckets[bucketNumber].luxMax =
			    localPrefs.getFloat(("lux" + String(legacyIndex)).c_str(), loadedCurve.buckets[bucketNumber].luxMax);
			loadedCurve.buckets[bucketNumber].brightnessMax = localPrefs.getFloat(
			    ("bright" + String(legacyIndex)).c_str(), loadedCurve.buckets[bucketNumber].brightnessMax);
		}
		migrated = true;
	} else {
		for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
			loadedCurve.buckets[bucketNumber].luxMax =
			    localPrefs.getFloat(("lux" + String(bucketNumber)).c_str(), loadedCurve.buckets[bucketNumber].luxMax);
			loadedCurve.buckets[bucketNumber].brightnessMax = localPrefs.getFloat(
			    ("bright" + String(bucketNumber)).c_str(), loadedCurve.buckets[bucketNumber].brightnessMax);
		}
	}
	localPrefs.end();
	xSemaphoreGive(fastLEDPreferencesMutex);

	if (!isValidBrightnessCurve(loadedCurve)) {
		Serial.println("Invalid brightness curve in Preferences; using defaults");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(curveMutex);
		for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
			buckets[bucketNumber] = loadedCurve.buckets[bucketNumber];
		}
	}

	if (migrated) {
		this->save();  // Save the migrated buckets back to Preferences
		Serial.println("Migrated legacy brightness buckets to the new 4 bucket format");
	}
}

void BrightnessManager::applyPendingCurve() {
	BrightnessCurve requestedCurve;
	{
		std::lock_guard<std::mutex> lock(curveMutex);
		if (!curveUpdatePending) {
			return;
		}
		requestedCurve = pendingCurve;
		curveUpdatePending = false;
		for (int bucketNumber = 0; bucketNumber < numBuckets; bucketNumber++) {
			buckets[bucketNumber] = requestedCurve.buckets[bucketNumber];
		}
	}

	bucketIndex = getAmbientBucketIndex(ambientLux);
	brightnessValue = calculateBrightnessForAmbient(ambientLux, bucketIndex);
	setBrightness();
	savePending = true;
	printBuckets();
}

void BrightnessManager::update() {
	// 1. Process instant task notifications
	uint32_t pendingEvents = 0;
	if (xTaskNotifyWait(0x00, ULONG_MAX, &pendingEvents, 0) == pdTRUE) {
		if (pendingEvents & EVT_UPDATE_CURVE) {
			applyPendingCurve();
		}
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
	if (xTaskGetTickCount() - lastUpdateTick >= pdMS_TO_TICKS(50)) {
		if (!lightSensorInitialized) {
			initializeLightSensor();
		}

		const TickType_t currentTick = xTaskGetTickCount();
		double measuredLux;

		if (lightSensor.newDataAvailable()) {
			lightSensorLastValidReadTick = currentTick;
			if (lightSensor.getApproximateLux(measuredLux)) {
				// Serial.printf("Measured Lux: %.2f\n", measuredLux);
				if (measuredLux > ambientLux) {
					ambientLux = float(measuredLux) * luxUpSmoothingFactor + ambientLux * (1.0f - luxUpSmoothingFactor);
				} else {
					ambientLux =
					    float(measuredLux) * luxDownSmoothingFactor + ambientLux * (1.0f - luxDownSmoothingFactor);
				}
				bucketIndex = getAmbientBucketIndex(ambientLux);
				brightnessValue = calculateBrightnessForAmbient(ambientLux, bucketIndex);
			}
		} else if (currentTick - lightSensorLastValidReadTick >= LightSensorNoDataTimeout) {
			Serial.println("LTR303 stopped responding; will retry initialization");
			lightSensorInitialized = false;
		}

		setBrightness();

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
	const float outputBrightness = MIN_BRIGHTNESS + gammaCorrectedValue * (MAX_BRIGHTNESS - MIN_BRIGHTNESS);
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
				// Serial.printf("Current Brightness: %i\n", FastLED.getBrightness());
			} else if (abs(targetBrightness - previousBrightness) > BRIGHTNESS_HYSTERESIS || targetBrightness == 0
			           || targetBrightness == 255) {
				targetBrightness = constrain(targetBrightness, previousBrightness - 1, previousBrightness + 1);
				FastLED.setBrightness(targetBrightness);
				// Serial.printf("Current Brightness: %i\n", FastLED.getBrightness());
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
	std::lock_guard<std::mutex> lock(curveMutex);
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
		const float constrainedBrightnessValue = constrain(brightnessValue, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		const float gammaExponent = 2.2f;
		const float normalizedBrightness = constrain(constrainedBrightnessValue / 255.0f, 0.0f, 1.0f);
		const float gammaCorrectedValue = pow(normalizedBrightness, gammaExponent);
		FastLED.setBrightness(uint8_t(gammaCorrectedValue * 255.0f));
		Serial.printf("Brightness set to %0.0f/255\n", brightnessValue);
	}
}

#endif
