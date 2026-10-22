/*
 * test_policy.c - Unit tests for the policy engine
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_policy.h>
#include <string.h>
#include <limits.h>

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

static gboolean test_single_device_present(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.present_threshold_dbm = -80;
  config.absent_threshold_dbm = -90;
  config.rssi_window = 3;
  config.min_disconnects = 1;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  ASSERT(engine != NULL, "engine creation failed");
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -60, TRUE, 1000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 2000);
  ASSERT(dec == BTLOCK_POLICY_KEEP_UNLOCKED, "present device should not lock");
  ASSERT(btlock_policy_engine_get_trusted_count(engine) == 1, "1 trusted");
  ASSERT(btlock_policy_engine_get_present_count(engine) == 1, "1 present");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_device_absent_allow_lock(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.present_threshold_dbm = -80;
  config.absent_threshold_dbm = -90;
  config.rssi_window = 1;
  config.min_disconnects = 1;
  config.lock_grace_ms = 0;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -95, TRUE, 1000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 2000);
  ASSERT(dec == BTLOCK_POLICY_ALLOW_LOCK, "absent device should allow lock");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_device_in_grace(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.present_threshold_dbm = -80;
  config.absent_threshold_dbm = -90;
  config.rssi_window = 1;
  config.min_disconnects = 1;
  config.lock_grace_ms = 10000;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -60, TRUE, 1000);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      INT8_MIN, TRUE, 2000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 5000);
    ASSERT(dec == BTLOCK_POLICY_IN_GRACE, "should be in grace period");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_multi_device_one_present(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.min_disconnects = 1;
  config.lock_grace_ms = 0;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:01", "Phone1",
                                      -60, TRUE, 1000);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:02", "Phone2",
                                      -95, TRUE, 1000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 2000);
  ASSERT(dec == BTLOCK_POLICY_KEEP_UNLOCKED, "one present prevents lock");
  ASSERT(btlock_policy_engine_get_present_count(engine) == 1, "1 present");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_multi_device_all_absent(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.min_disconnects = 1;
  config.lock_grace_ms = 0;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:01", "Phone1",
                                      -95, TRUE, 1000);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:02", "Phone2",
                                      -95, TRUE, 1000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 2000);
  ASSERT(dec == BTLOCK_POLICY_ALLOW_LOCK, "all absent allows lock");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_unenrolled_ignored(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -95, FALSE, 1000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 2000);
  ASSERT(dec == BTLOCK_POLICY_KEEP_UNLOCKED, "unenrolled device ignored");
  ASSERT(btlock_policy_engine_get_trusted_count(engine) == 0, "no trusted");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_invalid_address_rejected(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  BtlockPolicyDevice *dev = btlock_policy_engine_update_device(
      engine, "INVALID", "Phone", -60, TRUE, 1000);
  ASSERT(dev == NULL, "invalid address should be rejected");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_remove_device(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -60, TRUE, 1000);
  ASSERT(btlock_policy_engine_get_trusted_count(engine) == 1, "1 trusted");
  btlock_policy_engine_remove_device(engine, "AA:BB:CC:DD:EE:FF");
    ASSERT(btlock_policy_engine_get_trusted_count(engine) == 0, "0 after remove");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_get_device(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -60, TRUE, 1000);
  const BtlockPolicyDevice *dev = btlock_policy_engine_get_device(engine,
      "AA:BB:CC:DD:EE:FF");
  ASSERT(dev != NULL, "should find device");
  ASSERT(strcmp(dev->device.address, "AA:BB:CC:DD:EE:FF") == 0, "address match");
  ASSERT(strcmp(dev->device.name, "Phone") == 0, "name match");
  ASSERT(dev->enrolled == TRUE, "should be enrolled");
  const BtlockPolicyDevice *null_dev = btlock_policy_engine_get_device(engine,
      "FF:FF:FF:FF:FF:FF");
  ASSERT(null_dev == NULL, "non-existent device should return NULL");
  btlock_policy_engine_free(engine);
  return TRUE;
}

static gboolean test_rssi_filtering(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.present_threshold_dbm = -80;
  config.absent_threshold_dbm = -90;
  config.rssi_window = 3;
  config.min_disconnects = 1;
  BtlockPolicyEngine *engine = btlock_policy_engine_new(&config);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -60, TRUE, 1000);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -85, TRUE, 2000);
  btlock_policy_engine_update_device(engine, "AA:BB:CC:DD:EE:FF", "Phone",
                                      -62, TRUE, 3000);
  BtlockPolicyDecision dec = btlock_policy_engine_evaluate(engine, 4000);
  ASSERT(dec == BTLOCK_POLICY_KEEP_UNLOCKED, "noise should be filtered");
  btlock_policy_engine_free(engine);
  return TRUE;
}

typedef gboolean (*TestFunc)(void);

static TestFunc policy_tests[] = {
  test_single_device_present,
  test_device_absent_allow_lock,
  test_device_in_grace,
  test_multi_device_one_present,
  test_multi_device_all_absent,
  test_unenrolled_ignored,
  test_invalid_address_rejected,
  test_remove_device,
  test_get_device,
  test_rssi_filtering,
  NULL,
};

int main(void)
{
  g_print("Running %d policy tests...\n",
          (int)(sizeof(policy_tests) / sizeof(policy_tests[0]) - 1));
  for (int i = 0; policy_tests[i] != NULL; i++) {
    gboolean ok = policy_tests[i]();
    if (!ok)
      g_printerr("Test %d FAILED\n", i);
  }
  g_print("\nResults: %d passed, %d failed (of %d)\n",
          tests_passed, tests_failed, tests_run);
  return tests_failed > 0 ? 1 : 0;
}
