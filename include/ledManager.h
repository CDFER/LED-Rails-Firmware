#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#include <FastLED.h>
#include <vector>

// Only declare the functions needed by main.cpp
void setupLeds();
void setBlockColorRGB(uint16_t block, CRGB color);
void clearLEDs();
void setAllLedsColor(CRGB color);
void suspendDithering();
void resumeDithering();

#endif
