/**
 * @file systemInfo.h
 * @brief Utilities for reporting firmware and hardware status
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Retrieves detailed system information as a formatted string.
 * @return JSON or human-readable system information string
 */
String getSystemInfo();