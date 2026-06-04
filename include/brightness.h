#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>

#if defined(LIGHT_SENSOR)
	#include <LTR303.h>

extern LTR303 lightSensor;

/**
 * @brief Lux range and corresponding brightness level
 */
struct BrightnessBucket {
	float luxMax;		  // Maximum lux for this bucket
	float brightnessMax;  // Brightness at maximum lux (0-1)
};

#endif

/**
 * @brief Manages LED brightness using either an ambient light sensor or manual controls
 */
class BrightnessManager {
  public:
	BrightnessManager();

	void begin();
	void increase();
	void decrease();
	void toggle();
	void setPower(bool on);
	void setBrightness();
	bool isOn();
	void save(Preferences& prefs);
	void load(Preferences& prefs);
	void update();

  private:
	TickType_t lastUpdate = xTaskGetTickCount();
	float brightness;
	bool powerOn;
	bool savePending = false;
	bool buttonPressed = false;
#if defined(LIGHT_SENSOR)
	float ambientLux;
	int bucketIndex;
	BrightnessBucket buckets[3];
	const int numBuckets = 3;
	const float luxUpSmoothingFactor = 0.05f;
	const float luxDownSmoothingFactor = 0.01f;

	uint8_t gammaCorrectedBrightness(float _brightness);
	void adjustBuckets(float delta);
	void printBuckets();
	float mapFloat(float value, float inMin, float inMax, float outMin, float outMax);
	float getLuxForBucket(int index);
	float getBrightnessForBucket(int index);
	int getAmbientBucketIndex(float lux);
	float calculateBrightnessForAmbient(float lux, int index);
#endif
};

extern BrightnessManager brightness;
