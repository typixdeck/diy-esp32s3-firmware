// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

typedef enum {
    STC_WAIT, STC_READY, STC_IO_ERROR, STC_FAULT, STC_STALE,
    STC_INVALID, STC_SEEDED, STC_FULL
} stc_status_t;

typedef struct {
    stc_status_t status;
    bool tracking, valid, charged, full_latched;
    uint16_t counter;
    int mv, ma, soc_raw;
    int64_t last_ms, eoc_since_ms, charge_ms;
} stc_gauge_t;

// Single owner: caller serializes this with every MUX transition and read.
// No sleeps, reset, MUX, charger or power-control operations in this module.
void stc_gauge_invalidate(stc_gauge_t *state);
esp_err_t stc_gauge_poll(stc_gauge_t *state, i2c_master_dev_handle_t dev,
                       int cw_mv, bool usb_present, int64_t now_ms);
