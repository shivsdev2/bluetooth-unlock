/*
 * btlock_policy.h - Policy engine API
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The policy engine evaluates the collective state of multiple tracked
 * devices against the configured policy to determine whether the
 * workstation should lock.
 *
 * Key concepts:
 * - Each enrolled device has its own state (present/absent, filtered RSSI).
 * - The policy determines the "aggregate" decision.
 * - For a single device: lock when absent for grace period.
 * - For multiple devices: lock only when ALL enrolled devices are absent
 *   (if required_devices > 0), or when the last remaining device departs.
 */

#ifndef BTLOCK_POLICY_H
#define BTLOCK_POLICY_H

#include <btlock/btlock_common.h>
#include <btlock/btlock_config.h>
#include <btlock/btlock_filter.h>

/* Opaque policy engine handle */
typedef struct _BtlockPolicyEngine BtlockPolicyEngine;

/*
 * Individual device tracking state within the policy engine.
 */
typedef struct {
  BtlockDevice device;          /* Device identity and current reading */
  BtlockFilter *rssi_filter;    /* RSSI filter for this device */
  gboolean is_present;          /* Whether device is currently "present" */
  guint absent_count;           /* Consecutive absent readings */
  guint64 last_present_ms;      /* Last time device was present */
  guint64 last_absent_ms;       /* Last time device was absent */
  gboolean enrolled;            /* Whether device is enrolled as trusted */
} BtlockPolicyDevice;

/*
 * Aggregate policy decision.
 */
typedef enum {
  BTLOCK_POLICY_ALLOW_LOCK,      /* All criteria met - lock the workstation */
  BTLOCK_POLICY_KEEP_UNLOCKED,   /* At least one device is nearby - don't lock */
  BTLOCK_POLICY_IN_GRACE,        /* In departure grace period - don't lock yet */
  BTLOCK_POLICY_AWAIT_RETURN,    /* In return grace period - cancel pending lock */
} BtlockPolicyDecision;

/*
 * Create a new policy engine.
 *
 * config: The policy configuration to use. Copied internally.
 *
 * Returns: New engine instance, or NULL on error.
 */
BtlockPolicyEngine *btlock_policy_engine_new(const BtlockConfig *config);

/*
 * Free a policy engine and all tracked device state.
 */
void btlock_policy_engine_free(BtlockPolicyEngine *engine);

/*
 * Set or replace the policy configuration.
 */
void btlock_policy_engine_set_config(BtlockPolicyEngine *engine,
                                      const BtlockConfig *config);

/*
 * Add or update a tracked device.
 *
 * If a device with the same address already exists, it is updated.
 * Otherwise a new entry is created.
 *
 * device: Device information (address and name must be set).
 * rssi: Current RSSI reading (may be INT8_MIN if no signal).
 * is_trusted: Whether this device is enrolled as a trusted device.
 * monotonic_now_ms: Current monotonic time.
 *
 * Returns: The device entry that was added/updated.
 */
BtlockPolicyDevice *btlock_policy_engine_update_device(
    BtlockPolicyEngine *engine,
    const char *address,
    const char *name,
    int8_t rssi,
    gboolean is_trusted,
    guint64 monotonic_now_ms);

/*
 * Mark a device as lost (not seen in last scan cycle).
 * This increments the absent counter and updates last_absent_ms.
 */
void btlock_policy_engine_mark_absent(BtlockPolicyEngine *engine,
                                       const char *address,
                                       guint64 monotonic_now_ms);

/*
 * Remove a device from tracking (e.g., unenrolled).
 */
void btlock_policy_engine_remove_device(BtlockPolicyEngine *engine,
                                         const char *address);

/*
 * Evaluate the current policy and return a decision.
 *
 * monotonic_now_ms: Current monotonic time.
 *
 * Returns: The policy decision for the current state.
 */
BtlockPolicyDecision btlock_policy_engine_evaluate(
    BtlockPolicyEngine *engine, guint64 monotonic_now_ms);

/*
 * Get the number of currently tracked (enrolled) devices.
 */
guint btlock_policy_engine_get_trusted_count(const BtlockPolicyEngine *engine);

/*
 * Get the number of currently present (nearby) trusted devices.
 */
guint btlock_policy_engine_get_present_count(const BtlockPolicyEngine *engine);

/*
 * Get a device entry by address (for diagnostics).
 * Returns NULL if not found.
 */
const BtlockPolicyDevice *btlock_policy_engine_get_device(
    const BtlockPolicyEngine *engine, const char *address);

/*
 * Get diagnostic snapshot of all tracked devices.
 * Caller must free the returned array.
 */
BtlockPolicyDevice *btlock_policy_engine_get_all_devices(
    const BtlockPolicyEngine *engine, guint *count);

/*
 * Get the absent duration for the lock decision.
 * This is the time since the last present state of the "last remaining"
 * device, used to determine if the lock grace period has elapsed.
 *
 * Returns: -1 if no applicable device.
 */
gint64 btlock_policy_engine_get_absent_duration_ms(
    const BtlockPolicyEngine *engine, guint64 monotonic_now_ms);

#endif /* BTLOCK_POLICY_H */
