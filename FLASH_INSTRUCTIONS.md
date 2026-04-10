# WLED W5500 SPI Ethernet - Flash Instructions

Custom WLED build for ESP32 + W5500 SPI Ethernet (Matthijs Dethmers PCB).

## Quick Start

### Option 1: PlatformIO (recommended)

```bash
git clone -b V5-w5500 https://github.com/jessesep/WLED.git
cd WLED
pip install platformio
pio run -e esp32dev_w5500 --target upload
pio run -e esp32dev_w5500 --target uploadfs
```

### Option 2: esptool (pre-built binaries)

Build first, then flash from the output directory:

```bash
pio run -e esp32dev_w5500
pio run -e esp32dev_w5500 --target buildfs
```

Flash:

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash -z \
  0x1000  .pio/build/esp32dev_w5500/bootloader.bin \
  0x8000  .pio/build/esp32dev_w5500/partitions.bin \
  0x10000 .pio/build/esp32dev_w5500/firmware.bin \
  0x310000 .pio/build/esp32dev_w5500/littlefs.bin
```

Mac port: `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`

### Optional: erase flash first (clean start)

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
```

## Pre-loaded Config

The filesystem image includes `cfg.json` with:

- **Ethernet:** W5500 SPI (type 14)
- **LEDs:** 4 x 408 WS2805 (1632 total)
  - Output 1: GPIO 12
  - Output 2: GPIO 14
  - Output 3: GPIO 27
  - Output 4: GPIO 26
- **E1.31:** enabled, port 5568, DMX mode 4
- **FPS:** 42
- **Max power:** 10A (2.5A per output)

## Hardware Pin Map

| Function | GPIO |
|----------|------|
| W5500 MOSI | 23 |
| W5500 MISO | 19 |
| W5500 SCK | 18 |
| W5500 CS | 5 |
| W5500 INT | 4 |
| W5500 RST | tied high |
| LED Output 1 | 12 |
| LED Output 2 | 14 |
| LED Output 3 | 27 |
| LED Output 4 | 26 |

## After Flashing

1. ESP32 reboots automatically
2. WiFi AP "WLED-AP" comes up if ethernet is not connected
3. Open the WLED web UI at the device IP
4. All LED and ethernet settings are pre-configured
5. Settings can be changed via web UI at any time

## Build Info

- Branch: `V5-w5500`
- Base: WLED V5 (0.17.0-dev)
- Framework: Arduino-ESP32 3.3.7 / ESP-IDF 5.3.4
- Platform: Tasmota ESP32 2026.02.30
