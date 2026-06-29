# Zephyr Modbus RTU Slave

A Modbus RTU slave implementation from scratch in C++ on Zephyr RTOS v3.7.0, running on the native POSIX simulator.

## What it does
- Implements a Modbus RTU slave with a 10-register holding register map
- Handles **FC03** (Read Holding Registers) requests
- Handles **FC06** (Write Single Register) requests
- Validates every frame with **CRC16** checksum
- A simulated master thread sends requests every second

## Register Map
| Address | Description | Default |
|---------|-------------|---------|
| 0x00 | Temperature (scaled x10) | 250 = 25.0°C |
| 0x01 | Humidity (scaled x10) | 600 = 60.0% |
| 0x02 | Status | 1 = OK |
| 0x03 | Control (writable) | 0 |

## Architecture
master_thread

└── builds Modbus RTU frame → puts in shared buffer

│

k_sem_give(request_ready)

│

▼

slave_thread

└── validates CRC → parses FC → reads/writes registers

│

k_sem_give(response_ready)

│

▼

master_thread

└── reads response → logs result

## Concepts demonstrated
- Modbus RTU frame structure (address, function code, data, CRC16)
- CRC16 implementation from scratch
- FC03 Read Holding Registers
- FC06 Write Single Register
- Zephyr semaphores for thread synchronization
- Industrial register map design

## Project structure
zephyr-modbus-slave/

├── src/

│   └── main.cpp       # Slave + master threads, CRC16, register map

├── CMakeLists.txt

├── prj.conf

└── README.md

## How to build and run

### Requirements
- Zephyr RTOS v3.7.0
- West v1.5.0+
- Zephyr SDK 0.16.8

### Build
```bash
west build -b native_posix .
```

### Run
```bash
./build/zephyr/zephyr.exe
```

### Expected output
*** Booting Zephyr OS build v3.7.0 ***

[00:00:00.000] <inf> modbus_slave: Slave: ready, address=0x01

[00:00:00.510] <inf> modbus_slave: Master: sending FC03 - read temp & humidity

[00:00:00.510] <inf> modbus_slave: Slave: FC03 Read — start=0 count=2

[00:00:00.510] <inf> modbus_slave: Master: response — Temp=25.0 C  Humidity=60.0 %

[00:00:01.520] <inf> modbus_slave: Master: sending FC06 - write control register

[00:00:01.520] <inf> modbus_slave: Slave: FC06 Write — reg=3 value=1

## Environment
- Developed on Windows 11 with WSL2 (Ubuntu 24.04)
- Runs on native POSIX simulator (no hardware required)
- Portable to any Zephyr-supported board with UART (STM32, nRF52840)
