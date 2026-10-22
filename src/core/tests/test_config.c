/*
 * test_config.c - Unit tests for configuration validation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_config.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
  tests_run++; \
  if (!(cond)) { \
    g_printerr("FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
    tests_failed++; \
    return FALSE; \
  } \
  tests_passed++; \
} while (0)

static gboolean test_defaults_valid(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
    char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_OK, "default config should be valid");
  ASSERT(err == NULL, "no error message expected");
  g_free(err);
  return TRUE;
}

static gboolean test_auto_unlock_default(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  ASSERT(config.auto_unlock_enabled == FALSE, "auto-unlock must be FALSE by default");
  ASSERT(config.lock_confirm_required == TRUE, "lock_confirm_required should be TRUE");
  return TRUE;
}

static gboolean test_absent_gt_present_invalid(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.absent_threshold_dbm = -70;
  config.present_threshold_dbm = -80;
  char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_ERR_ABSENT_GT_PRESENT,
         "absent >= present should be invalid");
  ASSERT(err != NULL, "error message should be set");
  g_free(err);
  return TRUE;
}

static gboolean test_return_threshold_too_low(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.return_threshold_dbm = -90;  /* Below present (-80) */
  config.present_threshold_dbm = -80;
  char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_ERR_RETURN_TOO_LOW, "return threshold too low");
  g_free(err);
  return TRUE;
}

static gboolean test_schema_version_invalid(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.schema_version = 999;
  char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_ERR_SCHEMA_VERSION, "invalid schema version");
  g_free(err);
  return TRUE;
}

static gboolean test_out_of_range_lock_grace(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 1000 * 700; /* 700 seconds - too high */
  char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_ERR_INVALID_RANGE, "out of range grace");
  g_free(err);
  return TRUE;
}

static gboolean test_out_of_range_rssi_window(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.rssi_window = 0;
  char *err = NULL;
  BtlockConfigError e = btlock_config_validate(&config, &err);
  ASSERT(e == BTLOCK_CONFIG_ERR_INVALID_RANGE, "window 0 should be invalid");
  g_free(err);
  return TRUE;
}

static gboolean test_acceptable_boundaries(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 0; /* Min allowed */
  config.absent_threshold_dbm = -120; /* Min allowed */
  config.present_threshold_dbm = 0;   /* Max allowed, absent < present */
  config.rssi_window = 1;
  config.min_disconnects = 1;
  ASSERT(btlock_config_validate(&config, NULL) == BTLOCK_CONFIG_OK,
         "boundary values should be valid");
  return TRUE;
}

static gboolean test_config_equal(void)
{
  BtlockConfig a, b;
  btlock_config_init_defaults(&a);
  btlock_config_init_defaults(&b);
  ASSERT(btlock_config_equal(&a, &b) == TRUE, "identical configs should be equal");
  a.lock_grace_ms = 5000;
  ASSERT(btlock_config_equal(&a, &b) == FALSE, "different configs should not be equal");
  return TRUE;
}

typedef gboolean (*TestFunc)(void);

static TestFunc config_tests[] = {
  test_defaults_valid,
  test_auto_unlock_default,
  test_absent_gt_present_invalid,
  test_return_threshold_too_low,
  test_schema_version_invalid,
  test_out_of_range_lock_grace,
  test_out_of_range_rssi_window,
  test_acceptable_boundaries,
  test_config_equal,
  NULL,
};

int main(void)
{
  g_print("Running %d config tests...\n",
          (int)(sizeof(config_tests) / sizeof(config_tests[0]) - 1));
  for (int i = 0; config_tests[i] != NULL; i++) {
    gboolean ok = config_tests[i]();
    if (!ok)
      g_printerr("Test %d FAILED\n", i);
  }
  g_print("\nResults: %d passed, %d failed (of %d)\n",
          tests_passed, tests_failed, tests_run);
  return tests_failed > 0 ? 1 : 0;
}
