/*
 * btlock_log.c - Logging implementation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implementation of the logging subsystem. Uses GLib's logging
 * facilities for consistency with the GNOME ecosystem.
 *
 * Security: Addresses are truncated in non-debug levels. No
 * credentials or tokens are ever logged.
 */

#include <btlock/btlock_log.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

static BtlockLogLevel s_log_level = BTLOCK_LOG_INFO;
static gboolean s_debug_enabled = FALSE;

static const char *CATEGORY_NAMES[] = {
  [BTLOCK_CAT_CORE]      = "core",
  [BTLOCK_CAT_BLUETOOTH] = "bluetooth",
  [BTLOCK_CAT_POLICY]    = "policy",
  [BTLOCK_CAT_CONFIG]    = "config",
  [BTLOCK_CAT_DESKTOP]   = "desktop",
  [BTLOCK_CAT_SECURITY]  = "security",
};

const char *btlock_log_truncate_address(const char *addr)
{
  /* Return the address as-is if empty or very short */
  if (!addr || strlen(addr) < 17)
    return addr ? addr : "(unknown)";

  /* Only truncate for non-debug levels to avoid leaking info */
  if (s_debug_enabled)
    return addr;

  static char truncated[18]; /* "AA:BB:CC:xx:xx:xx\0" - 17 chars + NUL */
  snprintf(truncated, sizeof(truncated), "%.2s:%.2s:%.2s:xx:xx:xx",
           addr, addr + 3, addr + 6);
  return truncated;
}

void btlock_log_init(BtlockLogLevel level, const char *log_file)
{
  s_log_level = level;
  s_debug_enabled = (level == BTLOCK_LOG_DEBUG);

  if (log_file) {
    /* Set up GLib logging to file */
    g_setenv("G_MESSAGES_DEBUG", "btlock", TRUE);
    g_setenv("BTSILENCE_DUPLICATE", "1", TRUE);
  }
}

gboolean btlock_log_debug_enabled(void)
{
  return s_debug_enabled;
}

void btlock_log(BtlockLogLevel level, BtlockLogCategory cat,
                const char *fmt, ...)
{
  if (level > s_log_level)
    return;

  char message[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  const char *cat_name = (cat < (int)(sizeof(CATEGORY_NAMES) / sizeof(CATEGORY_NAMES[0])))
    ? CATEGORY_NAMES[cat] : "unknown";

  /* Use structured logging for consistency with GNOME ecosystem */
  g_log_structured(
    "btlock",
    (GLogLevelFlags)level,
    "message", "%s", message,
    "category", "%s", cat_name,
    NULL);
}

