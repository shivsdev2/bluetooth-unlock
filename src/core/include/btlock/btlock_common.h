/*
 * btlock_common.h - Common types and definitions for btlock-core
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This header defines the fundamental types shared across all btlock-core
 * modules: state identifiers, event types, device representation, and
 * basic utility macros.
 *
 * Trust boundary: This header is the stable C ABI boundary.
 * The state machine is intentionally pure logic with no I/O.
 */

#ifndef BTLOCK_COMMON_H
#define BTLOCK_COMMON_H

#include <glib.h>

/* Compile-time assertion helper */
#define BTLOCK_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

/* Maximum lengths matching Bluetooth specification */
#define BTLOCK_ADDR_MAX 18   /* "AA:BB:CC:DD:EE:FF" + NUL */
#define BTLOCK_NAME_MAX 248  /* Bluetooth EIR name max */

/*
 * BtlockState - All possible states for the proximity state machine.
 *
 * The machine starts in UNINITIALIZED and transitions through these
 * states based on events and timeouts. Each state has well-defined
 * entry/exit actions and allowed transitions (see btlock_state.c).
 */
typedef enum {
  BTLOCK_STATE_UNINITIALIZED = 0,     /* Not yet started */
  BTLOCK_STATE_DISCOVERING,           /* Scanning for devices */
  BTLOCK_STATE_DEVICE_PRESENT,        /* At least one trusted device nearby */
  BTLOCK_STATE_DEVICE_ABSENT,         /* No trusted device detected, no grace */
  BTLOCK_STATE_DEPARTURE_GRACE,       /* Device lost, grace period before lock */
  BTLOCK_STATE_LOCK_REQUESTED,        /* Lock command sent, awaiting confirmation */
  BTLOCK_STATE_LOCK_CONFIRMED,        /* Lock confirmed by session manager */
  BTLOCK_STATE_RETURNING,             /* Device returned during departure */
  BTLOCK_STATE_RECONNECTING,          /* Device reconnecting after lock */
  BTLOCK_STATE_BT_UNAVAILABLE,        /* Bluetooth subsystem not available */
  BTLOCK_STATE_ADAPTER_UNAVAILABLE,   /* No Bluetooth adapter found */
  BTLOCK_STATE_SESSION_UNAVAILABLE,   /* No session manager for locking */
  BTLOCK_STATE_AUTH_REQUIRED,         /* User authentication needed for unlock */
  BTLOCK_STATE_ERROR_DEGRADED,        /* Recoverable error, degraded operation */
  BTLOCK_STATE_SHUTTING_DOWN,         /* Application is shutting down */
  BTLOCK_STATE_COUNT,                 /* Sentinel for array sizing */
} BtlockState;

/*
 * BtlockEvent - Events that drive state transitions.
 *
 * Events carry optional data (device info, timestamps). The state
 * machine processes events synchronously and returns whether the
 * event triggered an actionable decision (lock/unlock needed).
 */
typedef enum {
  /* Device events */
  BTLOCK_EVENT_DEVICE_FOUND,          /* A trusted device was detected */
  BTLOCK_EVENT_DEVICE_LOST,           /* A trusted device was not seen */
  BTLOCK_EVENT_RSSI_UPDATE,           /* New RSSI reading for a device */

  /* Bluetooth subsystem events */
  BTLOCK_EVENT_BT_AVAILABLE,          /* Bluetooth is available again */
  BTLOCK_EVENT_BT_UNAVAILABLE,        /* Bluetooth subsystem unavailable */
  BTLOCK_EVENT_ADAPTER_AVAILABLE,     /* Adapter appeared */
  BTLOCK_EVENT_ADAPTER_UNAVAILABLE,   /* Adapter disappeared or replaced */

  /* Session / lock events */
  BTLOCK_EVENT_SESSION_AVAILABLE,     /* Session manager ready */
  BTLOCK_EVENT_SESSION_UNAVAILABLE,   /* Session manager unavailable */
  BTLOCK_EVENT_LOCK_CONFIRMED,        /* Lock was confirmed by session */
  BTLOCK_EVENT_LOCK_FAILED,           /* Lock request failed */
  BTLOCK_EVENT_SESSION_LOCKED,        /* Session was locked (by anyone) */
  BTLOCK_EVENT_SESSION_UNLOCKED,      /* Session was unlocked (by anyone) */

  /* Authentication events */
  BTLOCK_EVENT_AUTH_REQUIRED,         /* Manual auth required for unlock */
  BTLOCK_EVENT_AUTH_SUCCESS,          /* User authenticated successfully */

  /* Control events */
  BTLOCK_EVENT_START,                 /* Start monitoring */
  BTLOCK_EVENT_STOP,                  /* Stop monitoring */
  BTLOCK_EVENT_SHUTDOWN,              /* Application shutting down */
  BTLOCK_EVENT_TICK,                  /* Periodic timer tick (ms time advances) */
} BtlockEvent;

/*
 * BtlockDevice - A tracked Bluetooth device.
 *
 * The address is the device's Bluetooth MAC. For privacy, we store
 * only what is needed: address, name, and last signal. We do NOT store
 * any persistent unique identifier beyond the MAC (which can change
 * with privacy addressing - see docs/BLUETOOTH.md).
 */
typedef struct {
  char address[BTLOCK_ADDR_MAX];      /* Bluetooth MAC address */
  char name[BTLOCK_NAME_MAX];         /* Device name (may be empty) */
  int8_t rssi;                        /* Most recent RSSI in dBm */
  gboolean paired;                    /* Whether device is paired in BlueZ */
  gboolean connected;                 /* Whether currently connected */
  guint64 last_seen_ms;               /* Monotonic time of last detection */
  guint64 last_rssi_update_ms;        /* Monotonic time of last RSSI update */
} BtlockDevice;

/*
 * BtlockDeviceList - Dynamic array of tracked devices.
 *
 * Used by the policy engine to evaluate multi-device policies.
 * All devices in this list are considered "trusted" (enrolled by user).
 */
typedef struct {
  BtlockDevice *devices;  /* Array of devices */
  guint count;            /* Number of devices in array */
  gsize size;             /* Allocated array size */
} BtlockDeviceList;

/* String representations for logging/debugging */
const char *btlock_state_name(BtlockState state) G_GNUC_PURE;
const char *btlock_event_name(BtlockEvent event) G_GNUC_PURE;

/* Validate a Bluetooth MAC address format (XX:XX:XX:XX:XX:XX) */
gboolean btlock_address_is_valid(const char *addr) G_GNUC_PURE;

#endif /* BTLOCK_COMMON_H */
