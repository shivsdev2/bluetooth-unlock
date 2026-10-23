/*
 * btlock_policy.c - Policy engine implementation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The policy engine tracks multiple Bluetooth devices, applies
 * signal filtering, and evaluates the collective state against
 * the configured policy to decide whether to lock.
 */

#include <btlock/btlock_policy.h>
#include <btlock/btlock_log.h>
#include <string.h>
#include <stdlib.h>

#define MAX_POLICY_DEVICES 64

struct _BtlockPolicyEngine {
  BtlockConfig config;
  BtlockPolicyDevice devices[MAX_POLICY_DEVICES];
  guint device_count;
  BtlockFilterType filter_type;
};

static BtlockPolicyDevice *find_device(BtlockPolicyEngine *engine, const char *address)
{
  if (!address)
    return NULL;
  for (guint i = 0; i < engine->device_count; i++) {
    if (strcmp(engine->devices[i].device.address, address) == 0)
      return &engine->devices[i];
  }
  return NULL;
}

static BtlockPolicyDevice *find_free_slot(BtlockPolicyEngine *engine)
{
  if (engine->device_count < MAX_POLICY_DEVICES) {
    engine->device_count++;
    return &engine->devices[engine->device_count - 1];
  }
  return NULL;
}

BtlockPolicyEngine *btlock_policy_engine_new(const BtlockConfig *config)
{
  if (!config)
    return NULL;

  BtlockPolicyEngine *engine = g_new0(BtlockPolicyEngine, 1);
  if (!engine)
    return NULL;

  engine->config = *config;
  engine->filter_type = BTLOCK_FILTER_MEDIAN;
  engine->device_count = 0;
  return engine;
}

void btlock_policy_engine_free(BtlockPolicyEngine *engine)
{
  if (!engine)
    return;
  for (guint i = 0; i < engine->device_count; i++) {
    if (engine->devices[i].rssi_filter)
      btlock_filter_free(engine->devices[i].rssi_filter);
  }
  g_free(engine);
}

void btlock_policy_engine_set_config(BtlockPolicyEngine *engine,
                                     const BtlockConfig *config)
{
  if (!engine || !config)
    return;
  engine->config = *config;
  for (guint i = 0; i < engine->device_count; i++) {
    if (engine->devices[i].rssi_filter) {
      btlock_filter_free(engine->devices[i].rssi_filter);
      engine->devices[i].rssi_filter =
        btlock_filter_new(engine->filter_type, config->rssi_window);
    }
  }
}

BtlockPolicyDevice *btlock_policy_engine_update_device(
    BtlockPolicyEngine *engine,
    const char *address,
    const char *name,
    int8_t rssi,
    gboolean is_trusted,
        guint64 monotonic_now_ms)
{
  if (!engine || !address || !btlock_address_is_valid(address))
    return NULL;

  BtlockPolicyDevice *dev = find_device(engine, address);
  if (!dev) {
    dev = find_free_slot(engine);
    if (!dev) {
      btlock_warn("Policy engine device limit reached (%d)", MAX_POLICY_DEVICES);
      return NULL;
    }
    memset(dev, 0, sizeof(BtlockPolicyDevice));
    dev->device.rssi = INT8_MIN;
    dev->enrolled = is_trusted;
    dev->rssi_filter = btlock_filter_new(engine->filter_type,
                                          engine->config.rssi_window);
  }

  /* Update device identity */
  g_snprintf(dev->device.address, BTLOCK_ADDR_MAX, "%s", address);
  if (name)
    g_snprintf(dev->device.name, BTLOCK_NAME_MAX, "%s", name);
  dev->enrolled = is_trusted;
  dev->device.last_seen_ms = monotonic_now_ms;

  /* Add RSSI sample */
  if (rssi != INT8_MIN && dev->rssi_filter) {
    btlock_filter_add_sample(dev->rssi_filter, rssi, monotonic_now_ms);
    dev->device.rssi = btlock_filter_get_filtered(dev->rssi_filter);
    dev->device.last_rssi_update_ms = monotonic_now_ms;

    /* Evaluate presence */
    if (btlock_filter_is_present(dev->rssi_filter,
                                  engine->config.present_threshold_dbm)) {
      dev->is_present = TRUE;
      dev->absent_count = 0;
      dev->last_present_ms = monotonic_now_ms;
    } else if (!btlock_filter_is_present(dev->rssi_filter,
                                          engine->config.absent_threshold_dbm)) {
      dev->is_present = FALSE;
      dev->absent_count++;
      if (dev->last_absent_ms == 0)
        dev->last_absent_ms = monotonic_now_ms;
    } else {
      /* Between thresholds - hysteresis zone, keep current state */
    }
  }
  return dev;
}

void btlock_policy_engine_mark_absent(BtlockPolicyEngine *engine,
                                      const char *address,
                                      guint64 monotonic_now_ms)
{
  BtlockPolicyDevice *dev = find_device(engine, address);
  if (!dev)
    return;
  dev->is_present = FALSE;
  dev->absent_count++;
  dev->last_absent_ms = monotonic_now_ms;
  dev->device.last_seen_ms = monotonic_now_ms;
}

void btlock_policy_engine_remove_device(BtlockPolicyEngine *engine,
                                        const char *address)
{
  if (!engine || !address)
    return;
  for (guint i = 0; i < engine->device_count; i++) {
    if (strcmp(engine->devices[i].device.address, address) == 0) {
      if (engine->devices[i].rssi_filter)
        btlock_filter_free(engine->devices[i].rssi_filter);
      memmove(&engine->devices[i], &engine->devices[i + 1],
              (engine->device_count - i - 1) * sizeof(BtlockPolicyDevice));
      engine->device_count--;
      memset(&engine->devices[engine->device_count], 0, sizeof(BtlockPolicyDevice));
      return;
    }
  }
}

BtlockPolicyDecision btlock_policy_engine_evaluate(
    BtlockPolicyEngine *engine, guint64 monotonic_now_ms)
{
  if (!engine)
    return BTLOCK_POLICY_KEEP_UNLOCKED;

  /* Count present and absent enrolled devices */
  guint present = 0, absent = 0;
  for (guint i = 0; i < engine->device_count; i++) {
    BtlockPolicyDevice *dev = &engine->devices[i];
    if (!dev->enrolled)
      continue;
    if (dev->absent_count >= engine->config.min_disconnects) {
      dev->is_present = FALSE;
      absent++;
    } else {
      dev->is_present = TRUE;
      present++;
    }
  }

  if (present > 0)
    return BTLOCK_POLICY_KEEP_UNLOCKED;

  if (engine->device_count == 0 || absent == 0)
    return BTLOCK_POLICY_KEEP_UNLOCKED;

  /* Check grace period for last absent device */
  guint64 grace_end = 0;
  for (guint i = 0; i < engine->device_count; i++) {
    BtlockPolicyDevice *dev = &engine->devices[i];
    if (!dev->enrolled || dev->last_absent_ms == 0)
      continue;
    guint64 dev_grace_end = dev->last_absent_ms + engine->config.lock_grace_ms;
    if (grace_end == 0 || dev_grace_end > grace_end)
      grace_end = dev_grace_end;
  }

  if (grace_end > 0 && monotonic_now_ms < grace_end)
    return BTLOCK_POLICY_IN_GRACE;

  return BTLOCK_POLICY_ALLOW_LOCK;
}

guint btlock_policy_engine_get_trusted_count(const BtlockPolicyEngine *engine)
{
  if (!engine)
    return 0;
  guint count = 0;
  for (guint i = 0; i < engine->device_count; i++)
    if (engine->devices[i].enrolled)
      count++;
  return count;
}

guint btlock_policy_engine_get_present_count(const BtlockPolicyEngine *engine)
{
  if (!engine)
    return 0;
  guint count = 0;
  for (guint i = 0; i < engine->device_count; i++)
    if (engine->devices[i].enrolled && engine->devices[i].is_present)
      count++;
  return count;
}

const BtlockPolicyDevice *btlock_policy_engine_get_device(
    const BtlockPolicyEngine *engine, const char *address)
{
  if (!engine || !address)
    return NULL;
  for (guint i = 0; i < engine->device_count; i++)
    if (strcmp(engine->devices[i].device.address, address) == 0)
      return &engine->devices[i];
  return NULL;
}

BtlockPolicyDevice *btlock_policy_engine_get_all_devices(
    const BtlockPolicyEngine *engine, guint *count)
{
  if (!engine || !count)
    return NULL;
  *count = engine->device_count;
  BtlockPolicyDevice *result = g_new0(BtlockPolicyDevice, engine->device_count);
  if (!result)
    return NULL;
  for (guint i = 0; i < engine->device_count; i++) {
    result[i].device = engine->devices[i].device;
    result[i].is_present = engine->devices[i].is_present;
    result[i].absent_count = engine->devices[i].absent_count;
    result[i].last_present_ms = engine->devices[i].last_present_ms;
    result[i].last_absent_ms = engine->devices[i].last_absent_ms;
    result[i].enrolled = engine->devices[i].enrolled;
    result[i].rssi_filter = NULL;
  }
  return result;
}

gint64 btlock_policy_engine_get_absent_duration_ms(
    const BtlockPolicyEngine *engine, guint64 monotonic_now_ms)
{
  if (!engine)
    return -1;
  guint64 max_dur = 0;
  for (guint i = 0; i < engine->device_count; i++) {
    BtlockPolicyDevice *dev = &((BtlockPolicyEngine *)engine)->devices[i];
    if (!dev->enrolled || dev->last_absent_ms == 0)
      continue;
    guint64 dur = monotonic_now_ms - dev->last_absent_ms;
    if (dur > max_dur)
      max_dur = dur;
  }
  return max_dur > 0 ? (gint64)max_dur : -1;
}

