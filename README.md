# LED-Rails-Firmware

[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-orange?logo=platformio)](https://platformio.org/)

Firmware for my LED train maps (Currently includes Auckland, Wellington and Melbourne)

You can edit and run your firmware using PlatformIO (An extension for VSCode)

The firmware is responsible for:

1. Connecting to Wi-Fi
2. Fetching live train data from the [GTFS Realtime Cache API](https://github.com/CDFER/GTFS-Realtime-Cache-Server)
3. Processing data to determine train locations
4. Controlling WS2812B LEDs to display train positions

## Web Installer

Easily flash the latest firmware to your map using your browser:

[Open the LED Train Map Web Installer](https://cdfer.github.io/Auckland-LED-Train-Map/led-rails.html)

- Works with Firefox, Chrome, Edge, or any Web Serial-compatible browser
- Follow on-screen instructions to connect and flash your device

The installer is a Vite/Svelte/TypeScript app in `Web Installer`:

```sh
cd "Web Installer"
npm install
npm run check
npm run dev
```

Open `http://localhost:5173/led-rails.html` while developing. The deployment workflow builds the app automatically before copying it to GitHub Pages.

## Device Control Panel

When a train map is connected to Wi-Fi, open its local IP address in a browser to use the embedded device control panel. It shows connection and map status, changes LED power and brightness, selects available display modes, and manages saved Wi-Fi network names without returning saved passwords. On sensor-enabled boards, it also lets you edit the four-point ambient-light brightness curve. Curve points are validated as one complete update, persisted to NVS, and applied by the LED task without allowing the web server to access FastLED state directly.

The offline Svelte/TypeScript source is in `Device Web`. PlatformIO regenerates `include/deviceWebAssets.h` when the control panel source changes. To work on the control panel directly:

```sh
cd "Device Web"
npm install
npm run check
npm run build
```

The generated assets are gzip-compressed and served from firmware flash, so the device page does not require internet access or a filesystem partition.
