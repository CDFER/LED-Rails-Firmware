#include "i2cBus.h"

#include <Wire.h>

I2cBus i2cBus;

I2cBus::I2cBus() : mutex(xSemaphoreCreateMutex()) {}

void I2cBus::begin() {
	I2cBusLock lock(*this);
#if defined(SDA_PIN) && defined(SCL_PIN)
	Wire.begin(SDA_PIN, SCL_PIN, 50000);
#endif
}

void I2cBus::scan() {
	I2cBusLock lock(*this);
	vTaskDelay(1000);
	Serial.println("Scanning I2C bus...");
	for (uint8_t address = 1; address < 254; address++) {
		Wire.beginTransmission(address);
		if (Wire.endTransmission() == 0) {
			Serial.printf("I2C device found at 0x%02X\n", address);
		}
		vTaskDelay(10);
	}
	Serial.println("I2C scan complete.");
}
