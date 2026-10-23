/*
 * btlock_filter.h - RSSI signal filtering API
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides filtering of noisy RSSI readings. Bluetooth signal strength
 * is inherently noisy; direct use of raw RSSI would cause flickering
 * state transitions.
 *
 * Two filters are available:
 * 1. Moving average - simple smoothing over N samples.
 * 2. Median filter - robust against outliers.
 *
 * Both are implemented as stateful filters that maintain a window
 * of recent samples.
 */

#ifndef BTLOCK_FILTER_H
#define BTLOCK_FILTER_H

#include <btlock/btlock_common.h>

/* Opaque filter handle */
typedef struct _BtlockFilter BtlockFilter;

/* Filter algorithm type */
typedef enum {
  BTLOCK_FILTER_MOVING_AVERAGE,  /* Simple moving average */
  BTLOCK_FILTER_MEDIAN,          /* Median filter (robust to outliers) */
} BtlockFilterType;

/*
 * Create a new RSSI filter.
 *
 * type: Filter algorithm to use.
 * window_size: Number of samples to retain (1 = no filtering).
 *
 * Returns: New filter instance, or NULL on error.
 */
BtlockFilter *btlock_filter_new(BtlockFilterType type, guint window_size);

/*
 * Free a filter instance.
 */
void btlock_filter_free(BtlockFilter *filter);

/*
 * Add a new RSSI sample.
 *
 * rssi: The raw RSSI reading in dBm.
 * monotonic_now_ms: Monotonic timestamp of the sample.
 *
 * Returns: TRUE if this sample was accepted, FALSE if rejected
 *          (e.g., duplicate timestamp, out-of-range value).
 */
gboolean btlock_filter_add_sample(BtlockFilter *filter,
                                   int8_t rssi,
                                   guint64 monotonic_now_ms);

/*
 * Get the filtered RSSI value.
 *
 * Returns: The current filtered RSSI, or INT8_MIN if no samples.
 */
int8_t btlock_filter_get_filtered(const BtlockFilter *filter);

/*
 * Get the raw RSSI of the most recent sample.
 *
 * Returns: Most recent RSSI, or INT8_MIN if no samples.
 */
int8_t btlock_filter_get_latest(const BtlockFilter *filter);

/*
 * Check if the device is considered "present" based on filtered RSSI.
 *
 * Returns: TRUE if filtered RSSI is above the threshold.
 */
gboolean btlock_filter_is_present(const BtlockFilter *filter,
                                   int8_t threshold_dbm);

/*
 * Get the number of samples currently in the window.
 */
guint btlock_filter_get_count(const BtlockFilter *filter);

/*
 * Clear all samples (e.g., after device reconnection).
 */
void btlock_filter_reset(BtlockFilter *filter);

/*
 * Get statistics for diagnostics.
 */
typedef struct {
  int8_t min_rssi;     /* Minimum observed RSSI */
  int8_t max_rssi;     /* Maximum observed RSSI */
  int8_t latest_rssi;  /* Most recent RSSI */
  guint sample_count;  /* Total samples added */
  guint rejected_count; /* Samples rejected as invalid */
} BtlockFilterStats;

void btlock_filter_get_stats(const BtlockFilter *filter,
                              BtlockFilterStats *stats);

#endif /* BTLOCK_FILTER_H */
