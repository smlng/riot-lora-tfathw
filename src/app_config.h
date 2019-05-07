#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "net/loramac.h"

/* LoRaWAN application configurations */
#define APP_LORAWAN_BUF_SIZE        (64U)
#define APP_LORAWAN_TX_PORT         (1U)
#define APP_LORAWAN_DATARATE        (LORAMAC_DR_2)
#define APP_LORAWAN_JOIN_RETRY_S    (23U)
/* we must respect the duty cycle limitations */
#define APP_SLEEP_S                 (37U)
#define APP_ALIVE_S                 (10U)
#define APP_REBOOT_S                (3600U)

#endif
