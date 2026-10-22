/*
 * test_state_machine.c - Unit tests for the proximity state machine
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_state.h>
#include <btlock/btlock_log.h>
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

static gboolean test_initial_state(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  ASSERT(sm != NULL, "state machine creation failed");
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_UNINITIALIZED,
         "initial state should be UNINITIALIZED");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_start_transition(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  BtlockEventResult result = btlock_state_machine_process_event(
      sm, BTLOCK_EVENT_START, NULL, 1000);
  ASSERT(result.state_changed, "state should have changed");
  ASSERT(result.new_state == BTLOCK_STATE_DISCOVERING,
         "should transition to DISCOVERING on START");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_device_found_present(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 1000);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  strcpy(dev.name, "TestPhone");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DEVICE_PRESENT,
         "should be DEVICE_PRESENT after device found");
  ASSERT(btlock_state_machine_needs_lock(sm) == FALSE,
         "should not need lock when device is present");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_device_lost_zero_grace(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 0;
  config.min_disconnects = 1;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 1000);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 2000);
  BtlockEventResult result = btlock_state_machine_process_event(
      sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 3000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_REQUESTED,
         "should be LOCK_REQUESTED with 0 grace");
  ASSERT(result.action == BTLOCK_ACTION_REQUEST_LOCK, "should request lock");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_grace_not_expired(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 15000;
  config.min_disconnects = 1;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 1000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DEPARTURE_GRACE,
         "should be in DEPARTURE_GRACE");
  BtlockEventResult result = btlock_state_machine_process_event(
      sm, BTLOCK_EVENT_TICK, NULL, 3000);
  ASSERT(result.action == BTLOCK_ACTION_NONE, "should not lock during grace");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_grace_expired(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 5000;
  config.min_disconnects = 1;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 1000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  BtlockEventResult result = btlock_state_machine_process_event(
      sm, BTLOCK_EVENT_TICK, NULL, 7001);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_REQUESTED,
         "should be LOCK_REQUESTED after grace");
  ASSERT(result.action == BTLOCK_ACTION_REQUEST_LOCK, "should request lock");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_lock_confirmed_transition(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 1000;
  config.min_disconnects = 1;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 1000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_TICK, NULL, 3000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_REQUESTED,
         "should be LOCK_REQUESTED after grace");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_LOCK_CONFIRMED, NULL, 3100);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_CONFIRMED,
                   "should be LOCK_CONFIRMED");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_bt_unavailable_recovery(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_BT_UNAVAILABLE, NULL, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_BT_UNAVAILABLE,
         "should be BT_UNAVAILABLE");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_BT_AVAILABLE, NULL, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DISCOVERING,
         "should be DISCOVERING after BT recovers");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_shutdown(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_SHUTDOWN, NULL, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_SHUTTING_DOWN,
         "should be SHUTTING_DOWN");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_auto_unlock_default_disabled(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  ASSERT(btlock_state_machine_can_auto_unlock(sm) == FALSE,
         "auto-unlock should be disabled by default");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_stop_returns_to_uninitialized(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_STOP, NULL, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_UNINITIALIZED,
           btlock_state_machine_process_event(sm, BTLOCK_EVENT_STOP, NULL, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_UNINITIALIZED,
         "stop should return to UNINITIALIZED");
  btlock_state_machine_free(sm);
    return TRUE;
}

static gboolean test_returning_with_auto_unlock(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 1000;
  config.min_disconnects = 1;
  config.auto_unlock_enabled = TRUE;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 1000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_TICK, NULL, 3000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_LOCK_CONFIRMED, NULL, 3100);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_CONFIRMED,
         "should be LOCK_CONFIRMED");
  dev.rssi = -55;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 4000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_RETURNING,
         "should be RETURNING with auto-unlock");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_multi_device_cancel_lock(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 1000;
  config.min_disconnects = 1;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev1 = {0};
  strcpy(dev1.address, "AA:BB:CC:DD:EE:01");
  dev1.rssi = -60;
  BtlockDevice dev2 = {0};
  strcpy(dev2.address, "AA:BB:CC:DD:EE:02");
  dev2.rssi = -55;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev1, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DEVICE_PRESENT,
         "should be present");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DEPARTURE_GRACE,
         "should be in grace");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev2, 2500);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DEVICE_PRESENT,
         "should be present when 2nd device found during grace");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_event_state_names(void)
{
  ASSERT(strcmp(btlock_state_name(BTLOCK_STATE_UNINITIALIZED), "UNINITIALIZED") == 0,
         "state name check");
  ASSERT(strcmp(btlock_event_name(BTLOCK_EVENT_START), "START") == 0,
         "event name check");
  return TRUE;
}

static gboolean test_null_machine_safety(void)
{
  btlock_state_machine_process_event(NULL, BTLOCK_EVENT_START, NULL, 0);
  ASSERT(btlock_state_machine_get_state(NULL) == BTLOCK_STATE_UNINITIALIZED,
         "NULL machine should return UNINITIALIZED");
    return TRUE;
}

static gboolean test_adapter_unavailable(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_ADAPTER_UNAVAILABLE, NULL, 1000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_ADAPTER_UNAVAILABLE,
         "should be ADAPTER_UNAVAILABLE");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_ADAPTER_AVAILABLE, NULL, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_DISCOVERING,
         "should be DISCOVERING after adapter returns");
  btlock_state_machine_free(sm);
  return TRUE;
}

static gboolean test_invalid_config_rejected(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.absent_threshold_dbm = -70;
  config.present_threshold_dbm = -80;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  ASSERT(sm == NULL, "should reject invalid config");
  return TRUE;
}

static gboolean test_lock_failed_retry(void)
{
  BtlockConfig config;
  btlock_config_init_defaults(&config);
  config.lock_grace_ms = 0;
  config.min_disconnects = 1;
  config.max_retries = 2;
  BtlockStateMachine *sm = btlock_state_machine_new(&config);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_START, NULL, 0);
  BtlockDevice dev = {0};
  strcpy(dev.address, "AA:BB:CC:DD:EE:FF");
  dev.rssi = -60;
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_FOUND, &dev, 1000);
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_DEVICE_LOST, NULL, 2000);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_REQUESTED,
         "should be LOCK_REQUESTED");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_LOCK_FAILED, NULL, 2100);
  ASSERT(btlock_state_machine_get_state(sm) == BTLOCK_STATE_LOCK_REQUESTED,
         "should stay LOCK_REQUESTED after 1st fail");
  btlock_state_machine_process_event(sm, BTLOCK_EVENT_LOCK_FAILED, NULL, 2200);
  btlock_state_machine_free(sm);
  return TRUE;
}

typedef gboolean (*TestFunc)(void);

static TestFunc tests[] = {
  test_initial_state,
  test_start_transition,
  test_device_found_present,
  test_device_lost_zero_grace,
  test_grace_not_expired,
  test_grace_expired,
  test_lock_confirmed_transition,
  test_bt_unavailable_recovery,
  test_shutdown,
  test_auto_unlock_default_disabled,
  test_min_disconnects_threshold,
  test_returning_with_auto_unlock,
  test_multi_device_cancel_lock,
  test_event_state_names,
  test_null_machine_safety,
  test_adapter_unavailable,
  test_invalid_config_rejected,
  test_lock_failed_retry,
  NULL,
};

int main(void)
{
  btlock_log_init(BTLOCK_LOG_ERROR, NULL);
  g_print("Running %d state machine tests...\n",
          (int)(sizeof(tests) / sizeof(tests[0]) - 1));
  for (int i = 0; tests[i] != NULL; i++) {
    gboolean ok = tests[i]();
    if (!ok)
      g_printerr("Test %d FAILED\n", i);
  }
  g_print("\nResults: %d passed, %d failed (of %d)\n",
          tests_passed, tests_failed, tests_run);
  return tests_failed > 0 ? 1 : 0;
}



