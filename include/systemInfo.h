/**
 * @file systemInfo.h
 * @brief Utility for formatting firmware and hardware diagnostics.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Retrieve detailed firmware, chip, flash, and memory information.
 * @return Newline-separated human-readable diagnostic information.
 */
String getSystemInfo();