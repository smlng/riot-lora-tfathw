#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "byteorder.h"
#include "fmt.h"
#include "periph/gpio.h"
#include "tfa_thw.h"
#include "tfa_thw_params.h"
#include "xtimer.h"

#include "net/loramac.h"
#include "semtech_loramac.h"

#include "lora-keys.m11.h"
#include "app_config.h"

#define ENABLE_DEBUG        (1)
#include "debug.h"

/**
 * @name LoRaWAN payload buffer parameters
 *
 * 32     24    20            8          0
 *  |XXXXX| RES | DEVID                  |
 *  | WINDSPEED | TEMPERATURE | HUMIDITY |
 *
 * @{
 */
#define BUF_DEVID_M         (0xFFFFF)   /**< Mask for devid, 20 Bit */
#define BUF_TEMPWIND_M      (0xFFF)     /**< Mask for temp/wind data, 12 Bit */
#define BUF_HUMIDITY_M      (0xFF)      /**< Mask for humidity data, 8 Bit */

typedef union tfa_thw_lorawan_buf {
    uint8_t  u8[8];                 /**< raw buffer */
    uint64_t u64;
    struct {
        uint32_t humidity    : 8;   /**< humidity in % */
        uint32_t temperature : 12;  /**< temperature+500 in C x 10.0 */
        uint32_t windspeed   : 12;  /**< windspeed in kph x 10.0 */
        uint32_t id          : 32;  /**< device id, randomly generated */
    };
} tfa_thw_lorawan_buf_t;
/** @} */

static semtech_loramac_t g_loramac;
static uint8_t buf[APP_LORAWAN_BUF_SIZE];

#define DATALEN     (2U)
static tfa_thw_t dev;
static tfa_thw_data_t data[DATALEN];

static kernel_pid_t kpid = KERNEL_PID_UNDEF;
static char ka_stack[THREAD_STACKSIZE_DEFAULT];
/* following pins are on CN1 in a row */
static gpio_t pins[] = { GPIO_PIN(0, 9), GPIO_PIN(1, 12), GPIO_PIN(1, 6), GPIO_PIN(1, 13), GPIO_PIN(1, 14), GPIO_PIN(1, 15) };

/* helper function to increase power demand when using a power pack with
 * dynamic shut off if energy usage is below certain threshold. Therefore
 * connect some resistores, e.g. 220Ohm to, some or all pins defined above
 * to increase power load. Also adapt sleep times as needed.
 */
void *_keep_alive(void *arg)
{
    (void) arg;
    DEBUG("%s: init power drain pins\n", __func__);
    for (unsigned i = 0; i < (sizeof(pins)/sizeof(gpio_t)); ++i) {
        gpio_init(pins[i], GPIO_OUT);
    }
    while(1) {
        DEBUG("%s: set power drain pins\n", __func__);
        for (unsigned i = 0; i < (sizeof(pins)/sizeof(gpio_t)); ++i) {
            gpio_set(pins[i]);
        }
        xtimer_sleep(1);
        DEBUG("%s: clear power drain pins\n", __func__);
        for (unsigned i = 0; i < (sizeof(pins)/sizeof(gpio_t)); ++i) {
            gpio_clear(pins[i]);
        }
        DEBUG("%s: wait until next round\n", __func__);
        xtimer_sleep(9);
    }
    return NULL;
}

static void _blink(bool fail)
{
    unsigned count = (fail) ? 10 : 4;
    for (unsigned i = 0; i < count; ++i) {
        LED1_TOGGLE;
        xtimer_usleep(US_PER_SEC / count);
    }
}

void lorawan_setup(semtech_loramac_t *loramac)
{
    DEBUG(". %s\n", __func__);
    /* init LoRaMAC */
    semtech_loramac_init(loramac);
    /* load device EUI */
    fmt_hex_bytes(buf, LORA_DEVEUI);
    semtech_loramac_set_deveui(loramac, buf);
    /* load application EUI */
    fmt_hex_bytes(buf, LORA_APPEUI);
    semtech_loramac_set_appeui(loramac, buf);
    /* load application key */
    fmt_hex_bytes(buf, LORA_APPKEY);
    semtech_loramac_set_appkey(loramac, buf);
    /* Try to join by Over The Air Activation */
    DEBUG(".. LoRaWAN join: ");
    LED1_ON;
    while (semtech_loramac_join(loramac, LORAMAC_JOIN_OTAA) !=
           SEMTECH_LORAMAC_JOIN_SUCCEEDED) {
        DEBUG("[FAIL]\n.. retry join:");
        _blink(true);
        xtimer_sleep(APP_LORAWAN_JOIN_RETRY_S);
        LED1_ON;
    }
    DEBUG("[DONE]\n");
    _blink(false);
    LED1_OFF;
}

void create_buf(uint32_t devid, uint16_t windspeed,
                uint16_t temperature, uint8_t humidity,
                tfa_thw_lorawan_buf_t *buf)
{
    DEBUG(". %s\n", __func__);
    /* reset buffer */
    memset(buf, 0, sizeof(tfa_thw_lorawan_buf_t));
    buf->id = devid;
    buf->windspeed = (windspeed & BUF_TEMPWIND_M);
    buf->temperature = (temperature & BUF_TEMPWIND_M);
    buf->humidity = (humidity & BUF_HUMIDITY_M);
}

void lorawan_send(semtech_loramac_t *loramac, uint8_t *buf, uint8_t len)
{
    DEBUG(". %s\n", __func__);

    semtech_loramac_set_tx_mode(loramac, LORAMAC_TX_UNCNF);
    semtech_loramac_set_tx_port(loramac, APP_LORAWAN_TX_PORT);
    /* set datarate */
    semtech_loramac_set_dr(loramac, APP_LORAWAN_DATARATE);
    /* try to send data */
    DEBUG(".. send: ");
    unsigned ret = semtech_loramac_send(loramac, buf, len);
    switch (ret) {
        case SEMTECH_LORAMAC_TX_OK:
            DEBUG("success\n");
            break;

        case SEMTECH_LORAMAC_NOT_JOINED:
            DEBUG("failed, not joined\n");
            break;

        case SEMTECH_LORAMAC_BUSY:
            DEBUG("failed, MAC busy\n");
            break;

        default:
            DEBUG("failed with %u\n", ret);
    }
    /* try to receive something (mandatory to unblock) */
    DEBUG(".. recv: ");
    ret = semtech_loramac_recv(loramac);
    /* check if something was received */
    switch (ret) {
        case SEMTECH_LORAMAC_DATA_RECEIVED:
            loramac->rx_data.payload[loramac->rx_data.payload_len] = 0;
            DEBUG("got data  [%s] on port %d\n",
                  (char *)loramac->rx_data.payload, loramac->rx_data.port);
            break;

        case SEMTECH_LORAMAC_TX_CNF_FAILED:
            DEBUG("confirmable TX failed!\n");
            break;

        case SEMTECH_LORAMAC_TX_DONE:
            DEBUG("TX complete, no data received\n");
            break;

        default:
            DEBUG("failed with %u\n", ret);
    }
}

int main(void)
{
    /* set LED0 on */
    LED0_OFF;
    LED1_OFF;
    LED2_OFF;
    LED3_OFF;
    DEBUG("create keep alive thread: ");
    kpid = thread_create(
            ka_stack, sizeof(ka_stack),
            THREAD_PRIORITY_MAIN - 1,
            THREAD_CREATE_WOUT_YIELD | THREAD_CREATE_STACKTEST,
            _keep_alive, NULL, "_keep_alive");
    DEBUG("[DONE]\n");
    /* Setup LoRa parameters and OTAA join */
    DEBUG("init network:\n");
    lorawan_setup(&g_loramac);

    DEBUG("init sensor: ");
    if (tfa_thw_init(&dev, &tfa_thw_params[0])) {
        DEBUG("[FAIL]\n");
        return 1;
    }
    DEBUG("[DONE]\n");

    while(1) {
        DEBUG("read data:\n");
        LED3_ON;
        if (tfa_thw_read(&dev, data, DATALEN) == 0) {
            if (data[0].id != data[1].id) {
                DEBUG("! id mismatch !\n");
            }
            else if (data[0].type == data[1].type) {
                DEBUG("! invalid data (1) !\n");
            }
            else if ((data[0].type + data[1].type) != 3) {
                DEBUG("! invalid data (2) !\n");
            }
            else {
                LED1_ON;
                tfa_thw_lorawan_buf_t tbuf;
                if (data[0].type == 1) { /* temperature and humidity in data[0] */
                    create_buf(data[0].id, data[1].tempwind, data[0].tempwind,
                               data[0].humidity, &tbuf);
                }
                else {
                    create_buf(data[0].id, data[0].tempwind, data[1].tempwind,
                               data[1].humidity, &tbuf);
                }
                lorawan_send(&g_loramac, tbuf.u8, sizeof(tbuf.u8));
                LED1_OFF;
            }
        }
        else{
            DEBUG("! ERROR !\n");
        }
        LED3_OFF;
        xtimer_sleep(APP_SLEEP_S);
    }

    return 0;
}
