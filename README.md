# BMP280 Device Driver

## Overview

This is a Linux kernel module for the BMP280 digital pressure and temperature sensor.  
It exposes real-time temperature and pressure readings to userspace via the sysfs filesystem.

---

## Features

- Reads temperature and pressure from the BMP280 sensor using the I2C interface.
- Exposes readings through a sysfs attribute for easy access from userspace.
- Includes integer compensation (no floating-point math) as per the official Bosch datasheet.

---

## BMP280 to Raspberry Pi 4 Pinout

| BMP280 Pin | Raspberry Pi 4 Connection |
|------------|---------------------------|
| VCC        | 3.3V (pin 1)              |
| GND        | Ground (pin 6)            |
| SCL        | GPIO 3 (pin 5)            |
| SDA        | GPIO 2 (pin 3)            |

---

## Documentation

- [Linux Device Driver Tutorial - Embetronicx](https://embetronicx.com/tutorials/linux/device-drivers/linux-device-driver-part-1-introduction/)
- [Bosch BMP280 Datasheet](https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp280/)

---

## Installation

```bash
# 1. Build the kernel module
make

# 2. Insert the module
sudo insmod bmp280.ko

# 3. Instantiate the device (replace 0x76 with your sensor address if needed)
echo bmp280 0x76 | sudo tee /sys/bus/i2c/devices/i2c-1/new_device

# 4. Navigate to the sysfs directory for your device
cd /sys/bus/i2c/devices/1-0076/

# 5. Read temperature and pressure
cat Bmp280-Calculations

