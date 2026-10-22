/*
 * btlock_log.h - Logging API for btlock-core
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides structured logging with levels and categories.
 * Security note: Logs must never contain credentials, passwords,
 * authentication tokens, or unnecessary device identifiers.
 * Only MAC addresses used for correlation are logged, and
 * they are truncated in non-debug levels.
 */

#ifndef BTLOCK_LOG_H
#define BTLOCK_LOG_H

#include <glib.h>

/* Log levels - mirror GLib levels */
typedef enum {
  BTLOCK_LOG_ERROR   = G_LOG_LEVEL_ERROR,
  BTLOCK_LOG_CRITICAL = G_LOG_LEVEL_CRITICAL,
  BTLOCK_LOG_WARNING  = G_LOG_LEVEL_WARNING,
  BTLOCK_LOG_MESSAGE  = G_LOG_LEVEL_MESSAGE,
  BTLOCK_LOG_INFO    = G_LOG_LEVEL_INFO,
  BTLOCK_LOG_DEBUG   = G_LOG_LEVEL_DEBUG,
} BtlockLogLevel;

/* Log categories for structured output */
typedef enum {
  BTLOCK_CAT_CORE,       /* State machine and core logic */
  BTLOCK_CAT_BLUETOOTH,  /* Bluetooth discovery and D-Bus */
  BTLOCK_CAT_POLICY,     /* Policy engine decisions */
  BTLOCK_CAT_CONFIG,     /* Configuration loading/validation */
  BTLOCK_CAT_DESKTOP,    /* Desktop integration */
  BTLOCK_CAT_SECURITY,   /* Security-relevant events */
} BtlockLogCategory;

/*
 * Initialize logging. If log_domain is NULL, uses "btlock".
 * Sets the default log level.
 */
void btlock_log_init(BtlockLogLevel level, const char *log_file);

/*
 * Log a message with category and level.
 */
void btlock_log(BtlockLogLevel level, BtlockLogCategory cat,
                const char *fmt, ...) G_GNUC_PRINTF(3, 4);

/*
 * Truncate a device address for non-debug logs.
 * Shows only the first 3 octets: "AA:BB:CC:xx:xx:xx"
 */
const char *btlock_log_truncate_address(const char *addr);

/*
 * Check if debug logging is enabled.
 */
gboolean btlock_log_debug_enabled(void);

/* Convenience macros */
#define btlock_debug(fmt, ...) \
  btlock_log(BTLOCK_LOG_DEBUG, BTLOCK_CAT_CORE, fmt, ##__VA_ARGS__)
#define btlock_info(fmt, ...) \
  btlock_log(BTLOCK_LOG_INFO, BTLOCK_CAT_CORE, fmt, ##__VA_ARGS__)
#define btlock_warn(fmt, ...) \
  btlock_log(BTLOCK_LOG_WARNING, BTLOCK_CAT_CORE, fmt, ##__VA_ARGS__)
#define btlock_error(fmt, ...) \
  btlock_log(BTLOCK_LOG_ERROR, BTLOCK_CAT_CORE, fmt, ##__VA_ARGS__)

#endif /* BTLOCK_LOG_H */
