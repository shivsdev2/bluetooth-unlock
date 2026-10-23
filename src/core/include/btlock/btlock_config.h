/*
 * btlock_config.h - Configuration schema and validation for btlock
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Defines the configuration structure, default values, validation
 * rules, and migration support. The same config schema is mirrored
 * in Python (src/daemon/btlock/config.py) for cross-language consistency.
 *
 * Trust boundary: Configuration is a trust boundary. All values must
 * be validated before use. Defaults must be safe.
 */

#ifndef BTLOCK_CONFIG_H
#define BTLOCK_CONFIG_H

#include <btlock/btlock_common.h>

/* Configuration schema version for migration support */
#define BTLOCK_CONFIG_VERSION 1
#define BTLOCK_CONFIG_VERSION_STR "1.0"

/* --- Default values --- */
#define BTLOCK_DEFAULT_LOCK_GRACE_SEC  15      /* Grace before locking */
#define BTLOCK_DEFAULT_ABSENT_THRESHOLD_DBM  (-90)  /* RSSI below which device is "absent" */
#define BTLOCK_DEFAULT_PRESENT_THRESHOLD_DBM (-80)  /* RSSI above which device is "present" */
#define BTLOCK_DEFAULT_RSSI_WINDOW       5      /* Samples for moving average */
#define BTLOCK_DEFAULT_MIN_DISCONNECTS   3      /* Consecutive absent samples needed */
#define BTLOCK_DEFAULT_RETURN_THRESHOLD_DBM (-75)  /* RSSI above which device is "returning" */
#define BTLOCK_DEFAULT_TICK_INTERVAL_SEC 2      /* Timer tick interval */
#define BTLOCK_DEFAULT_RECOVERY_RETRY_SEC  5   /* Backoff for recovery */
#define BTLOCK_DEFAULT_MAX_RETRIES        3    /* Max retries for lock operation */
#define BTLOCK_DEFAULT_REQUIRED_DEVICES    0    /* 0 = any single device */

/* Validation bounds */
#define BTLOCK_MIN_LOCK_GRACE_SEC   0
#define BTLOCK_MAX_LOCK_GRACE_SEC   300  /* 5 minutes max */

#define BTLOCK_MIN_RSSI_THRESHOLD  (-120)
#define BTLOCK_MAX_RSSI_THRESHOLD  (0)

#define BTLOCK_MIN_RSSI_WINDOW     1
#define BTLOCK_MAX_RSSI_WINDOW     50

#define BTLOCK_MIN_MIN_DISCONNECTS  1
#define BTLOCK_MAX_MIN_DISCONNECTS  20

#define BTLOCK_MIN_TICK_INTERVAL_SEC 1
#define BTLOCK_MAX_TICK_INTERVAL_SEC 60

/*
 * BtlockConfig - All configuration for the proximity system.
 *
 * All time values are in milliseconds (converted from seconds at load).
 * RSSI thresholds are in dBm (negative values).
 */
typedef struct {
  /* Schema version (for migration) */
  guint32 schema_version;

  /* Departure detection */
  guint32 lock_grace_ms;           /* Grace period before locking after device lost */
  int8_t absent_threshold_dbm;     /* RSSI below this = "absent" */
  int8_t present_threshold_dbm;    /* RSSI above this = "present" */
  guint32 rssi_window;             /* Number of RSSI samples for moving average */
  guint32 min_disconnects;         /* Consecutive absent samples before state change */

  /* Return detection */
  int8_t return_threshold_dbm;     /* RSSI above this = "returning" */
  guint32 return_grace_ms;         /* Grace period to confirm return */

  /* Discovery */
  guint32 tick_interval_ms;        /* How often to check device timeouts */

  /* Multiple-device policy */
  guint32 required_devices;        /* 0=any absent triggers lock; N=lock only when all N absent */

  /* Security: auto-unlock */
  gboolean auto_unlock_enabled;    /* MUST be true to allow auto-unlock */
  gboolean lock_confirm_required;  /* Must get lock confirmation from session */

  /* Recovery */
  guint32 recovery_retry_ms;       /* Backoff for retry after failure */
  guint32 max_retries;             /* Max consecutive retries before error state */
} BtlockConfig;

/*
 * BtlockConfigError - Validation error codes.
 */
typedef enum {
  BTLOCK_CONFIG_OK = 0,
  BTLOCK_CONFIG_ERR_INVALID_RANGE,    /* Value out of allowed range */
  BTLOCK_CONFIG_ERR_ABSENT_GT_PRESENT,/* absent threshold >= present threshold */
  BTLOCK_CONFIG_ERR_RETURN_TOO_LOW,   /* return threshold < present threshold */
  BTLOCK_CONFIG_ERR_SCHEMA_VERSION,   /* Unknown schema version */
} BtlockConfigError;

/*
 * Load default (safe) configuration.
 */
void btlock_config_init_defaults(BtlockConfig *config);

/*
 * Validate a configuration.
 *
 * Returns: BTLOCK_CONFIG_OK if valid, error code otherwise.
 * error_msg: Set to a human-readable description on error (caller frees).
 *            May be NULL if caller doesn't need the message.
 */
BtlockConfigError btlock_config_validate(const BtlockConfig *config,
                                          char **error_msg);

/*
 * Check if two configs are equivalent (for change detection).
 */
gboolean btlock_config_equal(const BtlockConfig *a, const BtlockConfig *b);

#endif /* BTLOCK_CONFIG_H */
