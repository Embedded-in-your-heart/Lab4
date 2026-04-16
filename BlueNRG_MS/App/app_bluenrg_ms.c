/**
  ******************************************************************************
  * @file    app_bluenrg_ms.c
  * @author  SRA Application Team
  * @brief   BlueNRG-M0 initialization and applicative code
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_bluenrg_ms.h"

#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "link_layer.h"
#include "sensor.h"
#include "gatt_db.h"

#include "compiler.h"
#include "bluenrg_utils.h"
#include "b_l475e_iot01a1.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "sm.h"
#include "stm32l4xx_hal_tim.h"

#include "custom_motion_sensors.h"
#include "lsm6dsl.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private defines -----------------------------------------------------------*/
/**
 * 1 to send environmental and motion data when pushing the user button
 * 0 to send environmental and motion data automatically (period = 1 sec)
 */
#define USE_BUTTON 0

/* Private macros ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
extern AxesRaw_t x_axes;
extern AxesRaw_t g_axes;
extern AxesRaw_t m_axes;
extern AxesRaw_t q_axes;

extern volatile uint8_t set_connectable;
extern volatile int     connected;
extern volatile uint32_t sampling_period_ms;
/* at startup, suppose the X-NUCLEO-IDB04A1 is used */
uint8_t bnrg_expansion_board = IDB04A1;
uint8_t bdaddr[BDADDR_SIZE];
static volatile uint8_t user_button_init_state = 1;
static volatile uint8_t user_button_pressed = 0;

/* Tick of the last sensor sample/notify cycle */
static uint32_t last_sample_tick = 0;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void User_Process(void);
static void User_Init(void);
static void Read_Sensor_Data(void);
static float Read_LSM6DSL_Temperature(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

#if PRINT_CSV_FORMAT
extern volatile uint32_t ms_counter;
/**
 * @brief  This function is a utility to print the log time
 *         in the format HH:MM:SS:MSS (DK GUI time format)
 * @param  None
 * @retval None
 */
void print_csv_time(void){
  uint32_t ms = HAL_GetTick();
  PRINT_CSV("%02ld:%02ld:%02ld.%03ld", (long)(ms/(60*60*1000)%24), (long)(ms/(60*1000)%60), (long)((ms/1000)%60), (long)(ms%1000));
}
#endif

void MX_BlueNRG_MS_Init(void)
{
  /* USER CODE BEGIN SV */

  /* USER CODE END SV */

  /* USER CODE BEGIN BlueNRG_MS_Init_PreTreatment */

  /* USER CODE END BlueNRG_MS_Init_PreTreatment */

  /* Initialize the peripherals and the BLE Stack */
  const char *name = "BlueNRG";
  uint16_t service_handle, dev_name_char_handle, appearance_char_handle;

  uint8_t  bdaddr_len_out;
  uint8_t  hwVersion;
  uint16_t fwVersion;
  int ret;

  User_Init();

  /* Get the User Button initial state */
  user_button_init_state = BSP_PB_GetState(BUTTON_KEY);

  hci_init(user_notify, NULL);

  /* get the BlueNRG HW and FW versions */
  getBlueNRGVersion(&hwVersion, &fwVersion);

  /*
   * Reset BlueNRG again otherwise we won't
   * be able to change its MAC address.
   * aci_hal_write_config_data() must be the first
   * command after reset otherwise it will fail.
   */
  hci_reset();
  HAL_Delay(100);

  PRINTF("HWver %d\nFWver %d\n", hwVersion, fwVersion);
  if (hwVersion > 0x30) { /* X-NUCLEO-IDB05A1 expansion board is used */
    bnrg_expansion_board = IDB05A1;
  }

  ret = aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS, BDADDR_SIZE, &bdaddr_len_out, bdaddr);

  if (ret) {
    PRINTF("Read Static Random address failed.\n");
  }

  if ((bdaddr[5] & 0xC0) != 0xC0) {
    PRINTF("Static Random address not well formed.\n");
    while(1);
  }

  /* GATT Init */
  ret = aci_gatt_init();
  if(ret){
    PRINTF("GATT_Init failed.\n");
  }

  /* GAP Init */
  if (bnrg_expansion_board == IDB05A1) {
    ret = aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 0x07, &service_handle, &dev_name_char_handle, &appearance_char_handle);
  }
  else {
    ret = aci_gap_init_IDB04A1(GAP_PERIPHERAL_ROLE_IDB04A1, &service_handle, &dev_name_char_handle, &appearance_char_handle);
  }
  if (ret != BLE_STATUS_SUCCESS) {
    PRINTF("GAP_Init failed.\n");
  }

  /* Update device name */
  ret = aci_gatt_update_char_value(service_handle, dev_name_char_handle, 0,
                                   strlen(name), (uint8_t *)name);
  if (ret) {
    PRINTF("aci_gatt_update_char_value failed.\n");
    while(1);
  }

  ret = aci_gap_set_auth_requirement(MITM_PROTECTION_REQUIRED,
                                     OOB_AUTH_DATA_ABSENT,
                                     NULL,
                                     7,
                                     16,
                                     USE_FIXED_PIN_FOR_PAIRING,
                                     123456,
                                     BONDING);
  if (ret) {
    PRINTF("aci_gap_set_authentication_requirement failed.\n");
    while(1);
  }

  PRINTF("BLE Stack Initialized\n");

  ret = Add_HWServW2ST_Service();
  if(ret == BLE_STATUS_SUCCESS) {
    PRINTF("BlueMS HW service added successfully.\n");
  } else {
    PRINTF("Error while adding BlueMS HW service: 0x%02x\r\n", ret);
    while(1);
  }

  ret = Add_SWServW2ST_Service();
  if(ret == BLE_STATUS_SUCCESS) {
     PRINTF("BlueMS SW service added successfully.\n");
  } else {
     PRINTF("Error while adding BlueMS HW service: 0x%02x\r\n", ret);
     while(1);
  }

  /* Set output power level */
  ret = aci_hal_set_tx_power_level(1,4);

  /* USER CODE BEGIN BlueNRG_MS_Init_PostTreatment */

  /* USER CODE END BlueNRG_MS_Init_PostTreatment */
}

/*
 * BlueNRG-MS background task
 */
void MX_BlueNRG_MS_Process(void)
{
  /* USER CODE BEGIN BlueNRG_MS_Process_PreTreatment */

  /* USER CODE END BlueNRG_MS_Process_PreTreatment */

  User_Process();
  hci_user_evt_proc();

  /* USER CODE BEGIN BlueNRG_MS_Process_PostTreatment */

  /* USER CODE END BlueNRG_MS_Process_PostTreatment */
}

/**
 * @brief  Initialize User process.
 *
 * @param  None
 * @retval None
 */
static void User_Init(void)
{
  BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_EXTI);
  BSP_LED_Init(LED2);

  BSP_COM_Init(COM1);

  /* Initialize LSM6DSL accelerometer and gyroscope */
  if (CUSTOM_MOTION_SENSOR_Init(CUSTOM_LSM6DSL_0, MOTION_ACCELERO | MOTION_GYRO) != BSP_ERROR_NONE)
  {
    PRINTF("LSM6DSL sensor init failed!\n");
  }
  else
  {
    PRINTF("LSM6DSL sensor initialized (Accel + Gyro)\n");
  }
}

/**
 * @brief  Process user input (i.e. pressing the USER button on Nucleo board)
 *         and send the updated acceleration data to the remote client.
 *
 * @param  None
 * @retval None
 */
static void User_Process(void)
{
  if (set_connectable)
  {
    Set_DeviceConnectable();
    set_connectable = FALSE;
  }

#if USE_BUTTON
  if (user_button_pressed)
  {
    HAL_Delay(50);
    while (BSP_PB_GetState(BUTTON_KEY) == !user_button_init_state);
    HAL_Delay(50);

    if (connected)
    {
      BSP_LED_Toggle(LED2);
      Read_Sensor_Data();
      float temp = Read_LSM6DSL_Temperature();
      int32_t press = 100000;
      BlueMS_Environmental_Update(press, (int16_t)(temp * 10));
      Acc_Update(&x_axes, &g_axes, &m_axes);
      q_axes.AXIS_X = x_axes.AXIS_X;
      q_axes.AXIS_Y = x_axes.AXIS_Y;
      q_axes.AXIS_Z = x_axes.AXIS_Z;
      Quat_Update(&q_axes);
    }

    user_button_pressed = 0;
  }
#else
  /* Non-blocking tick-based sampling: send data at the rate set by the BLE client */
  if (connected)
  {
    uint32_t now = HAL_GetTick();
    if (now - last_sample_tick >= sampling_period_ms)
    {
      last_sample_tick = now;

      BSP_LED_Toggle(LED2);

      Read_Sensor_Data();

      float temp = Read_LSM6DSL_Temperature();
      int32_t press = 100000; /* No LPS22HB driver: fixed 1000.00 hPa placeholder */
      BlueMS_Environmental_Update(press, (int16_t)(temp * 10));

      Acc_Update(&x_axes, &g_axes, &m_axes);

      q_axes.AXIS_X = x_axes.AXIS_X;
      q_axes.AXIS_Y = x_axes.AXIS_Y;
      q_axes.AXIS_Z = x_axes.AXIS_Z;
      Quat_Update(&q_axes);
    }
  }
#endif
}

/**
 * @brief  Read real motion sensor data from LSM6DSL
 * @retval None
 */
static void Read_Sensor_Data(void)
{
  CUSTOM_MOTION_SENSOR_Axes_t acc_axes;
  CUSTOM_MOTION_SENSOR_Axes_t gyro_axes;

  /* Read accelerometer data (in mg) */
  if (CUSTOM_MOTION_SENSOR_GetAxes(CUSTOM_LSM6DSL_0, MOTION_ACCELERO, &acc_axes) == BSP_ERROR_NONE)
  {
    x_axes.AXIS_X = acc_axes.x;
    x_axes.AXIS_Y = acc_axes.y;
    x_axes.AXIS_Z = acc_axes.z;
  }

  /* Read gyroscope data (in mdps) */
  if (CUSTOM_MOTION_SENSOR_GetAxes(CUSTOM_LSM6DSL_0, MOTION_GYRO, &gyro_axes) == BSP_ERROR_NONE)
  {
    g_axes.AXIS_X = gyro_axes.x;
    g_axes.AXIS_Y = gyro_axes.y;
    g_axes.AXIS_Z = gyro_axes.z;
  }

  /* No magnetometer driver available (LSM3MDL not included) */
  m_axes.AXIS_X = 0;
  m_axes.AXIS_Y = 0;
  m_axes.AXIS_Z = 0;
}

/**
 * @brief  Read temperature from LSM6DSL internal sensor
 * @retval Temperature in degrees Celsius
 */
static float Read_LSM6DSL_Temperature(void)
{
  extern void *MotionCompObj[];
  LSM6DSL_Object_t *pObj = (LSM6DSL_Object_t *)MotionCompObj[CUSTOM_LSM6DSL_0];
  int16_t raw_temp = 0;

  if (pObj != NULL)
  {
    lsm6dsl_temperature_raw_get(&(pObj->Ctx), &raw_temp);
  }

  /* LSM6DSL temperature: 256 LSB/degC, offset = 25 degC */
  return 25.0f + ((float)raw_temp / 256.0f);
}

/**
  * @brief  BSP Push Button callback
  * @param  Button Specifies the pin connected EXTI line
  * @retval None.
  */
void BSP_PB_Callback(Button_TypeDef Button)
{
  /* Set the User Button flag */
  user_button_pressed = 1;
}
