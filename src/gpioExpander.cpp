#include "gpioExpander.h"

#if defined(GPIO_EXPANDER)

GpioExpander gpioExpander;

namespace {
/// Reads one AW9523 register. Returns -1 on a bus error. Caller must hold the bus lock.
int readRegister(uint8_t reg) {
	Wire.beginTransmission(GPIO_EXPANDER_ADDRESS);
	Wire.write(reg);
	if (Wire.endTransmission(false) != 0) {
		return -1;
	}
	if (Wire.requestFrom(static_cast<uint8_t>(GPIO_EXPANDER_ADDRESS), static_cast<uint8_t>(1)) != 1) {
		return -1;
	}
	return Wire.read();
}
}

GpioExpander::GpioExpander()
    : expander(GPIO_EXPANDER_ADDRESS, GPIO_EXPANDER_INT_PIN, &Wire),
      interruptAvailable(GPIO_EXPANDER_INT_PIN != IO_PIN_NOT_DEFINED) {}

void GpioExpander::begin() {
	bool responding;
	{
		I2cBusLock lock(i2cBus);
		Wire.beginTransmission(GPIO_EXPANDER_ADDRESS);
		responding = Wire.endTransmission() == 0;
	}

	// The bus scan only runs on failure. It takes the bus lock itself, so it must run outside our lock.
	if (!responding) {
		Serial.printf("[AW9523] no ACK from 0x%02X, scanning I2C bus\n", GPIO_EXPANDER_ADDRESS);
		i2cBus.scan();
	}

	I2cBusLock lock(i2cBus);
	if (!responding) {
		dumpRegisters("before init");
	}

	// runLoop() performs the lazy device initialisation.
	expander.runLoop();
	// Sets the Imax ceiling for LED current mode pins (about 9.25 mA). Dim values scale from this.
	expander.writeGlobalControl(true, AW9523IoAbstraction::CURRENT_QUARTER);
	const uint8_t deviceId = expander.deviceId();
	if (deviceId != 0x23) {
		Serial.printf("AW9523 device ID: 0x%02X (expect 0x23), INT %s\n",
		              deviceId,
		              interruptAvailable ? "enabled" : "not configured");
	}
}

void GpioExpander::pinMode(uint8_t pin, uint8_t mode) {
	I2cBusLock lock(i2cBus);
	expander.pinDirection(pin, mode);
}

void GpioExpander::digitalWrite(uint8_t pin, uint8_t value) {
	I2cBusLock lock(i2cBus);
	expander.digitalWriteS(pin, value);
}

uint8_t GpioExpander::digitalRead(uint8_t pin) {
	I2cBusLock lock(i2cBus);
	return expander.digitalRead(pin);
}

void GpioExpander::refreshInputs() {
	I2cBusLock lock(i2cBus);
	expander.runLoop();
}

void GpioExpander::attachInterrupt(uint8_t pin, RawIntHandler handler, uint8_t mode) {
	if (!interruptAvailable) {
		return;
	}
	I2cBusLock lock(i2cBus);
	expander.attachInterrupt(pin, handler, mode);
}

void GpioExpander::setLedCurrent(uint8_t pin, uint8_t dim) {
	I2cBusLock lock(i2cBus);
	expander.setPinLedCurrent(pin, dim);
}

void GpioExpander::dumpRegisters(const char* stage) {
	static const struct {
		uint8_t reg;
		const char* name;
	} registers[] = {
		{ 0x00, "IN0" },  { 0x01, "IN1" },  { 0x02, "OUT0" }, { 0x03, "OUT1" }, { 0x04, "CFG0" }, { 0x05, "CFG1" },
		{ 0x06, "INT0" }, { 0x07, "INT1" }, { 0x10, "ID" },   { 0x11, "GCR" },  { 0x12, "LED0" }, { 0x13, "LED1" },
	};
	Serial.printf("[AW9523] registers (%s):\n", stage);
	for (const auto& r : registers) {
		const int value = readRegister(r.reg);
		if (value < 0) {
			Serial.printf("[AW9523]   %-4s (0x%02X) = bus error\n", r.name, r.reg);
		} else {
			Serial.printf("[AW9523]   %-4s (0x%02X) = 0x%02X\n", r.name, r.reg, value);
		}
	}
}

#endif