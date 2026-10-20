#include "ioPins.h"

#if defined(GPIO_EXPANDER)
#include "gpioExpander.h"
#endif

bool isExpanderPin(int pin) {
	return pin >= AW9523_PIN_FLAG && pin <= (AW9523_PIN_FLAG | 0x0F);
}

static inline uint8_t expanderDevicePin(int pin) {
	return static_cast<uint8_t>(pin & 0x0F);
}

void ioPinMode(int pin, uint8_t mode) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		gpioExpander.pinMode(expanderDevicePin(pin), mode);
		return;
	}
#endif
	pinMode(pin, mode);
}

void ioPinModeLedCurrent(int pin) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		gpioExpander.pinMode(expanderDevicePin(pin), AW9523_LED_OUTPUT);
		return;
	}
#endif
	pinMode(pin, OUTPUT);
}

void ioSetLedCurrent(int pin, uint8_t dim) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		gpioExpander.setLedCurrent(expanderDevicePin(pin), dim);
		return;
	}
#endif
	digitalWrite(pin, dim ? HIGH : LOW);
}

void ioDigitalWrite(int pin, uint8_t value) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		gpioExpander.digitalWrite(expanderDevicePin(pin), value);
		return;
	}
#endif
	digitalWrite(pin, value);
}

int ioDigitalRead(int pin) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		return gpioExpander.digitalRead(expanderDevicePin(pin));
	}
#endif
	return digitalRead(pin);
}

void ioRefreshInputs() {
#if defined(GPIO_EXPANDER)
	gpioExpander.refreshInputs();
#endif
}

void ioAttachInterrupt(int pin, void (*handler)(), uint8_t mode) {
#if defined(GPIO_EXPANDER)
	if (isExpanderPin(pin)) {
		gpioExpander.attachInterrupt(expanderDevicePin(pin), handler, mode);
		return;
	}
#endif
	attachInterrupt(digitalPinToInterrupt(pin), handler, mode);
}
