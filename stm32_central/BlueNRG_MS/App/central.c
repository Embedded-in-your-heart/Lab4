/**
  ******************************************************************************
  * @file    central.c
  * @brief   BLE GAP Central state machine, event handlers, and UART command
  *          parser for B-L475E-IOT01A acting as BLE central.
  *
  * State machine:
  *   IDLE → SCANNING → CONNECTING → DISC_SERVICES → DISC_CHARS
  *        → ENABLE_NOTIF → CONNECTED
  *
  * On connection: subscribes to AccGyroMag notifications and prints IMU data.
  * UART input "freq N" writes sampling frequency to characteristic_b.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "central.h"

#include "hci.h"
#include "hci_le.h"
#include "hci_const.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_l2cap_aci.h"
#include "bluenrg_aci_const.h"
#include "b_l475e_iot01a1.h"
#include "stm32l4xx_hal.h"

/* ---------------------------------------------------------------------------
 * External symbols from app_bluenrg_ms.c
 * ---------------------------------------------------------------------------*/
#define IDB04A1  0
#define IDB05A1  1
extern uint8_t bnrg_expansion_board;
extern uint8_t bdaddr[6];

/* UART handle – huart1 = USART1, routed to ST-LINK VCP (defined by BSP) */

/* ---------------------------------------------------------------------------
 * UUID byte arrays (128-bit, little-endian as sent by ATT layer)
 * ---------------------------------------------------------------------------*/

/* HW Service: 00000000-0001-11e1-9ab4-0002a5d5c51b */
static const uint8_t HW_SERVICE_UUID_LE[16] = {
  0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0xb4, 0x9a,
  0xe1, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* AccGyroMag char: 00E00000-0001-11e1-ac36-0002a5d5c51b */
static const uint8_t ACC_GYRO_MAG_UUID_LE[16] = {
  0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0x36, 0xac,
  0xe1, 0x11, 0x01, 0x00, 0x00, 0x00, 0xe0, 0x00
};

/* AccSamplingFreq char: 00C00000-0001-11e1-ac36-0002a5d5c51b */
static const uint8_t ACC_FREQ_UUID_LE[16] = {
  0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0x36, 0xac,
  0xe1, 0x11, 0x01, 0x00, 0x00, 0x00, 0xc0, 0x00
};

/* ---------------------------------------------------------------------------
 * State machine variables
 * ---------------------------------------------------------------------------*/
static CentralState_t state = CENTRAL_STATE_IDLE;
static uint16_t conn_handle         = 0;
static uint16_t hw_start_handle     = 0;
static uint16_t hw_end_handle       = 0;
static uint16_t acc_gyro_val_handle = 0;
static uint16_t freq_val_handle     = 0;
static uint8_t  peer_addr[6]        = {0};
static uint8_t  peer_addr_type      = 0;
/* Tick at which we may start GATT discovery (allows post-connect LL settling). */
static uint32_t disc_ready_tick     = 0;

/* ---------------------------------------------------------------------------
 * UART receive buffer (interrupt-driven, single byte at a time)
 * ---------------------------------------------------------------------------*/
#define UART_LINE_MAX 64
static uint8_t rx_byte;
static char    uart_line_buf[UART_LINE_MAX];
static uint8_t uart_line_len = 0;

/* ---------------------------------------------------------------------------
 * Private helpers
 * ---------------------------------------------------------------------------*/

static void start_scan(void)
{
  uint8_t ret;
  PRINTF("[SCAN] Scanning for \"BlueNRG\" ...\r\n");
  ret = aci_gap_start_general_discovery_proc(
          0x0060,              /* scan interval: 60 ms (96 * 0.625 ms) */
          0x0030,              /* scan window:   30 ms (48 * 0.625 ms) */
          STATIC_RANDOM_ADDR,  /* own address type (matches bdaddr loaded from config) */
          0x01);               /* filter duplicates */
  if (ret != BLE_STATUS_SUCCESS) {
    PRINTF("[SCAN] start_general_discovery_proc failed: 0x%02x (retry 1s later)\r\n", ret);
    HAL_Delay(1000);
    return;
  }
  state = CENTRAL_STATE_SCANNING;
}

/**
 * Scan AD structures for Complete/Shortened Local Name (0x09/0x08)
 * and copy into out_name (NUL-terminated, bounded by out_size).
 * Returns the name length written, or 0 if no name field present.
 */
static uint8_t ad_get_name(const uint8_t *ad_data, uint8_t ad_len,
                           char *out_name, uint8_t out_size)
{
  uint8_t i = 0;
  out_name[0] = '\0';
  while (i < ad_len) {
    uint8_t len = ad_data[i];
    if (len == 0 || i + len >= ad_len) break;
    uint8_t type = ad_data[i + 1];
    if (type == 0x09 || type == 0x08) {
      uint8_t n = len - 1;
      if (n > out_size - 1) n = out_size - 1;
      memcpy(out_name, &ad_data[i + 2], n);
      out_name[n] = '\0';
      return n;
    }
    i += 1 + len;
  }
  return 0;
}

/**
 * Parse raw AccGyroMag notification data (20 bytes) and print to UART.
 * Format: [ts:2][ax:2][ay:2][az:2][gx:2][gy:2][gz:2][mx:2][my:2][mz:2]
 * All int16 LE. acc in mg, gyro in mdps.
 */
static void print_imu(const uint8_t *data, uint8_t len)
{
  if (len < 20) return;
  int16_t ax = (int16_t)(data[2]  | ((uint16_t)data[3]  << 8));
  int16_t ay = (int16_t)(data[4]  | ((uint16_t)data[5]  << 8));
  int16_t az = (int16_t)(data[6]  | ((uint16_t)data[7]  << 8));
  int16_t gx = (int16_t)(data[8]  | ((uint16_t)data[9]  << 8));
  int16_t gy = (int16_t)(data[10] | ((uint16_t)data[11] << 8));
  int16_t gz = (int16_t)(data[12] | ((uint16_t)data[13] << 8));
  PRINTF("[IMU]  Acc=(%6d, %6d, %6d) mg  Gyro=(%7d, %7d, %7d) mdps\r\n",
         (int)ax, (int)ay, (int)az,
         (int)gx, (int)gy, (int)gz);
}

/**
 * Handle a complete UART line. Supported commands:
 *   freq N   — write N Hz to AccSamplingFreq (1-100)
 */
static void parse_command(const char *line)
{
  unsigned int freq = 0;
  if (sscanf(line, "freq %u", &freq) == 1) {
    if (freq < 1 || freq > 100) {
      PRINTF("[FREQ] ERR: value out of range (1-100)\r\n");
      return;
    }
    if (state != CENTRAL_STATE_CONNECTED || freq_val_handle == 0) {
      PRINTF("[FREQ] ERR: not connected or characteristic not discovered\r\n");
      return;
    }
    uint8_t data[2] = { (uint8_t)(freq & 0xFF), (uint8_t)((freq >> 8) & 0xFF) };
    uint8_t ret = aci_gatt_write_without_response(conn_handle, freq_val_handle, 2, data);
    if (ret == BLE_STATUS_SUCCESS) {
      PRINTF("[FREQ] Writing %u Hz to characteristic_b ...\r\n", freq);
    } else {
      PRINTF("[FREQ] ERR: write failed (0x%02x)\r\n", ret);
    }
  }
}

/* ---------------------------------------------------------------------------
 * Event handlers (called from Central_EventHandler per state)
 * ---------------------------------------------------------------------------*/

static void handle_adv_report(uint8_t bdaddr_type, const uint8_t bdaddr[6],
                              const uint8_t *ad_rssi, uint8_t data_length)
{
  char name[32];
  ad_get_name(ad_rssi, data_length, name, sizeof(name));

  int8_t rssi = (int8_t)ad_rssi[data_length];
  PRINTF("[SCAN] dev %02X:%02X:%02X:%02X:%02X:%02X rssi=%d name=\"%s\"\r\n",
         bdaddr[5], bdaddr[4], bdaddr[3],
         bdaddr[2], bdaddr[1], bdaddr[0], (int)rssi, name);

  if (strcmp(name, "BlueNRG") == 0) {
    memcpy(peer_addr, bdaddr, 6);
    peer_addr_type = bdaddr_type;
    PRINTF("[CONN] Found BlueNRG at %02X:%02X:%02X:%02X:%02X:%02X type=%d\r\n",
           peer_addr[5], peer_addr[4], peer_addr[3],
           peer_addr[2], peer_addr[1], peer_addr[0], peer_addr_type);
    /* Stop the discovery procedure before connecting; aci_gap_create_connection
     * cannot be issued while GAP_GENERAL_DISCOVERY_PROC is still active (error 0x0c).
     * The actual connection is initiated from the EVT_BLUE_GAP_PROCEDURE_COMPLETE
     * handler once the scan has cleanly stopped. */
    state = CENTRAL_STATE_CONNECTING;
    uint8_t ret = aci_gap_terminate_gap_procedure(GAP_GENERAL_DISCOVERY_PROC);
    if (ret != BLE_STATUS_SUCCESS) {
      PRINTF("[CONN] aci_gap_terminate_gap_procedure failed: 0x%02x\r\n", ret);
      state = CENTRAL_STATE_SCANNING;
    }
  }
}

static void on_conn_complete(evt_le_connection_complete *cc)
{
  if (cc->status != BLE_STATUS_SUCCESS) {
    PRINTF("[CONN] Connection failed (status=0x%02x). Halting.\r\n", cc->status);
    state = CENTRAL_STATE_HALTED;
    return;
  }
  conn_handle = cc->handle;
  PRINTF("[CONN] Connected to %02X:%02X:%02X:%02X:%02X:%02X interval=%u latency=%u supv_timeout=%u\r\n",
         cc->peer_bdaddr[5], cc->peer_bdaddr[4], cc->peer_bdaddr[3],
         cc->peer_bdaddr[2], cc->peer_bdaddr[1], cc->peer_bdaddr[0],
         cc->interval, cc->latency, cc->supervision_timeout);

  /* Reset discovery state */
  hw_start_handle   = 0;
  hw_end_handle     = 0;
  acc_gyro_val_handle = 0;
  freq_val_handle   = 0;

  /* Delay GATT discovery by 500 ms to let post-connection LL procedures
   * (LL_VERSION_REQ, L2CAP parameter update, etc.) finish before we send
   * the first ATT request. Discovery is triggered from Central_Process. */
  disc_ready_tick = HAL_GetTick() + 500;
  state = CENTRAL_STATE_DISC_SERVICES;
}

static void on_disconn_complete(void)
{
  PRINTF("[CONN] Disconnected. Halting.\r\n");
  conn_handle         = 0;
  acc_gyro_val_handle = 0;
  freq_val_handle     = 0;
  state = CENTRAL_STATE_HALTED;
}

/**
 * Process one service entry from EVT_BLUE_ATT_READ_BY_GROUP_TYPE_RESP.
 * entry layout: [start:2][end:2][UUID:16] (for 128-bit UUID services).
 */
static void on_service_entry(const uint8_t *entry, uint8_t attr_data_len)
{
  if (attr_data_len < 20) return;  /* need 4B handles + 16B UUID */
  uint16_t svc_start = (uint16_t)(entry[0] | ((uint16_t)entry[1] << 8));
  uint16_t svc_end   = (uint16_t)(entry[2] | ((uint16_t)entry[3] << 8));
  const uint8_t *uuid = &entry[4];
  if (memcmp(uuid, HW_SERVICE_UUID_LE, 16) == 0) {
    hw_start_handle = svc_start;
    hw_end_handle   = svc_end;
    PRINTF("[DISC] HW Service found: handles 0x%04X-0x%04X\r\n", svc_start, svc_end);
  }
}

/**
 * Process one characteristic entry from EVT_BLUE_ATT_READ_BY_TYPE_RESP.
 * pair layout: [decl_handle:2][props:1][value_handle:2][UUID:16] (128-bit).
 */
static void on_char_entry(const uint8_t *pair, uint8_t pair_len)
{
  if (pair_len < 21) return;  /* 2+1+2+16 */
  uint16_t val_handle = (uint16_t)(pair[3] | ((uint16_t)pair[4] << 8));
  const uint8_t *uuid = &pair[5];
  if (memcmp(uuid, ACC_GYRO_MAG_UUID_LE, 16) == 0) {
    acc_gyro_val_handle = val_handle;
    PRINTF("[DISC] AccGyroMag val_handle=0x%04X\r\n", val_handle);
  } else if (memcmp(uuid, ACC_FREQ_UUID_LE, 16) == 0) {
    freq_val_handle = val_handle;
    PRINTF("[DISC] AccSamplingFreq val_handle=0x%04X\r\n", val_handle);
  }
}

static void on_gatt_procedure_complete(evt_gatt_procedure_complete *pc)
{
  (void)pc;
  switch (state) {
    case CENTRAL_STATE_DISC_SERVICES: {
      if (hw_start_handle == 0) {
        PRINTF("[DISC] HW Service not found. Disconnecting ...\r\n");
        aci_gap_terminate(conn_handle, 0x13);
        state = CENTRAL_STATE_IDLE;
        break;
      }
      PRINTF("[DISC] HW Service found. Discovering characteristics ...\r\n");
      uint8_t ret = aci_gatt_disc_all_charac_of_serv(conn_handle,
                                                      hw_start_handle, hw_end_handle);
      if (ret != BLE_STATUS_SUCCESS) {
        PRINTF("[DISC] aci_gatt_disc_all_charac_of_serv failed: 0x%02x\r\n", ret);
        aci_gap_terminate(conn_handle, 0x13);
        state = CENTRAL_STATE_IDLE;
        break;
      }
      state = CENTRAL_STATE_DISC_CHARS;
      break;
    }
    case CENTRAL_STATE_DISC_CHARS: {
      if (acc_gyro_val_handle == 0) {
        PRINTF("[DISC] AccGyroMag not found. Disconnecting ...\r\n");
        aci_gap_terminate(conn_handle, 0x13);
        state = CENTRAL_STATE_IDLE;
        break;
      }
      PRINTF("[DISC] AccGyroMag=0x%04X  AccSamplingFreq=0x%04X\r\n",
             acc_gyro_val_handle, freq_val_handle);
      /* Enable notifications on AccGyroMag (write 0x0001 to CCCD at val_handle+1) */
      uint8_t cccd_val[2] = {0x01, 0x00};
      uint8_t ret = aci_gatt_write_charac_descriptor(conn_handle,
                                                      acc_gyro_val_handle + 1,
                                                      2, cccd_val);
      if (ret != BLE_STATUS_SUCCESS) {
        PRINTF("[DISC] Enable notify failed: 0x%02x\r\n", ret);
        aci_gap_terminate(conn_handle, 0x13);
        state = CENTRAL_STATE_IDLE;
        break;
      }
      state = CENTRAL_STATE_ENABLE_NOTIF;
      break;
    }
    case CENTRAL_STATE_ENABLE_NOTIF: {
      PRINTF("[CONN] Notifications enabled. Ready.\r\n");
      state = CENTRAL_STATE_CONNECTED;
      BSP_LED_On(LED2);
      break;
    }
    default:
      break;
  }
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

void Central_Init(void)
{
  state           = CENTRAL_STATE_IDLE;
  conn_handle     = 0;
  uart_line_len   = 0;
  memset(uart_line_buf, 0, sizeof(uart_line_buf));

  /* Arm UART RX interrupt – single byte at a time */
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

void Central_Process(void)
{
  if (state == CENTRAL_STATE_IDLE) {
    start_scan();
    return;
  }

  if (state == CENTRAL_STATE_DISC_SERVICES && disc_ready_tick != 0 &&
      HAL_GetTick() >= disc_ready_tick) {
    disc_ready_tick = 0;
    PRINTF("[DISC] Discovering primary services ...\r\n");
    uint8_t ret = aci_gatt_disc_all_prim_services(conn_handle);
    if (ret != BLE_STATUS_SUCCESS) {
      PRINTF("[DISC] aci_gatt_disc_all_prim_services failed: 0x%02x\r\n", ret);
      aci_gap_terminate(conn_handle, 0x13);
      state = CENTRAL_STATE_HALTED;
    }
  }
}

/**
 * BLE event handler – registered with hci_init(Central_EventHandler, NULL).
 * Called from hci_user_evt_proc() in the main loop.
 */
void Central_EventHandler(void *pData)
{
  hci_uart_pckt   *hci_pckt   = pData;
  hci_event_pckt  *event_pckt = (hci_event_pckt *)hci_pckt->data;

  if (hci_pckt->type != HCI_EVENT_PKT) return;

  switch (event_pckt->evt) {

    /* ------------------------------------------------------------------ */
    case EVT_DISCONN_COMPLETE: {
      evt_disconn_complete *dc = (void *)event_pckt->data;
      PRINTF("[CONN] EVT_DISCONN_COMPLETE status=0x%02x reason=0x%02x\r\n",
             dc->status, dc->reason);
      on_disconn_complete();
      break;
    }

    /* ------------------------------------------------------------------ */
    case EVT_LE_META_EVENT: {
      evt_le_meta_event *meta = (void *)event_pckt->data;
      if (meta->subevent == EVT_LE_CONN_COMPLETE) {
        evt_le_connection_complete *cc = (void *)meta->data;
        on_conn_complete(cc);
      } else if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
        /* meta->data: [num_reports:1] then num_reports × le_advertising_info
         *   [evt_type:1][bdaddr_type:1][bdaddr:6][data_length:1][ad_data+rssi] */
        if (state != CENTRAL_STATE_SCANNING) break;
        uint8_t num_reports = meta->data[0];
        const uint8_t *p = &meta->data[1];
        for (uint8_t i = 0; i < num_reports; i++) {
          uint8_t bdaddr_type = p[1];
          const uint8_t *bdaddr = &p[2];
          uint8_t data_length = p[8];
          const uint8_t *ad_rssi = &p[9];
          handle_adv_report(bdaddr_type, bdaddr, ad_rssi, data_length);
          p += 9 + data_length + 1;
        }
      }
      break;
    }

    /* ------------------------------------------------------------------ */
    case EVT_VENDOR: {
      evt_blue_aci *blue_evt = (void *)event_pckt->data;
      switch (blue_evt->ecode) {

        case EVT_BLUE_ATT_READ_BY_GROUP_TYPE_RESP: {
          if (state == CENTRAL_STATE_DISC_SERVICES) {
            evt_att_read_by_group_resp *resp = (void *)blue_evt->data;
            uint8_t attr_data_len = resp->attribute_data_length;
            uint8_t list_len = resp->event_data_length - 1; /* -1 for attr_data_len field */
            uint8_t n = (attr_data_len > 0) ? (list_len / attr_data_len) : 0;
            for (uint8_t i = 0; i < n; i++) {
              on_service_entry(&resp->attribute_data_list[i * attr_data_len], attr_data_len);
            }
          }
          break;
        }

        case EVT_BLUE_ATT_READ_BY_TYPE_RESP: {
          if (state == CENTRAL_STATE_DISC_CHARS) {
            evt_att_read_by_type_resp *resp = (void *)blue_evt->data;
            uint8_t pair_len  = resp->handle_value_pair_length;
            uint8_t list_len  = resp->event_data_length - 1; /* -1 for pair_len field */
            uint8_t n = (pair_len > 0) ? (list_len / pair_len) : 0;
            for (uint8_t i = 0; i < n; i++) {
              on_char_entry(&resp->handle_value_pair[i * pair_len], pair_len);
            }
          }
          break;
        }

        case EVT_BLUE_GATT_PROCEDURE_COMPLETE: {
          evt_gatt_procedure_complete *pc = (void *)blue_evt->data;
          on_gatt_procedure_complete(pc);
          break;
        }

        /* GAP general-discovery has a 10.24 s internal timeout; when it
         * fires we must relaunch the procedure, otherwise scanning dies.
         * Also fired when we call aci_gap_terminate_gap_procedure() to stop
         * scanning before initiating a connection. */
        case EVT_BLUE_GAP_PROCEDURE_COMPLETE: {
          evt_gap_procedure_complete *gpc = (void *)blue_evt->data;
          PRINTF("[GAP] procedure_complete code=0x%02x status=0x%02x\r\n",
                 gpc->procedure_code, gpc->status);

          if (gpc->procedure_code == GAP_GENERAL_DISCOVERY_PROC) {
            if (state == CENTRAL_STATE_CONNECTING) {
              /* Scan stopped intentionally; now start direct connection.
               * GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC (0x40) will fire
               * inside aci_gap_create_connection — ignore it to avoid a
               * recursive second call that corrupts the HCI state. */
              uint8_t ret = aci_gap_create_connection(
                              0x0060, 0x0030,
                              peer_addr_type, peer_addr,
                              STATIC_RANDOM_ADDR,
                              0x000C, 0x000C,
                              0,
                              0x00C8,
                              0, 0);
              if (ret != BLE_STATUS_SUCCESS && ret != BLE_STATUS_FAILED) {
                PRINTF("[CONN] aci_gap_create_connection failed: 0x%02x\r\n", ret);
                state = CENTRAL_STATE_IDLE;
              }
            } else if (state == CENTRAL_STATE_SCANNING) {
              PRINTF("[SCAN] procedure timeout, restarting ...\r\n");
              state = CENTRAL_STATE_IDLE;
            }
          }
          /* GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC (0x40) fires inside
           * aci_gap_create_connection; EVT_LE_CONN_COMPLETE carries the
           * real outcome — no action needed here. */
          break;
        }

        case EVT_BLUE_L2CAP_CONN_UPD_REQ: {
          evt_l2cap_conn_upd_req *lr = (void *)blue_evt->data;
          PRINTF("[L2CAP] conn_upd_req interval=[%u,%u] latency=%u timeout=%u\r\n",
                 lr->interval_min, lr->interval_max,
                 lr->slave_latency, lr->timeout_mult);
          /* Accept the peripheral's connection parameter update request. */
          if (bnrg_expansion_board == IDB05A1) {
            aci_l2cap_connection_parameter_update_response_IDB05A1(
              lr->conn_handle, lr->interval_min, lr->interval_max,
              lr->slave_latency, lr->timeout_mult, 0, 0,
              lr->identifier, 0x01);
          } else {
            aci_l2cap_connection_parameter_update_response_IDB04A1(
              lr->conn_handle, lr->interval_min, lr->interval_max,
              lr->slave_latency, lr->timeout_mult,
              lr->identifier, 0x01);
          }
          break;
        }

        case EVT_BLUE_GATT_NOTIFICATION: {
          if (state == CENTRAL_STATE_CONNECTED) {
            evt_gatt_attr_notification *notif = (void *)blue_evt->data;
            if (notif->attr_handle == acc_gyro_val_handle) {
              print_imu(notif->attr_value,
                        notif->event_data_length > 2
                          ? notif->event_data_length - 2  /* subtract handle field */
                          : 0);
            }
          }
          break;
        }

        default:
          break;
      }
      break;
    } /* EVT_VENDOR */

    default:
      break;
  }
}

/* ---------------------------------------------------------------------------
 * UART RX interrupt callback (override HAL weak symbol)
 * ---------------------------------------------------------------------------*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != huart1.Instance) return;

  char c = (char)rx_byte;
  if (c == '\r' || c == '\n') {
    if (uart_line_len > 0) {
      uart_line_buf[uart_line_len] = '\0';
      parse_command(uart_line_buf);
      uart_line_len = 0;
    }
  } else {
    if (uart_line_len < UART_LINE_MAX - 1) {
      uart_line_buf[uart_line_len++] = c;
    }
  }

  /* Re-arm for next byte */
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}
