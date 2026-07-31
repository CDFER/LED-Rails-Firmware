#include "mapLeds.h"
#include "ledStorageSync.h"

// Define the LED arrays
// Pins and pixel counts defined in the board file (./boards/)
#if defined(LED_1_PIN)
CRGB leds1[LED_1_PIXELS];
CRGB targetLeds1[LED_1_PIXELS];
#endif
#if defined(LED_2_PIN)
CRGB leds2[LED_2_PIXELS];
CRGB targetLeds2[LED_2_PIXELS];
#endif
#if defined(LED_3_PIN)
CRGB leds3[LED_3_PIXELS];
CRGB targetLeds3[LED_3_PIXELS];
#endif
#if defined(LED_4_PIN)
CRGB leds4[LED_4_PIXELS];
CRGB targetLeds4[LED_4_PIXELS];
#endif
#if defined(LED_5_PIN)
CRGB leds5[LED_5_PIXELS];
CRGB targetLeds5[LED_5_PIXELS];
#endif
#if defined(LED_6_PIN)
CRGB leds6[LED_6_PIXELS];
CRGB targetLeds6[LED_6_PIXELS];
#endif
#if defined(LED_7_PIN)
CRGB leds7[LED_7_PIXELS];
CRGB targetLeds7[LED_7_PIXELS];
#endif
#if defined(LED_8_PIN)
CRGB leds8[LED_8_PIXELS];
CRGB targetLeds8[LED_8_PIXELS];
#endif

// Structure to hold strip information for runtime iteration
struct LedStripInfo {
	CRGB* currentLeds;
	CRGB* targetLeds;
	int pixelCount;
	int startBlock;
};

std::vector<LedStripInfo> ledStrips;

TaskHandle_t ledTaskHandle;

LedManager mapLeds;

extern BrightnessManager brightnessManager;

void setupPowerPins() {
#if defined(LED_5V_EN)
	pinMode(LED_5V_EN, OUTPUT);
#endif

#if defined(LED_POWER_CH1_EN)
	pinMode(LED_POWER_CH1_EN, OUTPUT);
#endif

#if defined(LED_POWER_CH2_EN)
	pinMode(LED_POWER_CH2_EN, OUTPUT);
#endif

#if defined(LVL_Shifter_EN)
	pinMode(LVL_Shifter_EN, OUTPUT);
#endif
}

void enablePower() {
#if defined(LVL_Shifter_EN)
	digitalWrite(LVL_Shifter_EN, LOW);  //Enable LVL Shifter
#endif

#if defined(LED_5V_EN)
	digitalWrite(LED_5V_EN, HIGH);  //Enable 5V Power
#endif

#if defined(LED_POWER_CH1_EN)
	digitalWrite(LED_POWER_CH1_EN, HIGH);  //Enable Power to LED Channel 1
#endif

#if defined(LED_POWER_CH2_EN)
	vTaskDelay(pdMS_TO_TICKS(25));         // Short delay to ensure stable power before enabling the second channel
	digitalWrite(LED_POWER_CH2_EN, HIGH);  //Enable Power to LED Channel 2
#endif
	FastLED.clear(true);
	Serial.println("LED power rail/s turned on.");
}

void disablePower() {
	FastLED.clear(true);

#if defined(LED_5V_EN)
	digitalWrite(LED_5V_EN, LOW);  // Disable 5V Power
#endif

#if defined(LED_POWER_CH1_EN)
	digitalWrite(LED_POWER_CH1_EN, LOW);  // Disable Power to LED Channel 1
#endif

#if defined(LED_POWER_CH2_EN)
	digitalWrite(LED_POWER_CH2_EN, LOW);  // Disable Power to LED Channel 2
#endif

#if defined(LVL_Shifter_EN)
	digitalWrite(LVL_Shifter_EN, HIGH);  // Disable LVL Shifter
#endif
	Serial.println("LED power rail/s turned off.");
}

bool anyLedsOn() {
	for (const auto& strip : ledStrips) {
		for (int pixelIndex = 0; pixelIndex < strip.pixelCount; pixelIndex++) {
			if (strip.currentLeds[pixelIndex] != CRGB::Black || strip.targetLeds[pixelIndex] != CRGB::Black) {
				return true;
			}
		}
	}
	return false;
}

uint8_t fadeChannel(uint8_t current, uint8_t target) {
	const uint8_t minFadeThreshold = 16;  // Threshold for faster fade when brightness is low
	if (current == target) {
		return current;
	} else if (current < minFadeThreshold) {
		return (current + target * 3) / 4;
	} else {
		return (current + target) / 2;  // Simple averaging for smoother fade
	}
}

void LedManager::updateFade() {
	for (const auto& strip : ledStrips) {
		for (int pixelIndex = 0; pixelIndex < strip.pixelCount; pixelIndex++) {
			CRGB target = brightnessManager.isOn() ? strip.targetLeds[pixelIndex] : CRGB::Black;
			strip.currentLeds[pixelIndex].r = fadeChannel(strip.currentLeds[pixelIndex].r, target.r);
			strip.currentLeds[pixelIndex].g = fadeChannel(strip.currentLeds[pixelIndex].g, target.g);
			strip.currentLeds[pixelIndex].b = fadeChannel(strip.currentLeds[pixelIndex].b, target.b);
		}
	}
}

void LedManager::processFrames() {
	std::vector<LedCommand>* pendingFrame;
	// Process all complete frames in queue without blocking
	while (xQueueReceive(frameQueue, &pendingFrame, 0) == pdTRUE) {
		if (pendingFrame == nullptr)
			continue;

		for (const auto& command : *pendingFrame) {
			switch (command.type) {
				case CMD_SET_BLOCK:
					{
						// Apply gamma correction (γ = 2.0)
						auto gammaCorrect = [](float value) -> uint8_t {
							return static_cast<uint8_t>(pow(value / 255.0f, 2.0) * 255.0f);
						};

						CRGB color = command.color;
						color.r = gammaCorrect(color.r);
						color.g = gammaCorrect(color.g);
						color.b = gammaCorrect(color.b);

						if (command.block == 0)
							break;

						bool found = false;
						for (const auto& strip : ledStrips) {
							if (command.block >= strip.startBlock
							    && command.block < strip.startBlock + strip.pixelCount) {
								strip.targetLeds[command.block - strip.startBlock] = color;
								found = true;
								break;
							}
						}
						if (!found) {
							Serial.printf("Block %d is out of range.\n", command.block);
						}
					}
					break;
				case CMD_CLEAR_ALL:
					for (const auto& strip : ledStrips) {
						fill_solid(strip.targetLeds, strip.pixelCount, CRGB::Black);
					}
					break;
				case CMD_SET_ALL_COLOR:
					for (const auto& strip : ledStrips) {
						fill_solid(strip.targetLeds, strip.pixelCount, command.color);
					}
					break;
			}
		}

		delete pendingFrame;  // Clean up the dynamically allocated frame
	}
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
				if (brightnessManager.isOn() && FastLED.getBrightness() > 0 && anyLedsOn()) {
					currentState = ledState::TURNING_ON;
				}
				vTaskDelay(pdMS_TO_TICKS(50));  // Sleep a bit to yield to other tasks
				break;

			case ledState::TURNING_ON:
				if (!brightnessManager.isOn()) {
					currentState = ledState::OFF;
					break;
				}
				enablePower();
				currentState = ledState::ON;
				break;

			case ledState::ON:
				{
					uint8_t frameCounter = 0;
					TickType_t lastFrameTime = xTaskGetTickCount();

					while (frameCounter
					       < int(1000 / frameDelay)) {  // Do about 1 second of frames before checking anyLedsOn()
						// Process updates between dither frames to keep rendering responsive
						processFrames();
						updateFade();
						brightnessManager.update();

						xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
						TickType_t startTime = xTaskGetTickCount();

						FastLED.show();
						xSemaphoreGive(fastLEDPreferencesMutex);

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

						vTaskDelayUntil(&lastFrameTime, frameDelay);
					}

					if (!brightnessManager.isOn() || FastLED.getBrightness() == 0 || !anyLedsOn()) {
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

		brightnessManager.update();
	}
}

void LedManager::begin() {
	fastLEDPreferencesMutex = xSemaphoreCreateMutex();
	if (fastLEDPreferencesMutex == nullptr) {
		Serial.println("Error creating FastLED/Preferences mutex!");
	}

	frameQueue = xQueueCreate(10, sizeof(std::vector<LedCommand>*));
	if (frameQueue == NULL) {
		Serial.println("Error creating LED frame queue!");
	}

	setupPowerPins();
	disablePower();

	ledStrips.clear();

	// Channel 1
#if defined(LED_1_PIN)
	FastLED.addLeds<WS2811, LED_1_PIN, GRB>(leds1, LED_1_PIXELS);
	ledStrips.push_back({ leds1, targetLeds1, LED_1_PIXELS, LED_1_START });
#endif

	// Channel 2
#if defined(LED_2_PIN)
	FastLED.addLeds<WS2811, LED_2_PIN, GRB>(leds2, LED_2_PIXELS);
	ledStrips.push_back({ leds2, targetLeds2, LED_2_PIXELS, LED_2_START });
#endif

	// Channel 3
#if defined(LED_3_PIN)
	FastLED.addLeds<WS2811, LED_3_PIN, GRB>(leds3, LED_3_PIXELS);
	ledStrips.push_back({ leds3, targetLeds3, LED_3_PIXELS, LED_3_START });
#endif

	// Channel 4
#if defined(LED_4_PIN)
	FastLED.addLeds<WS2811, LED_4_PIN, GRB>(leds4, LED_4_PIXELS);
	ledStrips.push_back({ leds4, targetLeds4, LED_4_PIXELS, LED_4_START });
#endif

	// Channel 5
#if defined(LED_5_PIN)
	FastLED.addLeds<WS2811, LED_5_PIN, GRB>(leds5, LED_5_PIXELS);
	ledStrips.push_back({ leds5, targetLeds5, LED_5_PIXELS, LED_5_START });
#endif

	// Channel 6
#if defined(LED_6_PIN)
	FastLED.addLeds<WS2811, LED_6_PIN, GRB>(leds6, LED_6_PIXELS);
	ledStrips.push_back({ leds6, targetLeds6, LED_6_PIXELS, LED_6_START });
#endif

	// Channel 7
#if defined(LED_7_PIN)
	FastLED.addLeds<WS2811, LED_7_PIN, GRB>(leds7, LED_7_PIXELS);
	ledStrips.push_back({ leds7, targetLeds7, LED_7_PIXELS, LED_7_START });
#endif

	// Channel 8
#if defined(LED_8_PIN)
	FastLED.addLeds<WS2811, LED_8_PIN, GRB>(leds8, LED_8_PIXELS);
	ledStrips.push_back({ leds8, targetLeds8, LED_8_PIXELS, LED_8_START });
#endif

	FastLED.clear(false);
	FastLED.setDither(BINARY_DITHER);

	brightnessManager.begin();

	xTaskCreate(
	    [](void* pvParameters) {
		    static_cast<LedManager*>(pvParameters)->task();
	    },
	    "LedManager Task",
	    4096,
	    this,
	    5,
	    &ledTaskHandle);
}

void LedManager::beginFrame() {
	if (currentFrame != nullptr) {
		delete currentFrame;
	}
	currentFrame = new std::vector<LedCommand>();
}

void LedManager::show() {
	if (currentFrame != nullptr) {
		if (frameQueue == NULL || xQueueSend(frameQueue, &currentFrame, 0) != pdTRUE) {
			Serial.println("Frame queue full, dropping frame.");
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
