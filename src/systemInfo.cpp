#include "systemInfo.h"
#include <Arduino.h>

String getSystemInfo() {
	FlashMode_t mode = (FlashMode_t)ESP.getFlashChipMode();
	String flashMode;

	// Convert flash mode to human-readable string
	switch (mode) {
		case FM_QIO: flashMode = "Quad I/O (QIO)"; break;
		case FM_QOUT: flashMode = "Quad Output (QOUT)"; break;
		case FM_DIO: flashMode = "Dual I/O (DIO)"; break;
		case FM_DOUT: flashMode = "Dual Output (DOUT)"; break;
		case FM_FAST_READ: flashMode = "Fast Read"; break;
		case FM_SLOW_READ: flashMode = "Slow Read"; break;
		default: flashMode = "Unknown"; break;
	}

	String info = "\n";
	info += String(ARDUINO_BOARD) + "\n";
#if defined(FIRMWARE) && defined(FIRMWARE_VERSION)
	info += String(FIRMWARE) + " V" + FIRMWARE_VERSION + "\n";
#endif
	info += "Built: " + String(__DATE__) + " " + __TIME__ + "\n";
	info += String(ESP.getChipModel()) + " Rev:" + ESP.getChipRevision() + "\n";
	info += String(ESP.getChipCores()) + " Core @ " + ESP.getCpuFreqMHz() + "MHz\n";
	info += String(ESP.getFlashChipSize() / (1024 * 1024)) + "MiB Flash @ " + (ESP.getFlashChipSpeed() / (1000 * 1000))
			+ "MHz in " + flashMode + " Mode\n";
	info += "RAM Heap: " + String(ESP.getHeapSize() / 1024) + "kiB\n";
	info += "IDF SDK: " + String(ESP.getSdkVersion()) + "\n";

	return info;
}