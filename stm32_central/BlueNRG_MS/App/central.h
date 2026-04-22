/**
  ******************************************************************************
  * @file    central.h
  * @brief   BLE Central state machine – public interface
  ******************************************************************************
  */

#ifndef CENTRAL_H
#define CENTRAL_H

#include <stdint.h>

/* BLE Central state machine states */
typedef enum {
  CENTRAL_STATE_IDLE = 0,
  CENTRAL_STATE_SCANNING,
  CENTRAL_STATE_CONNECTING,
  CENTRAL_STATE_DISC_SERVICES,
  CENTRAL_STATE_DISC_CHARS,
  CENTRAL_STATE_ENABLE_NOTIF,
  CENTRAL_STATE_CONNECTED,
  CENTRAL_STATE_HALTED,
} CentralState_t;

/* Public API ----------------------------------------------------------------*/
void Central_Init(void);
void Central_Process(void);
void Central_EventHandler(void *pData);

#endif /* CENTRAL_H */
