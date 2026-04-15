
## <b>SensorDemo_BLESensor-App Application Description</b>
  
This application shows how to implement proprietary BLE profiles.
The communication is done using a STM32 Nucleo board and a Smartphone with BTLE.

Example Description:

This application shows how to implement a peripheral device tailored for 
interacting with the "ST BLE Sensor" app for Android/iOS devices.

The "ST BLE Sensor" app is freely available on both GooglePlay and iTunes
  - iTunes: https://itunes.apple.com/us/app/st-bluems/id993670214
  - GooglePlay: https://play.google.com/store/apps/details?id=com.st.bluems
The source code of the "ST BLE Sensor" app is available on GitHub at:
  - iOS: https://github.com/STMicroelectronics-CentralLabs/STBlueMS_iOS
  - Android: https://github.com/STMicroelectronics-CentralLabs/STBlueMS_Android

@note: NO SUPPORT WILL BE PROVIDED TO YOU BY STMICROELECTRONICS FOR ANY OF THE
ANDROID/iOS app INCLUDED IN OR REFERENCED BY THIS PACKAGE.

After establishing the connection between the STM32 board and the smartphone:
 -  the temperature and the pressure emulated values are sent by the STM32 board to 
    the mobile device and are shown in the ENVIRONMENTAL tab;
 -  the emulated sensor fusion data sent by the STM32 board to the mobile device 
    reflects into the cube rotation showed in the app's MEMS SENSOR FUSION tab
 -  the plot of the emulated data (temperature, pressure, sensor fusion data, 
    accelerometer, gyroscope and magnetometer) sent by the board are shown in the 
	PLOT DATA tab;
 -  in the RSSI & Battery tab the RSSI value is shown.
According to the value of the #define USE_BUTTON in file app_bluenrg_ms.c, the 
environmental and the motion data can be sent automatically (with 1 sec period) 
or when the User Button is pressed.

The communication is done using a vendor specific profile.

Known limitations:

- When starting the project from Example Selector in STM32CubeMX and regenerating it
  from ioc file, you may face a build issue. To solve it, if you started the project for the
  Nucleo-L476RG board, remove from the IDE project the file stm32l4xx_nucleo.c in the Application/User
  virtual folder and delete, from Src and Inc folders, the files: stm32l4xx_nucleo.c, stm32l4xx_nucleo.h
  and stm32l4xx_nucleo_errno.h.

### <b>GATT Database</b>

#### Service 1: Hardware Service (HW\_Serv\_W2ST)

| Field | Value |
|-------|-------|
| **UUID** | `00000000-0001-11e1-9ab4-0002a5d5c51b` |
| **Type** | Primary Service |

**Characteristic 1-1: Environmental**

| Field | Value |
|-------|-------|
| **UUID** | `00140000-0001-11e1-ac36-0002a5d5c51b` |
| **Properties** | Notify, Read |
| **Value Length** | 8 bytes |
| **Data Format** | `[timestamp:2][pressure:4][temperature:2]` |

- `timestamp` (2 bytes, LE): `HAL_GetTick() >> 3`
- `pressure` (4 bytes, LE): int32, unit = 1/100 hPa (e.g. 100000 = 1000.00 hPa)
- `temperature` (2 bytes, LE): int16, unit = 1/10 °C (e.g. 250 = 25.0 °C)
- **Data Source**: Temperature from LSM6DSL internal sensor; pressure is a fixed placeholder (no LPS22HB driver)

**Characteristic 1-2: AccGyroMag**

| Field | Value |
|-------|-------|
| **UUID** | `00E00000-0001-11e1-ac36-0002a5d5c51b` |
| **Properties** | Notify |
| **Value Length** | 20 bytes |
| **Data Format** | `[timestamp:2][acc_x:2][acc_y:2][acc_z:2][gyro_x:2][gyro_y:2][gyro_z:2][mag_x:2][mag_y:2][mag_z:2]` |

- `timestamp` (2 bytes, LE): `HAL_GetTick() >> 3`
- `acc_{x,y,z}` (2 bytes each, LE): int16, accelerometer in mg — **real data from LSM6DSL**
- `gyro_{x,y,z}` (2 bytes each, LE): int16, gyroscope in mdps — **real data from LSM6DSL**
- `mag_{x,y,z}` (2 bytes each, LE): int16, magnetometer — fixed 0 (no LSM3MDL driver)

#### Service 2: Software Service (SW\_Serv\_W2ST)

| Field | Value |
|-------|-------|
| **UUID** | `00000000-0002-11e1-9ab4-0002a5d5c51b` |
| **Type** | Primary Service |

**Characteristic 2-1: Quaternions**

| Field | Value |
|-------|-------|
| **UUID** | `00000100-0001-11e1-ac36-0002a5d5c51b` |
| **Properties** | Notify |
| **Value Length** | 8 bytes (2 + 6 × SEND\_N\_QUATERNIONS) |
| **Data Format** | `[timestamp:2][qi_x:2][qi_y:2][qi_z:2]` |

- `timestamp` (2 bytes, LE): `HAL_GetTick() >> 3`
- `qi_{x,y,z}` (2 bytes each, LE): int16, derived from accelerometer tilt data
- `SEND_N_QUATERNIONS` = 1

### <b>Sensor Data Sources</b>

| Sensor Type | Source | Component | Notes |
|-------------|--------|-----------|-------|
| Accelerometer | **Real** | LSM6DSL (I2C2) | 3-axis, unit: mg |
| Gyroscope | **Real** | LSM6DSL (I2C2) | 3-axis, unit: mdps |
| Temperature | **Real** | LSM6DSL internal | Accuracy ~±1°C |
| Magnetometer | Unavailable | — | LSM3MDL driver not included, value = 0 |
| Pressure | Placeholder | — | LPS22HB driver not included, fixed 1000.00 hPa |

### <b>Keywords</b>

BLE, Peripheral, SPI, BlueNRG-M0, BlueNRG-MS

### <b>Directory contents</b>

 - app_bluenrg_ms.c       SensorDemo_BLESensor-App initialization and applicative code
 
 - gatt_db.c              Functions for building GATT DB and handling GATT events
 
 - hci_tl_interface.c     Interface to the BlueNRG HCI Transport Layer 
 
 - main.c                 Main program body
  
 - sensor.c               Sensor init and state machine
 
 - stm32**xx_hal_msp.c    Source code for MSP Initialization and de-Initialization

 - stm32**xx_it.c         Source code for interrupt Service Routines

 - stm32**xx_nucleo.c     Source file for the BSP Common driver 
						
 - stm32**xx_nucleo_bus.c Source file for the BSP BUS IO driver
 
 - system_stm32**xx.c     CMSIS Cortex-Mx Device Peripheral Access Layer System Source File

 - target_platform.c      Get information on the target device memory
  
### <b>Hardware and Software environment</b>

  - This example runs on STM32 Nucleo boards with X-NUCLEO-IDB05A2 STM32 expansion board
    (the X-NUCLEO-IDB05A1 expansion board can be also used)
  - This example has been tested with STMicroelectronics:
    - NUCLEO-L476RG RevC board
    and can be easily tailored to any other supported device and development board.

ADDITIONAL_BOARD : X-NUCLEO-IDB05A2 https://www.st.com/en/ecosystems/x-nucleo-idb05a2.html
ADDITIONAL_COMP : BlueNRG-M0 https://www.st.com/en/wireless-connectivity/bluenrg-m0.html

### <b>How to use it?</b>

In order to make the program work, you must do the following:

 - WARNING: before opening the project with any toolchain be sure your folder
   installation path is not too in-depth since the toolchain may report errors
   after building.

 - Open STM32CubeIDE (this firmware has been successfully tested with Version 1.17.0).
   Alternatively you can use the Keil uVision toolchain (this firmware
   has been successfully tested with V5.38.0) or the IAR toolchain (this firmware has 
   been successfully tested with Embedded Workbench V9.20.1).

 - Rebuild all files and load your image into target memory.

 - Run the example.

 - Alternatively, you can download the pre-built binaries in "Binary" 
   folder included in the distributed package.

### <b>How to run the BLE Central on Raspberry Pi</b>

Prerequisites: Raspberry Pi 3/4/5 with Bluetooth, Python 3.8+, [uv](https://docs.astral.sh/uv/) installed.

```bash
# 1. Clone the repo and enter the central directory
cd rpi_central

# 2. Create virtual environment and install dependencies via uv
uv venv
source .venv/bin/activate
uv pip install -r requirements.txt

# 3. Run the BLE Central (requires root for BLE access)
sudo .venv/bin/python ble_central.py
```

> **Note:** `sudo` is required because bluepy needs root privileges to access the BLE adapter.
> Using `.venv/bin/python` explicitly ensures the venv packages are available under `sudo`.

After launching, the script will:
 1. Scan for nearby BLE devices (10 seconds)
 2. List discovered devices — select the STM32 board by index
 3. Connect and automatically subscribe to Environmental, AccGyroMag, and Quaternions notifications
 4. Print parsed sensor data in real-time; press `Ctrl+C` to disconnect

Example output:
```
[ENV]  T=26.3 C  P=1000.00 hPa
[IMU]  Acc=(   -23,    41,  1004) mg  Gyro=(   1250,   -870,    340) mdps
[QUAT] X=   -23  Y=    41  Z=  1004
```

### <b>Author</b>

SRA Application Team

### <b>License</b>

Copyright (c) 2025 STMicroelectronics.
All rights reserved.

This software is licensed under terms that can be found in the LICENSE file
in the root directory of this software component.
If no LICENSE file comes with this software, it is provided AS-IS.
