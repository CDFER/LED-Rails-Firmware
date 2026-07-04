#include "mapLEDs.h"

// Define the LED arrays
// Pins and pixel counts defined in the board file (./boards/)
#if defined(LED_1_PIN)
CRGB leds1[LED_1_PIXELS];
#endif
#if defined(LED_2_PIN)
CRGB leds2[LED_2_PIXELS];
#endif
#if defined(LED_3_PIN)
CRGB leds3[LED_3_PIXELS];
#endif
#if defined(LED_4_PIN)
CRGB leds4[LED_4_PIXELS];
#endif
#if defined(LED_5_PIN)
CRGB leds5[LED_5_PIXELS];
#endif
#if defined(LED_6_PIN)
CRGB leds6[LED_6_PIXELS];
#endif
#if defined(LED_7_PIN)
CRGB leds7[LED_7_PIXELS];
#endif
#if defined(LED_8_PIN)
CRGB leds8[LED_8_PIXELS];
#endif

// Structure to hold strip information for runtime iteration
struct LedStripInfo {
	CRGB* leds;
	int numPixels;
	int startBlock;
};

std::vector<LedStripInfo> ledStrips;

TaskHandle_t fastLEDDitheringTaskHandle;

LedManager mapLEDs;

extern BrightnessManager brightness;

void enablePower() {
#if defined(LVL_Shifter_EN)
	digitalWrite(LVL_Shifter_EN, LOW);	//Enable LVL Shifter
#endif

#if defined(LED_5V_EN)
	digitalWrite(LED_5V_EN, HIGH);	//Enable 5V Power
#endif

#if defined(LED_POWER_CH1_EN)
	digitalWrite(LED_POWER_CH1_EN, HIGH);  //Enable Power to LED Channel 1
#endif

#if defined(LED_POWER_CH2_EN)
	vTaskDelay(pdMS_TO_TICKS(25));		   // Short delay to ensure stable power before enabling the second channel
	digitalWrite(LED_POWER_CH2_EN, HIGH);  //Enable Power to LED Channel 2
#endif
	Serial.println("LED power rail/s turned on.");
}

void disablePower() {
#if defined(LED_5V_EN)
	pinMode(LED_5V_EN, OUTPUT);
	digitalWrite(LED_5V_EN, LOW);  // Disable 5V Power
#endif

#if defined(LED_POWER_CH1_EN)
	pinMode(LED_POWER_CH1_EN, OUTPUT);
	digitalWrite(LED_POWER_CH1_EN, LOW);  // Disable Power to LED Channel 1
#endif

#if defined(LED_POWER_CH2_EN)
	pinMode(LED_POWER_CH2_EN, OUTPUT);
	digitalWrite(LED_POWER_CH2_EN, LOW);  // Disable Power to LED Channel 2
#endif

#if defined(LVL_Shifter_EN)
	pinMode(LVL_Shifter_EN, OUTPUT);
	digitalWrite(LVL_Shifter_EN, HIGH);	 // Disable LVL Shifter
#endif
	Serial.println("LED power rail/s turned off.");
}

bool anyLedsOn() {
	for (const auto& strip : ledStrips) {
		for (int j = 0; j < strip.numPixels; j++) {
			if (strip.leds[j] != CRGB::Black) {
				return true;
			}
		}
	}
	return false;
}

void LedManager::processFrames() {
	std::vector<LedCommand>* frame;
	// Process all complete frames in queue without blocking
	while (xQueueReceive(frameQueue, &frame, 0) == pdTRUE) {
		if (frame == nullptr)
			continue;

		for (const auto& cmd : *frame) {
			switch (cmd.type) {
				case CMD_SET_BLOCK:
					{
						// Apply gamma correction (γ = 2.0)
						auto gammaCorrect = [](float value) -> uint8_t {
							return static_cast<uint8_t>(pow(value / 255.0f, 2.0) * 255.0f);
						};

						CRGB color = cmd.color;
						color.r = gammaCorrect(color.r);
						color.g = gammaCorrect(color.g);
						color.b = gammaCorrect(color.b);

						if (cmd.block == 0)
							break;

						bool found = false;
						for (const auto& strip : ledStrips) {
							if (cmd.block >= strip.startBlock && cmd.block < strip.startBlock + strip.numPixels) {
								strip.leds[cmd.block - strip.startBlock] = color;
								found = true;
								break;
							}
						}
						if (!found) {
							Serial.printf("Block %d is out of range.\n", cmd.block);
						}
					}
					break;
				case CMD_CLEAR_ALL:
					for (const auto& strip : ledStrips) {
						fill_solid(strip.leds, strip.numPixels, CRGB::Black);
					}
					break;
				case CMD_SET_ALL_COLOR:
					for (const auto& strip : ledStrips) {
						fill_solid(strip.leds, strip.numPixels, cmd.color);
					}
					break;
			}
		}

		delete frame;  // Clean up the dynamically allocated frame
	}
}

void LedManager::task_wrapper(void* pvParameters) {
	static_cast<LedManager*>(pvParameters)->task();
}

void LedManager::task() {
	const TickType_t frameDelay = pdMS_TO_TICKS(20);  // 50fps = 20ms interval
	enum class ledState { OFF, TURNING_ON, ON, TURNING_OFF };
	ledState currentState = ledState::OFF;

	while (true) {
		// Process any queued frame updates before rendering
		processFrames();

		switch (currentState) {
			case ledState::OFF:
				if (FastLED.getBrightness() > 0 && anyLedsOn()) {
					currentState = ledState::TURNING_ON;
				}
				vTaskDelay(frameDelay);
				break;

			case ledState::TURNING_ON:
				FastLED.clear(true);
				enablePower();
				currentState = ledState::ON;
				break;

			case ledState::ON:
				{
					TickType_t lastFrameTime = xTaskGetTickCount();
					uint8_t frameCounter = 0;

					while (frameCounter < 100) {  // Do 100 frames of dithering before checking
						// Process updates between dither frames to keep rendering responsive
						processFrames();
						brightness.update();

						TickType_t startTime = xTaskGetTickCount();
						FastLED.show();

						TickType_t currentTime = xTaskGetTickCount();
						uint32_t elapsedTimeMs = pdTICKS_TO_MS(currentTime - lastFrameTime);
						if (elapsedTimeMs > pdTICKS_TO_MS(frameDelay) + 10) {
							ESP_LOGW("FastLED",
									 "Frame took %u ms, which is longer than expected. FastLED.show() took %u ms.",
									 elapsedTimeMs,
									 pdTICKS_TO_MS(xTaskGetTickCount() - startTime));
						}
						lastFrameTime = currentTime;

						frameCounter++;

						while (xTaskGetTickCount() - startTime < frameDelay) {
							yield();  // Yield to other tasks
						}
					}

					if (FastLED.getBrightness() == 0 || !anyLedsOn()) {
						currentState = ledState::TURNING_OFF;
					}
					break;
				}

			case ledState::TURNING_OFF:
				disablePower();
				currentState = ledState::OFF;
				break;

			default: break;
		}

		brightness.update();
	}
}

void LedManager::begin() {
	frameQueue = xQueueCreate(10, sizeof(std::vector<LedCommand>*));
	if (frameQueue == NULL) {
		Serial.println("Error creating LED frame queue!");
	}

	disablePower();

	ledStrips.clear();

	// Channel 1
#if defined(LED_1_PIN)
	FastLED.addLeds<WS2811, LED_1_PIN, GRB>(leds1, LED_1_PIXELS);
	ledStrips.push_back({ leds1, LED_1_PIXELS, LED_1_START });
#endif

	// Channel 2
#if defined(LED_2_PIN)
	FastLED.addLeds<WS2811, LED_2_PIN, GRB>(leds2, LED_2_PIXELS);
	ledStrips.push_back({ leds2, LED_2_PIXELS, LED_2_START });
#endif

	// Channel 3
#if defined(LED_3_PIN)
	FastLED.addLeds<WS2811, LED_3_PIN, GRB>(leds3, LED_3_PIXELS);
	ledStrips.push_back({ leds3, LED_3_PIXELS, LED_3_START });
#endif

	// Channel 4
#if defined(LED_4_PIN)
	FastLED.addLeds<WS2811, LED_4_PIN, GRB>(leds4, LED_4_PIXELS);
	ledStrips.push_back({ leds4, LED_4_PIXELS, LED_4_START });
#endif

	// Channel 5
#if defined(LED_5_PIN)
	FastLED.addLeds<WS2811, LED_5_PIN, GRB>(leds5, LED_5_PIXELS);
	ledStrips.push_back({ leds5, LED_5_PIXELS, LED_5_START });
#endif

	// Channel 6
#if defined(LED_6_PIN)
	FastLED.addLeds<WS2811, LED_6_PIN, GRB>(leds6, LED_6_PIXELS);
	ledStrips.push_back({ leds6, LED_6_PIXELS, LED_6_START });
#endif

	// Channel 7
#if defined(LED_7_PIN)
	FastLED.addLeds<WS2811, LED_7_PIN, GRB>(leds7, LED_7_PIXELS);
	ledStrips.push_back({ leds7, LED_7_PIXELS, LED_7_START });
#endif

	// Channel 8
#if defined(LED_8_PIN)
	FastLED.addLeds<WS2811, LED_8_PIN, GRB>(leds8, LED_8_PIXELS);
	ledStrips.push_back({ leds8, LED_8_PIXELS, LED_8_START });
#endif

	FastLED.clear(true);
	FastLED.setDither(BINARY_DITHER);

	enablePower();

	brightness.begin();

	xTaskCreate(task_wrapper, "LedManager Task", 4096, this, 5, &fastLEDDitheringTaskHandle);
}

void LedManager::beginFrame() {
	if (currentFrame != nullptr) {
		delete currentFrame;
	}
	currentFrame = new std::vector<LedCommand>();
}

void LedManager::show() {
	if (currentFrame != nullptr) {
		if (xQueueSend(frameQueue, &currentFrame, 0) != pdTRUE) {
			delete currentFrame;  // Drop the frame if the queue is full to avoid leaks
		}
		currentFrame = nullptr;
	}
}

void LedManager::setBlockColorRGB(uint16_t block, CRGB color) {
	if (currentFrame != nullptr) {
		currentFrame->push_back({ CMD_SET_BLOCK, block, color });
	}
}

void LedManager::clear() {
	if (currentFrame != nullptr) {
		currentFrame->push_back({ CMD_CLEAR_ALL, 0, CRGB::Black });
	}
}

void LedManager::setAllLedsColor(CRGB color) {
	if (currentFrame != nullptr) {
		currentFrame->push_back({ CMD_SET_ALL_COLOR, 0, color });
	}
}
