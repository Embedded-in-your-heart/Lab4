/**
  ******************************************************************************
  * @file    app_bluenrg_ms.c
  * @brief   BlueNRG-MS Central initialization and process loop
  *
  * Initialises the BlueNRG-MS chip in GAP Central role, then delegates all
  * BLE event handling and the connection state machine to central.c.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_bluenrg_ms.h"
#include "central.h"

#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "bluenrg_utils.h"
#include "b_l475e_iot01a1.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "sm.h"

/* Private defines -----------------------------------------------------------*/
#define IDB04A1   0
#define IDB05A1   1
#define BDADDR_SIZE 6

/* Private variables ---------------------------------------------------------*/
uint8_t bnrg_expansion_board = IDB04A1;
uint8_t bdaddr[BDADDR_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void User_Init(void);

/* ---------------------------------------------------------------------------*/

void MX_BlueNRG_MS_Init(void)
{
  uint8_t  bdaddr_len_out;
  uint8_t  hwVersion;
  uint16_t fwVersion;
  uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
  int      ret;

  User_Init();

  hci_init(Central_EventHandler, NULL);

  getBlueNRGVersion(&hwVersion, &fwVersion);

  hci_reset();
  HAL_Delay(100);

  PRINTF("HWver %d\r\nFWver %d\r\n", hwVersion, fwVersion);
  if (hwVersion > 0x30) {
    bnrg_expansion_board = IDB05A1;
  }

  ret = aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS, BDADDR_SIZE, &bdaddr_len_out, bdaddr);
  if (ret) {
    PRINTF("Read Static Random address failed.\r\n");
  }
  if ((bdaddr[5] & 0xC0) != 0xC0) {
    PRINTF("Static Random address not well formed.\r\n");
    while (1);
  }

  ret = aci_gatt_init();
  if (ret) {
    PRINTF("GATT_Init failed.\r\n");
  }

  /* Init as GAP Central (no GATT server) */
  if (bnrg_expansion_board == IDB05A1) {
    ret = aci_gap_init_IDB05A1(GAP_CENTRAL_ROLE_IDB05A1, 0, 0x07,
                               &service_handle, &dev_name_char_handle, &appearance_char_handle);
  } else {
    ret = aci_gap_init_IDB04A1(GAP_CENTRAL_ROLE_IDB04A1,
                               &service_handle, &dev_name_char_handle, &appearance_char_handle);
  }
  if (ret != BLE_STATUS_SUCCESS) {
    PRINTF("GAP_Init failed: 0x%02x\r\n", ret);
  }

  aci_hal_set_tx_power_level(1, 4);

  PRINTF("BLE Central initialised.\r\n");

  Central_Init();
}

void MX_BlueNRG_MS_Process(void)
{
  Central_Process();
  hci_user_evt_proc();
}

static void User_Init(void)
{
  BSP_LED_Init(LED2);
  BSP_COM_Init(COM1);
}
