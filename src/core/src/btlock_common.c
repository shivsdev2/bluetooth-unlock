/*
 * btlock_common.c - Common utilities implementation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_common.h>
#include <string.h>
#include <stdio.h>

const char *btlock_state_name(BtlockState state)
{
  switch (state) {
    case BTLOCK_STATE_UNINITIALIZED:       return "UNINITIALIZED";
    case BTLOCK_STATE_DISCOVERING:         return "DISCOVERING";
    case BTLOCK_STATE_DEVICE_PRESENT:      return "DEVICE_PRESENT";
    case BTLOCK_STATE_DEVICE_ABSENT:       return "DEVICE_ABSENT";
    case BTLOCK_STATE_DEPARTURE_GRACE:     return "DEPARTURE_GRACE";
    case BTLOCK_STATE_LOCK_REQUESTED:      return "LOCK_REQUESTED";
    case BTLOCK_STATE_LOCK_CONFIRMED:      return "LOCK_CONFIRMED";
    case BTLOCK_STATE_RETURNING:           return "RETURNING";
    case BTLOCK_STATE_RECONNECTING:        return "RECONNECTING";
    case BTLOCK_STATE_BT_UNAVAILABLE:      return "BT_UNAVAILABLE";
    case BTLOCK_STATE_ADAPTER_UNAVAILABLE: return "ADAPTER_UNAVAILABLE";
    case BTLOCK_STATE_SESSION_UNAVAILABLE: return "SESSION_UNAVAILABLE";
    case BTLOCK_STATE_AUTH_REQUIRED:       return "AUTH_REQUIRED";
    case BTLOCK_STATE_ERROR_DEGRADED:      return "ERROR_DEGRADED";
    case BTLOCK_STATE_SHUTTING_DOWN:      return "SHUTTING_DOWN";
    case BTLOCK_STATE_COUNT:               return "COUNT_SENTINEL";
    default:                               return "UNKNOWN";
  }
}

const char *btlock_event_name(BtlockEvent event)
{
  switch (event) {
    case BTLOCK_EVENT_DEVICE_FOUND:         return "DEVICE_FOUND";
    case BTLOCK_EVENT_DEVICE_LOST:          return "DEVICE_LOST";
    case BTLOCK_EVENT_RSSI_UPDATE:          return "RSSI_UPDATE";
    case BTLOCK_EVENT_BT_AVAILABLE:         return "BT_AVAILABLE";
    case BTLOCK_EVENT_BT_UNAVAILABLE:       return "BT_UNAVAILABLE";
    case BTLOCK_EVENT_ADAPTER_AVAILABLE:    return "ADAPTER_AVAILABLE";
    case BTLOCK_EVENT_ADAPTER_UNAVAILABLE:  return "ADAPTER_UNAVAILABLE";
    case BTLOCK_EVENT_SESSION_AVAILABLE:    return "SESSION_AVAILABLE";
    case BTLOCK_EVENT_SESSION_UNAVAILABLE:  return "SESSION_UNAVAILABLE";
    case BTLOCK_EVENT_LOCK_CONFIRMED:       return "LOCK_CONFIRMED";
    case BTLOCK_EVENT_LOCK_FAILED:          return "LOCK_FAILED";
    case BTLOCK_EVENT_SESSION_LOCKED:       return "SESSION_LOCKED";
    case BTLOCK_EVENT_SESSION_UNLOCKED:     return "SESSION_UNLOCKED";
    case BTLOCK_EVENT_AUTH_REQUIRED:        return "AUTH_REQUIRED";
    case BTLOCK_EVENT_AUTH_SUCCESS:         return "AUTH_SUCCESS";
    case BTLOCK_EVENT_START:                return "START";
    case BTLOCK_EVENT_STOP:                 return "STOP";
    case BTLOCK_EVENT_SHUTDOWN:             return "SHUTDOWN";
    case BTLOCK_EVENT_TICK:                 return "TICK";
    default:                                return "UNKNOWN";
  }
}

/*
 * Validate a Bluetooth MAC address.
 * Format: "XX:XX:XX:XX:XX:XX" where X is hex.
 * Also accepts lowercase.
 */
gboolean btlock_address_is_valid(const char *addr)
{
  if (!addr || strlen(addr) != 17)
    return FALSE;

  for (int i = 0; i < 17; i++) {
    if (i % 3 == 2) {
      if (addr[i] != ':')
        return FALSE;
    } else {
      char c = addr[i];
      if (!((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f')))
        return FALSE;
    }
  }
  return TRUE;
}
