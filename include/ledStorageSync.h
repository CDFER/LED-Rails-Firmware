/**
 * @file ledStorageSync.h
 * @brief Shared synchronization primitives for LED and NVS access.
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Mutex protecting FastLED operations and Preferences access.
 *
 * Code that calls FastLED.show() or reads/writes the shared NVS namespaces
 * should hold this mutex for the duration of the operation.
 */
extern SemaphoreHandle_t fastLEDPreferencesMutex;