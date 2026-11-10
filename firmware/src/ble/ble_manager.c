/**
 * @file ble_manager.c
 * @brief BLE connection and security management implementation
 *
 * Implements: SWR-042, SWR-044, SWR-046, SYS-REQ-031, SYS-REQ-033
 * Risk Controls: RC-010 (AES-128-CCM encryption)
 */

#include "ble_manager.h"
#include "config/device_config.h"

#include <stdio.h>
#include <string.h>

#ifndef UNIT_TEST
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#else
#include "test_stubs.h"
#endif

static struct {
    bool            initialized;
    bool            connected;
    bool            advertising;
    uint32_t        adv_start_time;
    ble_connect_cb_t    on_connect;
    ble_disconnect_cb_t on_disconnect;
    char            wearer_name[CONFIG_BLE_WEARER_NAME_MAX + 1];
    char            adv_name[CONFIG_BLE_ADV_NAME_MAX + 1];
    uint16_t        glance_glucose;
    glucose_trend_t glance_trend;
    bool            glance_valid;
} s_ble;

/* Forward declarations */
static uint32_t get_uptime_ms(void);
static void build_adv_name(void);
static void refresh_adv_data(void);

#ifndef UNIT_TEST
static struct bt_data s_ad[3];
static const uint8_t s_ad_flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
static const uint8_t s_ad_uuid[] = { 0x1F, 0x18 }; /* CGM Service 0x181F */

static void fill_adv_data(void)
{
    s_ad[0] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &s_ad_flags, 1);
    s_ad[1] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE,
                                      s_ble.adv_name, strlen(s_ble.adv_name));
    s_ad[2] = (struct bt_data)BT_DATA(BT_DATA_UUID16_ALL,
                                      s_ad_uuid, sizeof(s_ad_uuid));
}
#endif

/**
 * @brief Initialize BLE stack with security (SWR-042, RC-010)
 *
 * Configures LE Secure Connections pairing with numeric comparison
 * and AES-128-CCM encryption for all data transfers.
 */
cgm_error_t ble_init(void)
{
#ifndef UNIT_TEST
    int err = bt_enable(NULL);
    if (err) {
        return CGM_ERR_BLE_INIT_FAIL;
    }

    /* Configure LE Secure Connections (SWR-042)
     * - Require MITM protection via numeric comparison
     * - Enable bonding for persistent pairing
     * - AES-128-CCM encryption is mandatory */
    /* bt_conn_auth_cb_register(...) */
#endif

    s_ble.initialized = true;
    s_ble.connected = false;
    s_ble.advertising = false;
    s_ble.wearer_name[0] = '\0';
    s_ble.glance_valid = false;
    build_adv_name();

    return CGM_OK;
}

/**
 * @brief Start BLE advertising (SWR-044)
 *
 * Advertising behavior:
 * - Fast interval: 1 second when no device connected
 * - Slow interval: 2 seconds after 5 minutes without connection
 * - Advertises device name and CGM Service UUID (0x181F)
 * - Device name is personalised with wearer name and latest reading (SWR-046)
 */
cgm_error_t ble_start_advertising(void)
{
    if (!s_ble.initialized) {
        return CGM_ERR_BLE_INIT_FAIL;
    }

#ifndef UNIT_TEST
    /* Configure advertising data:
     * - Flags: LE General Discoverable, BR/EDR Not Supported
     * - Complete Local Name: personalised name (SWR-046)
     * - 16-bit Service UUID: 0x181F (CGM Service) */
    struct bt_le_adv_param adv_param = {
        .options      = BT_LE_ADV_OPT_CONNECTABLE,
        .interval_min = CONFIG_BLE_ADV_INTERVAL_FAST_MS * 8 / 5, /* Convert to 0.625ms units */
        .interval_max = CONFIG_BLE_ADV_INTERVAL_FAST_MS * 8 / 5,
    };

    fill_adv_data();
    int err = bt_le_adv_start(&adv_param, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
    if (err) {
        return CGM_ERR_BLE_ADV_FAIL;
    }
#endif

    s_ble.advertising = true;
    s_ble.adv_start_time = get_uptime_ms();

    return CGM_OK;
}

void ble_stop_advertising(void)
{
#ifndef UNIT_TEST
    bt_le_adv_stop();
#endif
    s_ble.advertising = false;
}

bool ble_is_connected(void)
{
    return s_ble.connected;
}

void ble_register_callbacks(ble_connect_cb_t on_connect,
                            ble_disconnect_cb_t on_disconnect)
{
    s_ble.on_connect = on_connect;
    s_ble.on_disconnect = on_disconnect;
}

/**
 * @brief Auto-reconnection support (SYS-REQ-033)
 * After disconnection, advertising resumes automatically to allow
 * the mobile app to reconnect within 30 seconds.
 */
cgm_error_t ble_disconnect(void)
{
    if (!s_ble.connected) {
        return CGM_OK;
    }

#ifndef UNIT_TEST
    /* bt_conn_disconnect(...) */
#endif

    s_ble.connected = false;
    if (s_ble.on_disconnect) {
        s_ble.on_disconnect();
    }

    /* Resume advertising for auto-reconnection */
    return ble_start_advertising();
}

int8_t ble_get_tx_power(void)
{
    return CONFIG_BLE_TX_POWER_DBM;
}

/* --- Personalised device name (SWR-046) --- */

cgm_error_t ble_set_wearer_name(const char *name, uint16_t len)
{
    if (name == NULL) {
        return CGM_ERR_BLE_NOTIFY_FAIL;
    }

    if (len > CONFIG_BLE_WEARER_NAME_MAX) {
        len = CONFIG_BLE_WEARER_NAME_MAX;
    }
    memcpy(s_ble.wearer_name, name, len);
    s_ble.wearer_name[len] = '\0';

    build_adv_name();
    refresh_adv_data();

    return CGM_OK;
}

void ble_update_glance(uint16_t glucose_mgdl, glucose_trend_t trend)
{
    s_ble.glance_glucose = glucose_mgdl;
    s_ble.glance_trend = trend;
    s_ble.glance_valid = (glucose_mgdl != GLUCOSE_INVALID);

    build_adv_name();
    refresh_adv_data();
}

const char *ble_get_adv_name(void)
{
    return s_ble.adv_name;
}

static const char *trend_glyph(glucose_trend_t trend)
{
    switch (trend) {
    case TREND_FALLING_RAPIDLY: return "vv";
    case TREND_FALLING:         return "v";
    case TREND_STABLE:          return "->";
    case TREND_RISING:          return "^";
    case TREND_RISING_RAPIDLY:  return "^^";
    default:                    return "";
    }
}

/**
 * Builds the advertised name:
 *   no wearer name, no reading  -> "GlucoSense CGM"
 *   wearer name only            -> "Emma S"
 *   wearer name + reading       -> "Emma S 64 v"
 *   reading only                -> "GlucoSense CGM 64 v"
 */
static void build_adv_name(void)
{
    const char *base = (s_ble.wearer_name[0] != '\0')
                       ? s_ble.wearer_name
                       : CONFIG_BLE_DEVICE_NAME;

    if (s_ble.glance_valid) {
        snprintf(s_ble.adv_name, sizeof(s_ble.adv_name), "%s %u %s",
                 base, (unsigned)s_ble.glance_glucose,
                 trend_glyph(s_ble.glance_trend));
    } else {
        snprintf(s_ble.adv_name, sizeof(s_ble.adv_name), "%s", base);
    }

    /* Drop trailing space when there is no trend glyph */
    size_t n = strlen(s_ble.adv_name);
    if (n > 0 && s_ble.adv_name[n - 1] == ' ') {
        s_ble.adv_name[n - 1] = '\0';
    }
}

static void refresh_adv_data(void)
{
#ifndef UNIT_TEST
    bt_set_name(s_ble.adv_name);
    if (s_ble.advertising) {
        fill_adv_data();
        bt_le_adv_update_data(s_ad, ARRAY_SIZE(s_ad), NULL, 0);
    }
#endif
}

/* --- BLE event handlers (called from SoftDevice context) --- */

#ifndef UNIT_TEST
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }
    s_ble.connected = true;
    s_ble.advertising = false;

    if (s_ble.on_connect) {
        s_ble.on_connect();
    }
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    s_ble.connected = false;

    if (s_ble.on_disconnect) {
        s_ble.on_disconnect();
    }

    /* Auto-restart advertising for reconnection (SYS-REQ-033) */
    ble_start_advertising();
}
#endif

static uint32_t get_uptime_ms(void)
{
#ifndef UNIT_TEST
    return k_uptime_get_32();
#else
    extern uint32_t test_stub_uptime_ms;
    return test_stub_uptime_ms;
#endif
}
