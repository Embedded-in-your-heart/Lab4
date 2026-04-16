/**
  ******************************************************************************
  * @file    App/sensor.c
  * @author  SRA Application Team
  * @brief   Sensor init and sensor state machines
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sensor.h"
#include "gatt_db.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "hci_le.h"
#include "hci_const.h"
#include "bluenrg_aci_const.h"
#include "bluenrg_gatt_aci.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define  ADV_INTERVAL_MIN_MS  1000
#define  ADV_INTERVAL_MAX_MS  1200

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
extern uint8_t bdaddr[BDADDR_SIZE];
extern uint8_t bnrg_expansion_board;
__IO uint8_t set_connectable = 1;
__IO uint16_t connection_handle = 0;
__IO uint8_t  notification_enabled = FALSE;
__IO uint32_t connected = FALSE;

extern uint16_t EnvironmentalCharHandle;
extern uint16_t AccGyroMagCharHandle;
extern uint16_t AccSamplingFreqCharHandle;

/* Sampling period in milliseconds (default 1 Hz). Written by the BLE client. */
volatile uint32_t sampling_period_ms = 1000;

volatile uint8_t request_free_fall_notify = FALSE;

AxesRaw_t x_axes = {0, 0, 0};
AxesRaw_t g_axes = {0, 0, 0};
AxesRaw_t m_axes = {0, 0, 0};
AxesRaw_t q_axes[SEND_N_QUATERNIONS] = {{0, 0, 0}};

/* Private function prototypes -----------------------------------------------*/
void GAP_DisconnectionComplete_CB(void);
void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle);

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * Function Name  : Set_DeviceConnectable.
 * Description    : Puts the device in connectable mode.
 * Input          : None.
 * Output         : None.
 * Return         : None.
 *******************************************************************************/
void Set_DeviceConnectable(void)
{
  uint8_t ret;
  const char local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME,SENSOR_DEMO_NAME};

  uint8_t manuf_data[26] = {
    2,0x0A,0x00, /* 0 dBm */  // Transmission Power
    8,0x09,SENSOR_DEMO_NAME,  // Complete Name
    13,0xFF,0x01, /* SKD version */
    0x80,
    0x00,
    0xF4, /* ACC+Gyro+Mag 0xE0 | 0x04 Temp | 0x10 Pressure */
    0x00, /*  */
    0x00, /*  */
    bdaddr[5], /* BLE MAC start -MSB first- */
    bdaddr[4],
    bdaddr[3],
    bdaddr[2],
    bdaddr[1],
    bdaddr[0]  /* BLE MAC stop */
  };

  manuf_data[18] |= 0x01; /* Sensor Fusion */

  hci_le_set_scan_resp_data(0, NULL);

  PRINTF("Set General Discoverable Mode.\n");

  ret = aci_gap_set_discoverable(ADV_DATA_TYPE,
                                (ADV_INTERVAL_MIN_MS*1000)/625,(ADV_INTERVAL_MAX_MS*1000)/625,
                                 STATIC_RANDOM_ADDR, NO_WHITE_LIST_USE,
                                 sizeof(local_name), local_name, 0, NULL, 0, 0);

  aci_gap_update_adv_data(26, manuf_data);

  if(ret != BLE_STATUS_SUCCESS)
  {
    PRINTF("aci_gap_set_discoverable() failed: 0x%02x\r\n", ret);
  }
  else
    PRINTF("aci_gap_set_discoverable() --> SUCCESS\r\n");
}

/**
 * @brief  Callback processing the ACI events.
 * @note   Inside this function each event must be identified and correctly
 *         parsed.
 * @param  void* Pointer to the ACI packet
 * @retval None
 */
void user_notify(void * pData)
{
  hci_uart_pckt *hci_pckt = pData;
  /* obtain event packet */
  hci_event_pckt *event_pckt = (hci_event_pckt*)hci_pckt->data;

  if(hci_pckt->type != HCI_EVENT_PKT)
    return;

  switch(event_pckt->evt){

  case EVT_DISCONN_COMPLETE:
    {
      GAP_DisconnectionComplete_CB();
    }
    break;

  case EVT_LE_META_EVENT:
    {
      evt_le_meta_event *evt = (void *)event_pckt->data;

      switch(evt->subevent){
      case EVT_LE_CONN_COMPLETE:
        {
          evt_le_connection_complete *cc = (void *)evt->data;
          GAP_ConnectionComplete_CB(cc->peer_bdaddr, cc->handle);
        }
        break;
      }
    }
    break;

  case EVT_VENDOR:
    {
      evt_blue_aci *blue_evt = (void*)event_pckt->data;
      switch(blue_evt->ecode){

      case EVT_BLUE_GATT_READ_PERMIT_REQ:
        {
          evt_gatt_read_permit_req *pr = (void*)blue_evt->data;
          Read_Request_CB(pr->attr_handle);
        }
        break;

      case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED:
        {
          /* Handle differs between IDB04A1 and IDB05A1 (offset field presence) */
          uint16_t attr_handle;
          uint8_t  data_length;
          uint8_t *att_data;

          if (bnrg_expansion_board == IDB05A1)
          {
            evt_gatt_attr_modified_IDB05A1 *evt = (void*)blue_evt->data;
            attr_handle = evt->attr_handle;
            data_length = evt->data_length;
            att_data    = evt->att_data;
          }
          else
          {
            evt_gatt_attr_modified_IDB04A1 *evt = (void*)blue_evt->data;
            attr_handle = evt->attr_handle;
            data_length = evt->data_length;
            att_data    = evt->att_data;
          }

          /* Client wrote a new sampling frequency to characteristic_b */
          if (attr_handle == AccSamplingFreqCharHandle + 1 && data_length >= 2)
          {
            uint16_t freq_hz = (uint16_t)att_data[0] | ((uint16_t)att_data[1] << 8);
            if (freq_hz >= 1 && freq_hz <= 100)
            {
              sampling_period_ms = 1000U / freq_hz;
              PRINTF("Sampling frequency updated: %u Hz (%lu ms)\n",
                     (unsigned)freq_hz, (unsigned long)sampling_period_ms);
            }
            else
            {
              PRINTF("Rejected invalid frequency: %u Hz (valid: 1-100)\n", (unsigned)freq_hz);
            }
          }
        }
        break;
      }

    }
    break;
  }
}

/**
 * @brief  This function is called when the peer device gets disconnected.
 * @param  None
 * @retval None
 */
void GAP_DisconnectionComplete_CB(void)
{
  connected = FALSE;
  PRINTF("Disconnected\n");
  /* Make the device connectable again. */
  set_connectable = TRUE;
  notification_enabled = FALSE;
}

/**
 * @brief  This function is called when there is a LE Connection Complete event.
 * @param  uint8_t Address of peer device
 * @param  uint16_t Connection handle
 * @retval None
 */
void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle)
{
  connected = TRUE;
  connection_handle = handle;

  PRINTF("Connected to device:");
  for(uint32_t i = 5; i > 0; i--){
    PRINTF("%02X-", addr[i]);
  }
  PRINTF("%02X\n", addr[0]);
}
