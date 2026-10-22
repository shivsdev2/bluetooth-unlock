/*
 * btlock_config.c - Configuration defaults and validation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_config.h>
#include <string.h>

void btlock_config_init_defaults(BtlockConfig *config)
{
  if (!config)
    return;

  memset(config, 0, sizeof(*config));

  config->schema_version = BTLOCK_CONFIG_VERSION;
  config->lock_grace_ms = BTLOCK_DEFAULT_LOCK_GRACE_SEC * 1000;
  config->absent_threshold_dbm = BTLOCK_DEFAULT_ABSENT_THRESHOLD_DBM;
  config->present_threshold_dbm = BTLOCK_DEFAULT_PRESENT_THRESHOLD_DBM;
  config->rssi_window = BTLOCK_DEFAULT_RSSI_WINDOW;
  config->min_disconnects = BTLOCK_DEFAULT_MIN_DISCONNECTS;
  config->return_threshold_dbm = BTLOCK_DEFAULT_RETURN_THRESHOLD_DBM;
  config->return_grace_ms = BTLOCK_DEFAULT_RETURN_THRESHOLD_DBM * 1000; /* approx: use lock grace */
  config->return_grace_ms = config->lock_grace_ms; /* symmetry: same grace for return */
  config->tick_interval_ms = BTLOCK_DEFAULT_TICK_INTERVAL_SEC * 1000;
  config->required_devices = BTLOCK_DEFAULT_REQUIRED_DEVICES;
  config->auto_unlock_enabled = FALSE; /* Security: disabled by default */
  config->lock_confirm_required = TRUE; /* Safe default */
  config->recovery_retry_ms = BTLOCK_DEFAULT_RECOVERY_RETRY_SEC * 1000;
  config->max_retries = BTLOCK_DEFAULT_MAX_RETRIES;
}

static gboolean range_check(gint32 value, gint32 lo, gint32 hi, const char *name, char **err)
{
  if (value < lo || value > hi) {
    if (err)
      *err = g_strdup_printf("%s must be between %d and %d (got %d)",
                             name, (int)lo, (int)hi, (int)value);
    return FALSE;
  }
  return TRUE;
}

BtlockConfigError btlock_config_validate(const BtlockConfig *config, char **error_msg)
{
  if (!config) {
    if (error_msg)
      *error_msg = g_strdup("config is NULL");
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;
  }

  if (config->schema_version != BTLOCK_CONFIG_VERSION) {
    if (error_msg)
      *error_msg = g_strdup_printf("schema version %u not supported (expected %u)",
                                   config->schema_version, BTLOCK_CONFIG_VERSION);
    return BTLOCK_CONFIG_ERR_SCHEMA_VERSION;
  }

  if (!range_check((gint32)config->lock_grace_ms,
                   BTLOCK_MIN_LOCK_GRACE_SEC * 1000,
                   BTLOCK_MAX_LOCK_GRACE_SEC * 1000,
                   "lock_grace_ms", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check(config->absent_threshold_dbm,
                   BTLOCK_MIN_RSSI_THRESHOLD,
                   BTLOCK_MAX_RSSI_THRESHOLD,
                   "absent_threshold_dbm", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check(config->present_threshold_dbm,
                   BTLOCK_MIN_RSSI_THRESHOLD,
                   BTLOCK_MAX_RSSI_THRESHOLD,
                   "present_threshold_dbm", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (config->absent_threshold_dbm >= config->present_threshold_dbm) {
    if (error_msg)
      *error_msg = g_strdup_printf(
        "absent_threshold_dbm (%d) must be less than present_threshold_dbm (%d)",
        (int)config->absent_threshold_dbm, (int)config->present_threshold_dbm);
    return BTLOCK_CONFIG_ERR_ABSENT_GT_PRESENT;
  }

  if (!range_check(config->rssi_window,
                   BTLOCK_MIN_RSSI_WINDOW,
                   BTLOCK_MAX_RSSI_WINDOW,
                   "rssi_window", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check(config->min_disconnects,
                   BTLOCK_MIN_MIN_DISCONNECTS,
                   BTLOCK_MAX_MIN_DISCONNECTS,
                   "min_disconnects", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check(config->return_threshold_dbm,
                   BTLOCK_MIN_RSSI_THRESHOLD,
                   BTLOCK_MAX_RSSI_THRESHOLD,
                   "return_threshold_dbm", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (config->return_threshold_dbm < config->present_threshold_dbm) {
    if (error_msg)
      *error_msg = g_strdup_printf(
        "return_threshold_dbm (%d) must be >= present_threshold_dbm (%d)",
        (int)config->return_threshold_dbm, (int)config->present_threshold_dbm);
    return BTLOCK_CONFIG_ERR_RETURN_TOO_LOW;
  }

  if (!range_check((gint32)config->tick_interval_ms,
                   BTLOCK_MIN_TICK_INTERVAL_SEC * 1000,
                   BTLOCK_MAX_TICK_INTERVAL_SEC * 1000,
                   "tick_interval_ms", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check((gint32)config->recovery_retry_ms, 1000, 60000,
                   "recovery_retry_ms", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (!range_check(config->max_retries, 1, 20,
                   "max_retries", error_msg))
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;

  if (config->required_devices > 20) {
    if (error_msg)
      *error_msg = g_strdup("required_devices must be <= 20");
    return BTLOCK_CONFIG_ERR_INVALID_RANGE;
  }

  return BTLOCK_CONFIG_OK;
}

gboolean btlock_config_equal(const BtlockConfig *a, const BtlockConfig *b)
{
  if (!a || !b)
    return (a == b);

  return memcmp(a, b, sizeof(BtlockConfig)) == 0;
}
