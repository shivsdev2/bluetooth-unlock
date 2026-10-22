/*
 * btlock_state.h - Proximity state machine API
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This is the core state machine that decides when to lock and when
 * to wait for return. It is a pure-logic module: it takes events and
 * monotonic timestamps, and produces decisions. No I/O is performed.
 *
 * Design rationale:
 * - Separating state logic from I/O makes the machine fully testable
 *   with deterministic unit tests that don't require hardware or D-Bus.
 * - The machine tracks a single "primary" device decision but supports
 *   multiple enrolled devices through the policy layer.
 * - All time decisions use monotonic milliseconds to be immune to
 *   clock changes, NTP adjustments, or timezone shifts.
 */

#ifndef BTLOCK_STATE_H
#define BTLOCK_STATE_H

#include <btlock/btlock_common.h>
#include <btlock/btlock_config.h>

/* Opaque state machine handle */
typedef struct _BtlockStateMachine BtlockStateMachine;

/*
 * Decisions the state machine can produce after processing an event.
 * The caller (daemon) acts on these decisions via D-Bus / session.
 */
typedef enum {
  BTLOCK_ACTION_NONE = 0,             /* No action needed */
  BTLOCK_ACTION_REQUEST_LOCK,         /* Send lock request to session manager */
  BTLOCK_ACTION_REQUEST_UNLOCK,       /* Request unlock (requires manual auth) */
  BTLOCK_ACTION_CANCEL_LOCK,          /* Cancel pending lock (device returned) */
  BTLOCK_ACTION_NOTIFY,               /* Emit notification to UI */
  BTLOCK_ACTION_START_DISCOVERY,      /* Begin Bluetooth discovery */
  BTLOCK_ACTION_STOP_DISCOVERY,       /* Stop discovery (power saving) */
} BtlockAction;

/*
 * Event result - return value from processing an event.
 */
typedef struct {
  BtlockAction action;                /* Action to take (or NONE) */
  gboolean state_changed;            /* Whether the state actually changed */
  BtlockState new_state;             /* New state (or unchanged) */
} BtlockEventResult;

/*
 * Callback for decisions. Called from btlock_state_machine_process_event().
 * The callback can initiate lock/unlock operations.
 *
 * Parameters:
 *   action   - The action to perform
 *   device   - The device associated with the action (may be NULL)
 *   user_data - Opaque user data passed to the machine
 */
typedef void (*BtlockDecisionFunc)(BtlockAction action,
                                    const BtlockDevice *device,
                                    void *user_data);

/*
 * Create a new state machine.
 *
 * config: Configuration to apply. The machine copies the config.
 * Returns: A new state machine instance, or NULL on error.
 */
BtlockStateMachine *btlock_state_machine_new(const BtlockConfig *config);

/*
 * Free a state machine instance.
 */
void btlock_state_machine_free(BtlockStateMachine *sm);

/*
 * Set (or replace) the configuration.
 * Validates the config before applying.
 */
gboolean btlock_state_machine_set_config(BtlockStateMachine *sm,
                                          const BtlockConfig *config);

/*
 * Get the current state.
 */
BtlockState btlock_state_machine_get_state(const BtlockStateMachine *sm);

/*
 * Get the current configuration.
 */
const BtlockConfig *btlock_state_machine_get_config(const BtlockStateMachine *sm);

/*
 * Check if a lock is currently needed (e.g., for manual lock trigger).
 */
gboolean btlock_state_machine_needs_lock(const BtlockStateMachine *sm);

/*
 * Check if an unlock is allowed under current policy.
 * Returns FALSE if auto-unlock is disabled (the default).
 */
gboolean btlock_state_machine_can_auto_unlock(const BtlockStateMachine *sm);

/*
 * Get the number of consecutive absent device readings.
 * Used for diagnostics.
 */
guint btlock_state_machine_get_absent_count(const BtlockStateMachine *sm);

/*
 * Get the current monotonic time stored by the machine.
 * Returns 0 if never set.
 */
guint64 btlock_state_machine_get_current_time(const BtlockStateMachine *sm);

/*
 * Process an event. Returns the result including any action needed.
 *
 * For BTLOCK_EVENT_DEVICE_FOUND / BTLOCK_EVENT_DEVICE_LOST /
 * BTLOCK_EVENT_RSSI_UPDATE, pass the device via device param.
 * For BTLOCK_EVENT_TICK, pass monotonic_now_ms as the time.
 *
 * All other device params may be NULL.
 */
BtlockEventResult btlock_state_machine_process_event(
    BtlockStateMachine *sm,
    BtlockEvent event,
    const BtlockDevice *device,
    guint64 monotonic_now_ms);

#endif /* BTLOCK_STATE_H */
