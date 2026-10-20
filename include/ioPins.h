/**
 * @file ioPins.h
 * @brief Unified pin access for native GPIO and AW9523B expander pins.
 *
 * Expander pins are written in board files as P0_n (port 0) or P1_n (port 1).
 * Each token has bit 7 set, so it cannot collide with native GPIO numbers or
 * the 255 "unconfigured" sentinel used by status LEDs.
 */

#pragma once

#include <Arduino.h>

#define AW9523_PIN_FLAG 0x80

#define P0_0 (AW9523_PIN_FLAG | 0)
#define P0_1 (AW9523_PIN_FLAG | 1)
#define P0_2 (AW9523_PIN_FLAG | 2)
#define P0_3 (AW9523_PIN_FLAG | 3)
#define P0_4 (AW9523_PIN_FLAG | 4)
#define P0_5 (AW9523_PIN_FLAG | 5)
#define P0_6 (AW9523_PIN_FLAG | 6)
#define P0_7 (AW9523_PIN_FLAG | 7)
#define P1_0 (AW9523_PIN_FLAG | 8)
#define P1_1 (AW9523_PIN_FLAG | 9)
#define P1_2 (AW9523_PIN_FLAG | 10)
#define P1_3 (AW9523_PIN_FLAG | 11)
#define P1_4 (AW9523_PIN_FLAG | 12)
#define P1_5 (AW9523_PIN_FLAG | 13)
#define P1_6 (AW9523_PIN_FLAG | 14)
#define P1_7 (AW9523_PIN_FLAG | 15)

/** @brief True when the pin token refers to an AW9523B expander pin. */
bool isExpanderPin(int pin);

/** @brief pinMode for native or expander pins. */
void ioPinMode(int pin, uint8_t mode);

/** @brief Configure a pin as a constant-current LED sink. Expander pins only; native pins become outputs. */
void ioPinModeLedCurrent(int pin);

/** @brief Set LED sink current with an 8-bit dim value (0 = off). Native pins are driven high or low. */
void ioSetLedCurrent(int pin, uint8_t dim);

/** @brief digitalWrite for native or expander pins; expander writes are flushed immediately. */
void ioDigitalWrite(int pin, uint8_t value);

/** @brief digitalRead for native or expander pins; expander reads use the last refreshed value. */
int ioDigitalRead(int pin);

/** @brief Refresh cached expander inputs. Does nothing when no expander is configured. */
void ioRefreshInputs();

/** @brief attachInterrupt for native or expander pins. Expander pins without an INT line are ignored. */
void ioAttachInterrupt(int pin, void (*handler)(), uint8_t mode);
