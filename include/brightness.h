/**
 * @file brightnessManager.h
 * @brief LED brightnessManager, power, and ambient-light management.
 */

#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <atomic>
#include <mutex>

/**
 * @brief Number of configurable points in the ambient-light brightness curve.
 */
constexpr uint8_t BRIGHTNESS_BUCKET_COUNT = 4;

/**
 * @brief Upper lux boundary and normalized brightness for one curve point.
 *
 * Buckets are ordered by increasing lux. Brightness is normalized to the range
 * 0.0 to 1.0 before gamma correction is applied.
 */
struct BrightnessBucket {
	float luxMax;         ///< Exclusive upper lux boundary for this bucket.
	float brightnessMax;  ///< Normalized brightness at the upper boundary (0.0-1.0).
};

/**
 * @brief Copyable snapshot of the complete ambient-light brightness curve.
 */
struct BrightnessCurve {
	BrightnessBucket buckets[BRIGHTNESS_BUCKET_COUNT];
};

#if defined(LIGHT_SENSOR)
#include <LTR303.h>

extern LTR303 lightSensor;
#endif

/**
 * @brief Task-notification bits used to request brightnessManager changes.
 *
 * The public control methods set these bits and return immediately. The LED
 * task consumes them from BrightnessManager::update().
 */
#define EVT_INCREASE (1 << 0)
#define EVT_DECREASE (1 << 1)
#define EVT_UPDATE_BRIGHTNESS (1 << 2)
#define EVT_UPDATE_CURVE (1 << 3)

/**
 * @brief Manages LED brightnessManager using an ambient-light sensor or manual controls.
 *
 * The manager owns the persisted brightnessManager settings and the logical power
 * state. FastLED operations and storage access are performed by the LED task;
 * the public control methods are safe to call from other tasks.
 */
class BrightnessManager {
  public:
	/**
	 * @brief Construct a new Brightness Manager object
	 */
	BrightnessManager();

	/**
	 * @brief Initialize the brightnessManager manager
	 */
	void begin();

	/**
	 * @brief Increase manual brightnessManager level
	 */
	void increase();

	/**
	 * @brief Decrease manual brightnessManager level
	 */
	void decrease();

	/**
	 * @brief Toggle the logical LED power state.
	 *
	 * The request is recorded immediately and applied by the LED task.
	 */
	void toggle();

	/**
	 * @brief Set the logical power state of the LEDs.
	 *
	 * The request is recorded immediately and applied by the LED task.
	 * @param powerOnRequested True to turn on, false to turn off
	 */
	void setPower(bool powerOnRequested);

	/**
	 * @brief Return the current FastLED output brightness.
	 */
	uint8_t getBrightness() const;

#if defined(LIGHT_SENSOR)
	/** @brief Check the LTR303 connection without racing the LED task. */
	bool isLightSensorConnected();

	/** @brief Return the smoothed ambient-light level in lux. */
	float getAmbientLux() const;

	/** @brief Return the current curve-derived brightness target as a percentage. */
	float getAmbientBrightness() const;

	/**
	 * @brief Copy the current ambient-light curve without exposing mutable state.
	 * @param curve Destination for the current curve snapshot.
	 * @return True when the curve is available on this sensor-enabled build.
	 */
	bool getBrightnessCurve(BrightnessCurve& curve) const;

	/**
	 * @brief Queue a validated ambient-light curve for the LED task to apply.
	 * @param curve Complete curve snapshot in normalized brightness units.
	 * @return False when the curve violates its ordering or range constraints.
	 */
	bool requestBrightnessCurve(const BrightnessCurve& curve);
#endif

	/**
	 * @brief Apply the current brightness target to FastLED.
	 *
	 * When power is off, LedManager performs the pixel fade and power-rail
	 * shutdown; this method does not immediately erase the saved LED targets.
	 */
	void setBrightness();

	/**
	 * @brief Check whether the logical LED power state is on.
	 * @return true when LED output should be active, otherwise false.
	 */
	bool isOn();

	/**
	 * @brief Save brightness settings and sensor buckets to NVS.
	 */
	void save();

	/**
	 * @brief Load brightness settings and sensor buckets from NVS.
	 */
	void load();

	/**
	 * @brief Process pending requests and perform periodic sensor/storage work.
	 *
	 * This method is called by the LED task and should not be called concurrently
	 * from another task.
	 */
	void update();

  private:
	TickType_t lastUpdateTick = xTaskGetTickCount();  ///< Last time the brightness was updated.
	float brightnessValue;           ///< Current brightness level (0.0-1.0 for sensor builds, 0-255 otherwise).
	std::atomic<bool> powerEnabled;  ///< Logical LED power state requested by the user.
	bool savePending = false;        ///< True when a changed setting still needs to be written to NVS.
	bool buttonPressed = false;      ///< True when a manual change should bypass ambient-brightness smoothing.
#if defined(LIGHT_SENSOR)
	float ambientLux;                     ///< Smoothed ambient light level in lux.
	int bucketIndex;                      ///< Index of the bucket containing the current ambient lux value.
	mutable std::mutex lightSensorMutex;  ///< Protects LTR303 I2C access across tasks.
	BrightnessBucket buckets[BRIGHTNESS_BUCKET_COUNT];  ///< Ordered light-to-brightness curve used for interpolation.
	static constexpr int numBuckets = BRIGHTNESS_BUCKET_COUNT;
	mutable std::mutex curveMutex;               ///< Protects current and pending curve snapshots.
	BrightnessCurve pendingCurve{};              ///< Latest curve request waiting for the LED task.
	bool curveUpdatePending = false;             ///< True when pendingCurve must be applied.
	const float luxUpSmoothingFactor = 0.20f;    ///< Smoothing factor when measured lux increases.
	const float luxDownSmoothingFactor = 0.04f;  ///< Smoothing factor when measured lux decreases.

	/** @brief Apply the newest queued curve from the LED task. */
	void applyPendingCurve();

	/** @brief Validate ordering, finite values, and normalized ranges. */
	bool isValidBrightnessCurve(const BrightnessCurve& curve) const;

	/**
	 * @brief Apply gamma correction to a brightness value.
	 * @param brightnessValue Input linear brightness value.
	 * @return Gamma-corrected 8-bit brightness value.
	 */
	uint8_t gammaCorrectedBrightness(float brightnessValue);

	/**
	 * @brief Adjust all brightness buckets by a delta.
	 * @param delta Amount to change brightness by.
	 */
	void adjustBuckets(float delta);

	/**
	 * @brief Print bucket configuration to Serial for debugging
	 */
	void printBuckets();

	/**
	 * @brief Map a float value from one range to another.
	 * @param value Value to map.
	 * @param inMin Lower bound of the input range.
	 * @param inMax Upper bound of the input range.
	 * @param outMin Lower bound of the output range.
	 * @param outMax Upper bound of the output range.
	 * @return The corresponding value in the output range.
	 */
	float mapFloat(float value, float inMin, float inMax, float outMin, float outMax);

	/**
	 * @brief Get a bucket boundary, including virtual boundaries outside the array.
	 * @param bucketIndex Bucket index; values below zero and above the final bucket
	 *                    return virtual boundary values.
	 * @return Lux value at the requested boundary.
	 */
	float getLuxForBucket(int bucketIndex);

	/**
	 * @brief Get a bucket brightness boundary, including virtual boundaries outside the array.
	 * @param bucketIndex Bucket index; values below zero and above the final bucket
	 *                    return virtual boundary values.
	 * @return Normalized brightness value at the requested boundary.
	 */
	float getBrightnessForBucket(int bucketIndex);

	/**
	 * @brief Determine which bucket contains a measured lux value.
	 * @param lux Smoothed ambient-light level in lux.
	 * @return Index of the bucket containing the lux value.
	 */
	int getAmbientBucketIndex(float lux);

	/**
	 * @brief Interpolate a normalized brightness target for the current lux value.
	 * @param lux Ambient-light level in lux.
	 * @param bucketIndex Bucket containing the lux value.
	 * @return Normalized brightness target in the range 0.0 to 1.0.
	 */
	float calculateBrightnessForAmbient(float lux, int bucketIndex);
#endif
};

/**
 * @brief Global brightnessManager manager instance
 */
extern BrightnessManager brightnessManager;
