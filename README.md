## 安裝步驟：

### 第一步：建立新專案 (Board Selector)
在 STM32CubeIDE 中，使用「開發板選擇器」可以自動幫你排好基礎引腳：
1.  **File** -> **New** -> **STM32 Project**。
2.  切換到 **Board Selector** 分頁。
3.  搜尋並選取 **B-L475E-IOT01A**，點擊 **Next**。
4.  專案名稱取好後，當系統問你 *"Initialize all peripherals with their default Mode?"*，建議選 **Yes**（這會自動幫你配好 UART4 和 I2C2）。

### 第二步：下載與管理擴充包 (Software Packages)
這是你提到的下載工具路徑，用來將感測器驅動抓進電腦：
1.  點擊頂部選單 **Help** -> **Manage Embedded Software Packages**。
2.  切換到 **STMicroelectronics** 分頁。
3.  搜尋 **MEMS1**，找到 **X-CUBE-MEMS1**。
4.  勾選最新版本並點擊 **Install Now**。
5.  搜尋 **MEMS1**，找到 **X-CUBE-BLE1**。
6.  勾選最新版本並點擊 **Install Now**。

### 第三步：配置 `.ioc` 檔案 (硬體與軟體關聯)
這一步最關鍵，要把下載好的 Packages 「拉進」這個專案裡：

#### 1. 啟用軟體包組件
* 點擊上方按鈕 **Software Packs** -> **Select Components**。
* 展開 **X-CUBE-MEMS1** -> **AccGyr**。
* 選取 **LSM6DSL** -> **I2C2**。
* 勾選 **Board Support Custom** -> **MOTION_SENSOR**。
* 點擊 **OK**。
剩下的設定看 [https://cool.ntu.edu.tw/courses/59774/files/9000550?module_item_id=2381201](https://cool.ntu.edu.tw/courses/59774/files/9000550?module_item_id=2381201)

#### 2. 設定感測器參數
* 在左側目錄出現的 **Software Packs** 下，點擊 **STMicroelectronics.X-CUBE-MEMS1...**。
* 在右側上方視窗中，將所有設定勾選。
* **關鍵位址設定**：在下方 Parameter Settings 中，確保 SA0 對應 GND。
* **關鍵位址設定**：在下方 Platform Settings 中，將 IPs or Components 改為 I2C:I2C， **LSM6DSL Bus IO Driver** 的 Found Solution 改為 **I2C2**。

#### 3. 檢查通訊引腳
* **I2C2**: 確保是 **PB10 (SCL)** 與 **PB11 (SDA)**。
* **UART4**: 確保是 **PA0 (TX)** 與 **PA1 (RX)**（用於 Serial 輸出）。

### 第四步：產生程式碼 (Generate Code)
1.  按下 `Ctrl + S` 或點擊齒輪圖示產生程式碼。
2.  這時 IDE 會自動在你的專案中生成 `Drivers/BSP/Components/lsm6dsl` 以及 `App/app_mems1.c` 等資料夾，**不需要手動複製檔案**。

### 第五步：開啟 printf 對浮點數的支援
1.  在專案名稱上點擊 **右鍵** -> **Properties** (屬性)。
2.  導航到 **C/C++ Build** -> **Settings**。
3.  在右側分頁找到 **Tool Settings**。
4.  找到 **MCU GCC Compiler** (或類似名稱) -> **Include paths**。
5.  點擊 **Apply and Close**，然後嘗試 **Clean Project** 再重新編譯。

### 測試用程式碼
最上層要 `#include "custom_motion_sensors.h"`
```c
/* USER CODE BEGIN 2 */
// 使用 X-CUBE-MEMS1 的通用 API
CUSTOM_MOTION_SENSOR_Init(CUSTOM_LSM6DSL_0, MOTION_ACCELERO);
CUSTOM_MOTION_SENSOR_Enable(CUSTOM_LSM6DSL_0, MOTION_ACCELERO);
/* USER CODE END 2 */

CUSTOM_MOTION_SENSOR_Axes_t accel;
while (1) {
    /* USER CODE BEGIN 3 */
    if (CUSTOM_MOTION_SENSOR_GetAxes(CUSTOM_LSM6DSL_0, MOTION_ACCELERO, &accel) == BSP_ERROR_NONE) {
        printf("X:%ld Y:%ld Z:%ld (mg)\r\n", accel.x, accel.y, accel.z);
    }
    HAL_Delay(200);
    /* USER CODE END 3 */
}
```

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

### <b>Author</b>

SRA Application Team

### <b>License</b>

Copyright (c) 2025 STMicroelectronics.
All rights reserved.

This software is licensed under terms that can be found in the LICENSE file
in the root directory of this software component.
If no LICENSE file comes with this software, it is provided AS-IS.
