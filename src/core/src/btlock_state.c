/*
 * btlock_state.c - Proximity state machine implementation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implements the deterministic finite state machine that decides
 * when to lock and when to wait for a return.
 *
 * State transition diagram (simplified):
 *
 *     START
 *       |
 *       v
 *   UNINITIALIZED --START--> BT_UNAVAILABLE/ADAPTER_UNAVAILABLE/Session...
 *       |                    OR DISCOVERING
 *       v
 *   DISCOVERING --device found--> DEVICE_PRESENT
 *   DISCOVERING --bt unavailable--> BT_UNAVAILABLE
 *   DEVICE_PRESENT --device lost--> DEPARTURE_GRACE (start timer)
 *   DEPARTURE_GRACE --grace expired--> LOCK_REQUESTED
 *   LOCK_REQUESTED --confirmed--> LOCK_CONFIRMED
 *   LOCK_REQUESTED --failed--> ERROR_DEGRADED
 *   LOCK_REQUESTED/LOCK_CONFIRMED --device returned--> RETURNING
 *   RETURNING --confirmed return--> DEVICE_PRESENT (cancel lock)
 *   DEVICE_ABSENT (same as DEVICE_PRESENT but device lost without grace
 *                    if grace period is 0)
 *
 * All transitions are logged for traceability.
 * Time is tracked in monotonic milliseconds.
 */

#include <btlock/btlock_state.h>
#include <btlock/btlock_log.h>
#include <string.h>
#include <stdlib.h>

struct _BtlockStateMachine {
  BtlockConfig config;
  BtlockState state;
  BtlockState prev_state;
  guint64 current_time_ms;         /* Last monotonic time seen */
  guint absent_count;              /* Consecutive absent readings */
  guint64 absent_since_ms;         /* When device first went absent */
  guint64 grace_end_ms;            /* When departure grace period ends */
  guint64 return_deadline_ms;      /* When return grace period expires */
  guint retry_count;               /* Consecutive retry count */
  gboolean lock_requested;         /* Whether a lock has been requested */
  gboolean lock_confirmed_flag;    /* Whether lock was confirmed */
  BtlockDevice last_device;        /* Last device event details */
  gboolean device_tracked;         /* Whether we have a tracked device */
  guint64 device_last_seen_ms;     /* Last time device was seen */
};

/* --- Internal helpers --- */

static void
sm_set_state(BtlockStateMachine *sm, BtlockState new_state,
             const char *reason)
{
  if (sm->state == new_state)
    return;

  sm->prev_state = sm->state;
  sm->state = new_state;

  btlock_debug("State transition: %s -> %s (%s)",
               btlock_state_name(sm->prev_state),
               btlock_state_name(new_state),
               reason ? reason : "unspecified");
}

static gboolean
sm_is_recovery_state(BtlockState state)
{
  switch (state) {
    case BTLOCK_STATE_BT_UNAVAILABLE:
    case BTLOCK_STATE_ADAPTER_UNAVAILABLE:
    case BTLOCK_STATE_SESSION_UNAVAILABLE:
      return TRUE;
    default:
      return FALSE;
  }
}

/* --- Public API --- */

BtlockStateMachine *
btlock_state_machine_new(const BtlockConfig *config)
{
  if (!config)
    return NULL;

  BtlockStateMachine *sm = g_new0(BtlockStateMachine, 1);
  if (!sm)
    return NULL;

  if (config->schema_version == 0) {
    /* Caller used raw malloc - init defaults */
    btlock_config_init_defaults(&sm->config);
  } else {
    sm->config = *config;
  }

  BtlockConfigError err = btlock_config_validate(&sm->config, NULL);
  if (err != BTLOCK_CONFIG_OK) {
    g_free(sm);
    return NULL;
  }

  sm->state = BTLOCK_STATE_UNINITIALIZED;
  sm->absent_count = 0;
  sm->absent_since_ms = 0;
  sm->grace_end_ms = 0;
  sm->return_deadline_ms = 0;
  sm->retry_count = 0;
  sm->lock_requested = FALSE;
  sm->lock_confirmed_flag = FALSE;
  sm->device_tracked = FALSE;
  memset(&sm->last_device, 0, sizeof(sm->last_device));

  return sm;
}

void
btlock_state_machine_free(BtlockStateMachine *sm)
{
  if (!sm)
    return;
  g_free(sm);
}

gboolean
btlock_state_machine_set_config(BtlockStateMachine *sm,
                                const BtlockConfig *config)
{
  if (!sm || !config)
    return FALSE;

  char *err_msg = NULL;
  BtlockConfigError err = btlock_config_validate(config, &err_msg);
  if (err != BTLOCK_CONFIG_OK) {
    btlock_error("Config validation failed: %s", err_msg);
    g_free(err_msg);
    return FALSE;
  }

  sm->config = *config;
  btlock_debug("Configuration updated");
  return TRUE;
}

BtlockState
btlock_state_machine_get_state(const BtlockStateMachine *sm)
{
  return sm ? sm->state : BTLOCK_STATE_UNINITIALIZED;
}

const BtlockConfig *
btlock_state_machine_get_config(const BtlockStateMachine *sm)
{
  return sm ? &sm->config : NULL;
}

gboolean
btlock_state_machine_needs_lock(const BtlockStateMachine *sm)
{
  if (!sm)
    return FALSE;

  /* Only recommend lock in states where it's the correct time */
  switch (sm->state) {
    case BTLOCK_STATE_LOCK_REQUESTED:
    case BTLOCK_STATE_DEPARTURE_GRACE:
      /* LOCK_REQUESTED means we already requested, but needs_lock
       * is used for manual trigger. Departure grace means grace
       * expired and we should lock *now* (edge case with 0 grace). */
      return sm->state == BTLOCK_STATE_DEPARTURE_GRACE;
    default:
      return FALSE;
  }
}

gboolean
btlock_state_machine_can_auto_unlock(const BtlockStateMachine *sm)
{
  if (!sm)
    return FALSE;
  return sm->config.auto_unlock_enabled;
}

guint
btlock_state_machine_get_absent_count(const BtlockStateMachine *sm)
{
  return sm ? sm->absent_count : 0;
}

guint64
btlock_state_machine_get_current_time(const BtlockStateMachine *sm)
{
    return sm ? sm->current_time_ms : 0;
}

/* ------------------------------------------------------------------ */
/* Event processing - internal helpers                                */
/* ------------------------------------------------------------------ */

static gboolean device_is_present(const BtlockStateMachine *sm)
{
  return sm->device_tracked && sm->absent_count == 0;
}

static void start_departure_grace(BtlockStateMachine *sm, guint64 now_ms)
{
  sm->absent_since_ms = now_ms;
  sm->grace_end_ms = now_ms + sm->config.lock_grace_ms;
  sm->retry_count = 0;
  sm->lock_requested = FALSE;
  sm->lock_confirmed_flag = FALSE;
  sm_set_state(sm, BTLOCK_STATE_DEPARTURE_GRACE, "device absent, starting grace");
}

static void handle_device_found(BtlockStateMachine *sm, const BtlockDevice *device,
                                guint64 now_ms)
{
  if (device) {
    memcpy(&sm->last_device, device, sizeof(BtlockDevice));
    sm->device_last_seen_ms = now_ms;
  }
  sm->device_tracked = TRUE;
  sm->absent_count = 0;

  if (sm->state == BTLOCK_STATE_LOCK_REQUESTED ||
      sm->state == BTLOCK_STATE_LOCK_CONFIRMED ||
      sm->state == BTLOCK_STATE_DEPARTURE_GRACE) {

    if (sm->lock_confirmed_flag || sm->lock_requested) {
      if (sm->config.auto_unlock_enabled) {
        sm_set_state(sm, BTLOCK_STATE_RETURNING, "device returned, auto-unlock enabled");
      } else {
        sm_set_state(sm, BTLOCK_STATE_AUTH_REQUIRED, "device returned, manual auth required");
      }
      return;
    }
    sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "device re-detected during grace");
    return;
  }

  if (sm_is_recovery_state(sm->state)) {
    sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "device found, recovery complete");
    return;
  }

  if (sm->state != BTLOCK_STATE_DEVICE_PRESENT &&
      sm->state != BTLOCK_STATE_DISCOVERING &&
      sm->state != BTLOCK_STATE_UNINITIALIZED) {
    sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "device present");
  }
}

static void handle_device_lost(BtlockStateMachine *sm, guint64 now_ms)
{
  sm->absent_count++;

  if (!sm->device_tracked)
    return;

  if (sm->state == BTLOCK_STATE_DEVICE_PRESENT) {
    if (sm->absent_count >= sm->config.min_disconnects)
      start_departure_grace(sm, now_ms);
    return;
  }

  if (sm_is_recovery_state(sm->state))
    return;

  if (sm->state == BTLOCK_STATE_DEPARTURE_GRACE ||
      sm->state == BTLOCK_STATE_LOCK_REQUESTED ||
      sm->state == BTLOCK_STATE_LOCK_CONFIRMED)
    return;

  if (sm->state == BTLOCK_STATE_RETURNING) {
      start_departure_grace(sm, now_ms);
    return;
  }
}

/*
 * Handle tick events - check for grace period expiration.
 */
static BtlockAction handle_tick(BtlockStateMachine *sm, guint64 now_ms)
{
  sm->current_time_ms = now_ms;

  if (sm->state == BTLOCK_STATE_DEPARTURE_GRACE) {
    if (now_ms >= sm->grace_end_ms) {
      sm_set_state(sm, BTLOCK_STATE_LOCK_REQUESTED, "grace period expired");
      return BTLOCK_ACTION_REQUEST_LOCK;
    }
    return BTLOCK_ACTION_NONE;
  }

  if (sm->state == BTLOCK_STATE_RETURNING) {
    if (sm->return_deadline_ms > 0 && now_ms >= sm->return_deadline_ms) {
      sm_set_state(sm, BTLOCK_STATE_DEPARTURE_GRACE, "return grace expired");
      sm->grace_end_ms = now_ms + sm->config.lock_grace_ms;
      return BTLOCK_ACTION_REQUEST_LOCK;
    }
    return BTLOCK_ACTION_NONE;
  }

  if (sm->state == BTLOCK_STATE_AUTH_REQUIRED) {
    /* Auth persists until user authenticates - no timeout */
    return BTLOCK_ACTION_NONE;
  }

  return BTLOCK_ACTION_NONE;
}

static void handle_start(BtlockStateMachine *sm)
{
  if (sm->state == BTLOCK_STATE_UNINITIALIZED) {
    sm_set_state(sm, BTLOCK_STATE_DISCOVERING, "start requested");
  }
}

static void handle_shutdown(BtlockStateMachine *sm)
{
  sm_set_state(sm, BTLOCK_STATE_SHUTTING_DOWN, "shutdown requested");
}

/*
 * Main event processing entry point.
 */
BtlockEventResult
btlock_state_machine_process_event(BtlockStateMachine *sm,
                                   BtlockEvent event,
                                   const BtlockDevice *device,
                                   guint64 monotonic_now_ms)
{
  BtlockEventResult result = {
    .action = BTLOCK_ACTION_NONE,
    .state_changed = FALSE,
    .new_state = sm->state,
  };

  if (!sm)
    return result;

  if (event == BTLOCK_EVENT_TICK)
    sm->current_time_ms = monotonic_now_ms;

  BtlockState old_state = sm->state;

  switch (event) {
    case BTLOCK_EVENT_START:
      handle_start(sm);
      break;

    case BTLOCK_EVENT_SHUTDOWN:
      handle_shutdown(sm);
      break;

    case BTLOCK_EVENT_DEVICE_FOUND:
      handle_device_found(sm, device, monotonic_now_ms);
      break;

    case BTLOCK_EVENT_DEVICE_LOST:
      handle_device_lost(sm, monotonic_now_ms);
      break;

    case BTLOCK_EVENT_RSSI_UPDATE:
      if (device) {
        memcpy(&sm->last_device, device, sizeof(BtlockDevice));
        sm->device_last_seen_ms = monotonic_now_ms;
        sm->absent_count = 0;
        if (sm->state == BTLOCK_STATE_DEPARTURE_GRACE) {
          sm->device_tracked = TRUE;
        }
      }
      break;

    case BTLOCK_EVENT_TICK:
      result.action = handle_tick(sm, monotonic_now_ms);
      break;

    case BTLOCK_EVENT_BT_AVAILABLE:
      sm_set_state(sm, BTLOCK_STATE_DISCOVERING, "Bluetooth available");
      break;

    case BTLOCK_EVENT_BT_UNAVAILABLE:
      sm_set_state(sm, BTLOCK_STATE_BT_UNAVAILABLE, "Bluetooth unavailable");
      break;

    case BTLOCK_EVENT_ADAPTER_AVAILABLE:
      sm_set_state(sm, BTLOCK_STATE_DISCOVERING, "Adapter available");
      break;

    case BTLOCK_EVENT_ADAPTER_UNAVAILABLE:
      sm_set_state(sm, BTLOCK_STATE_ADAPTER_UNAVAILABLE, "Adapter unavailable");
      break;

    case BTLOCK_EVENT_SESSION_AVAILABLE:
      if (sm_is_recovery_state(sm->state) || sm->state == BTLOCK_STATE_UNINITIALIZED) {
        if (device_is_present(sm))
          sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "Session available, device present");
        else
          sm_set_state(sm, BTLOCK_STATE_DISCOVERING, "Session available");
      }
      break;

    case BTLOCK_EVENT_SESSION_UNAVAILABLE:
      sm_set_state(sm, BTLOCK_STATE_SESSION_UNAVAILABLE, "Session unavailable");
      break;

    case BTLOCK_EVENT_LOCK_CONFIRMED:
      sm_set_state(sm, BTLOCK_STATE_LOCK_CONFIRMED, "Lock confirmed by session");
      sm->lock_confirmed_flag = TRUE;
      break;

    case BTLOCK_EVENT_LOCK_FAILED:
      sm->retry_count++;
      if (sm->retry_count >= sm->config.max_retries) {
        sm_set_state(sm, BTLOCK_STATE_ERROR_DEGRADED, "Lock failed, max retries exceeded");
      }
      break;

    case BTLOCK_EVENT_SESSION_LOCKED:
      sm->lock_confirmed_flag = TRUE;
      if (sm->state == BTLOCK_STATE_LOCK_REQUESTED)
        sm_set_state(sm, BTLOCK_STATE_LOCK_CONFIRMED, "External lock detected");
      break;

    case BTLOCK_EVENT_SESSION_UNLOCKED:
      sm->lock_confirmed_flag = FALSE;
      sm->lock_requested = FALSE;
      if (sm->state == BTLOCK_STATE_LOCK_CONFIRMED ||
          sm->state == BTLOCK_STATE_AUTH_REQUIRED ||
          sm->state == BTLOCK_STATE_RETURNING) {
        sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "Session unlocked externally");
        sm->absent_count = 0;
      }
      break;

    case BTLOCK_EVENT_AUTH_REQUIRED:
      sm_set_state(sm, BTLOCK_STATE_AUTH_REQUIRED, "Auth required for unlock");
      break;

    case BTLOCK_EVENT_AUTH_SUCCESS:
      sm_set_state(sm, BTLOCK_STATE_DEVICE_PRESENT, "Auth success");
      sm->lock_requested = FALSE;
      sm->lock_confirmed_flag = FALSE;
      break;

    case BTLOCK_EVENT_STOP:
      sm_set_state(sm, BTLOCK_STATE_UNINITIALIZED, "Stop requested");
      sm->absent_count = 0;
      sm->lock_requested = FALSE;
      sm->lock_confirmed_flag = FALSE;
      break;
  }

  result.state_changed = (sm->state != old_state);
  result.new_state = sm->state;
  return result;
}

