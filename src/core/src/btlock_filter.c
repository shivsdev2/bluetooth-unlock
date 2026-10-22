/*
 * btlock_filter.c - RSSI signal filtering implementation
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implements moving-average and median filters for RSSI readings.
 * Bluetooth RSSI is inherently noisy due to multipath fading,
 * interference, and device behavior. Filtering prevents flickering
 * lock/unlock decisions.
 *
 * Design notes:
 * - Samples are stored in a circular buffer.
 * - Timestamps are validated to prevent stale data.
 * - RSSI values are validated against physically possible range.
 */

#include <btlock/btlock_filter.h>
#include <stdlib.h>
#include <string.h>

#define MIN_VALID_RSSI  (-120)  /* Physically possible minimum */
#define MAX_VALID_RSSI  (20)    /* Physically possible maximum (rare) */

struct _BtlockFilter {
  BtlockFilterType type;
  int8_t *samples;       /* Circular buffer of RSSI values */
  guint64 *timestamps;   /* Corresponding monotonic timestamps */
  guint window_size;     /* Allocated buffer size */
  guint count;           /* Current number of valid samples */
  guint head;            /* Next write position (circular) */
  int8_t latest_rssi;    /* Most recent sample */
  guint64 latest_ts;     /* Most recent timestamp */
  int8_t min_rssi;       /* Running minimum */
  int8_t max_rssi;       /* Running maximum */
  guint total_samples;   /* Total samples ever added */
  guint rejected_samples;/* Total samples rejected */
};

BtlockFilter *btlock_filter_new(BtlockFilterType type, guint window_size)
{
  if (window_size == 0 || window_size > 100)
    return NULL;

  BtlockFilter *f = g_new0(BtlockFilter, 1);
  if (!f)
    return NULL;

  f->type = type;
  f->window_size = window_size;
  f->samples = g_new0(int8_t, window_size);
  f->timestamps = g_new0(guint64, window_size);
  f->latest_rssi = INT8_MIN;
  f->min_rssi = INT8_MIN;
  f->max_rssi = INT8_MIN;

  if (!f->samples || !f->timestamps) {
    btlock_filter_free(f);
    return NULL;
  }

  return f;
}

void btlock_filter_free(BtlockFilter *filter)
{
  if (!filter)
    return;

  g_free(filter->samples);
  g_free(filter->timestamps);
  g_free(filter);
}

gboolean btlock_filter_add_sample(BtlockFilter *filter, int8_t rssi,
                                  guint64 monotonic_now_ms)
{
  if (!filter)
    return FALSE;

  /* Validate RSSI range */
  if (rssi < MIN_VALID_RSSI || rssi > MAX_VALID_RSSI) {
    filter->rejected_samples++;
    return FALSE;
  }

  /* Reject stale or duplicate timestamps */
  if (monotonic_now_ms <= filter->latest_ts) {
    filter->rejected_samples++;
    return FALSE;
  }

  filter->samples[filter->head] = rssi;
  filter->timestamps[filter->head] = monotonic_now_ms;
  filter->head = (filter->head + 1) % filter->window_size;

  if (filter->count < filter->window_size)
    filter->count++;

  filter->latest_rssi = rssi;
  filter->latest_ts = monotonic_now_ms;
  filter->total_samples++;

  /* Update running min/max */
  if (filter->min_rssi == INT8_MIN || rssi < filter->min_rssi)
    filter->min_rssi = rssi;
  if (filter->max_rssi == INT8_MIN || rssi > filter->max_rssi)
    filter->max_rssi = rssi;

    return TRUE;
}

/*
 * Get filtered value using moving average.
 */
static int8_t moving_average(const BtlockFilter *filter)
{
  if (filter->count == 0)
    return INT8_MIN;

  gint64 sum = 0;
  guint start = (filter->head >= filter->count)
    ? filter->head - filter->count
    : filter->window_size - (filter->count - filter->head);

  for (guint i = 0; i < filter->count; i++) {
    guint idx = (start + i) % filter->window_size;
    sum += filter->samples[idx];
  }

  return (int8_t)(sum / (gint64)filter->count);
}

/*
 * Get filtered value using median (requires sorting a copy).
 */
static int8_t median_filter(const BtlockFilter *filter)
{
  if (filter->count == 0)
    return INT8_MIN;

  int8_t *sorted = g_new(int8_t, filter->count);
  if (!sorted)
    return filter->latest_rssi;

  guint start = (filter->head >= filter->count)
    ? filter->head - filter->count
    : filter->window_size - (filter->count - filter->head);

  for (guint i = 0; i < filter->count; i++)
    sorted[i] = filter->samples[(start + i) % filter->window_size];

  /* Simple insertion sort (count is small) */
  for (guint i = 1; i < filter->count; i++) {
    int8_t key = sorted[i];
    gint j = (gint)i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }

  int8_t median;
  if (filter->count % 2 == 0)
    median = (sorted[filter->count / 2 - 1] + sorted[filter->count / 2]) / 2;
  else
    median = sorted[filter->count / 2];

  g_free(sorted);
  return median;
}

int8_t btlock_filter_get_filtered(const BtlockFilter *filter)
{
  if (!filter || filter->count == 0)
    return INT8_MIN;

  switch (filter->type) {
    case BTLOCK_FILTER_MOVING_AVERAGE:
      return moving_average(filter);
    case BTLOCK_FILTER_MEDIAN:
      return median_filter(filter);
    default:
      return filter->latest_rssi;
  }
}

int8_t btlock_filter_get_latest(const BtlockFilter *filter)
{
  return filter ? filter->latest_rssi : INT8_MIN;
}

gboolean btlock_filter_is_present(const BtlockFilter *filter, int8_t threshold_dbm)
{
  int8_t filtered = btlock_filter_get_filtered(filter);
  if (filtered == INT8_MIN)
    return FALSE;
  return filtered > threshold_dbm;
}

guint btlock_filter_get_count(const BtlockFilter *filter)
{
  return filter ? filter->count : 0;
}

void btlock_filter_reset(BtlockFilter *filter)
{
  if (!filter)
    return;

  filter->count = 0;
  filter->head = 0;
  g_memset(filter->samples, 0, filter->window_size * sizeof(int8_t));
  g_memset(filter->timestamps, 0, filter->window_size * sizeof(guint64));
}

void btlock_filter_get_stats(const BtlockFilter *filter, BtlockFilterStats *stats)
{
  if (!filter || !stats)
    return;

  stats->min_rssi = filter->min_rssi;
  stats->max_rssi = filter->max_rssi;
  stats->latest_rssi = filter->latest_rssi;
  stats->sample_count = filter->total_samples;
  stats->rejected_count = filter->rejected_samples;
}
