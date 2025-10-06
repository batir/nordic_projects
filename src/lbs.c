/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief LED Button Service (LBS) sample
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/sensor.h>

#include "lbs.h"

#include <zephyr/logging/log.h>
#define CONFIG_BT_LBS_LOG_LEVEL 3
LOG_MODULE_REGISTER(bt_lbs, CONFIG_BT_LBS_LOG_LEVEL);

static bool notify_enabled;
static bool button_state;
static struct bt_lbs_cb lbs_cb;
static int temperature_value;

static void lbslc_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t write_led(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags)
{
	LOG_DBG("Attribute write, handle: %u, conn: %p", attr->handle, (void *)conn);

	if (len != 1U) {
		LOG_DBG("Write led: Incorrect data length");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (offset != 0) {
		LOG_DBG("Write led: Incorrect data offset");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (lbs_cb.led_cb) {
		uint8_t val = *((uint8_t *)buf);

		if (val == 0x00 || val == 0x01) {
			lbs_cb.led_cb(val ? true : false);
		} else {
			LOG_DBG("Write led: Incorrect value");
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
	}

	return len;
}
static ssize_t read_button(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	LOG_DBG("Attribute read, handle: %u, conn: %p", attr->handle, (void *)conn);

	if (lbs_cb.button_cb) {
		button_state = lbs_cb.button_cb();
		return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
	}

	return 0;
}

static const struct device *const temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	struct sensor_value temp_val;
	const char *value = attr->user_data;
	if (!device_is_ready(temp_dev)) {
		LOG_INF("Temperature sensor device not ready!\n");
		return 0;
	}

	LOG_INF("On-chip temperature sensor example\n");

	// Fetch a new sample from the sensor
	if (sensor_sample_fetch(temp_dev) != 0) {
		LOG_INF("Failed to fetch temperature sample\n");
	} else {
		// Get the temperature channel data
		sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &temp_val);
		
		// sensor_value_to_double() converts the struct to a float/double
		temperature_value = (int)sensor_value_to_double(&temp_val);
		printk("Current die temperature: %d °C\n", temperature_value);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));;
}

/* LED Button Service Declaration */
BT_GATT_SERVICE_DEFINE(
	lbs_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_LBS),
	BT_GATT_CHARACTERISTIC(BT_UUID_LBS_BUTTON, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_button, NULL, &button_state),
	BT_GATT_CCC(lbslc_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_LBS_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_temperature, NULL, &temperature_value),
	BT_GATT_CUD("Temperature sensor value", BT_GATT_PERM_READ),
	/* STEP 1.1 - Change the LED characteristic permission to require encryption */
	/* STEP 8 - Change the LED characteristic permission to require pairing with authentication */
	BT_GATT_CHARACTERISTIC(BT_UUID_LBS_LED, BT_GATT_CHRC_WRITE,
			       // BT_GATT_PERM_WRITE_ENCRYPT,
			       BT_GATT_PERM_WRITE_AUTHEN, NULL, write_led, NULL), );

int bt_lbs_init(struct bt_lbs_cb *callbacks)
{
	if (callbacks) {
		lbs_cb.led_cb = callbacks->led_cb;
		lbs_cb.button_cb = callbacks->button_cb;
	}

	return 0;
}

int bt_lbs_send_button_state(bool button_state)
{
	if (!notify_enabled) {
		return -EACCES;
	}

	return bt_gatt_notify(NULL, &lbs_svc.attrs[2], &button_state, sizeof(button_state));
}
