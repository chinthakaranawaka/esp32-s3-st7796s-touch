# ESP32-S3 ST7796S Display with XPT2046 Touch and LVGL

This project demonstrates driving an ST7796S SPI display with XPT2046 touch controller using ESP32-S3 and LVGL.

## 📋 Hardware Connections

| Component | Pin | ESP32-S3 Pin |
|-----------|-----|--------------|
| Display MOSI | 11 | GPIO 11 |
| Display CLK | 12 | GPIO 12 |
| Display CS | 10 | GPIO 10 |
| Display DC | 9 | GPIO 9 |
| Display RST | 8 | GPIO 8 |
| Display BL | 7 | GPIO 7 |
| Touch MISO | 13 | GPIO 13 |
| Touch CS | 6 | GPIO 6 |
| Touch IRQ | 5 | GPIO 5 |

## 📦 Dependencies

- ESP-IDF v5.x or later
- LVGL v8.3.0
- ESP LCD Touch XPT2046 driver

## 🚀 Building and Flashing

```bash
# Set up ESP-IDF environment
source ~/esp-idf/export.sh

# Build the project
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash monitor
# esp32-s3-st7796s-touch
