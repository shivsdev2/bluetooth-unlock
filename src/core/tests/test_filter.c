/*
 * test_filter.c - Unit tests for the RSSI signal filter
 *
 * Copyright (C) 2025 btlock contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <btlock/btlock_filter.h>
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

static gboolean test_moving_average_simple(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 5);
  ASSERT(f != NULL, "filter creation failed");
  btlock_filter_add_sample(f, -60, 1000);
  ASSERT(btlock_filter_get_filtered(f) == -60, "single sample should be exact");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_moving_average_multi(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 5);
  btlock_filter_add_sample(f, -50, 1000);
  btlock_filter_add_sample(f, -52, 2000);
  btlock_filter_add_sample(f, -54, 3000);
  int8_t avg = btlock_filter_get_filtered(f);
  ASSERT(avg == -52, "moving average of -50,-52,-54 should be -52");
  ASSERT(btlock_filter_get_count(f) == 3, "should have 3 samples");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_median_filter(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MEDIAN, 5);
  btlock_filter_add_sample(f, -50, 1000);
  btlock_filter_add_sample(f, -80, 2000);  /* Outlier */
  btlock_filter_add_sample(f, -52, 3000);
  btlock_filter_add_sample(f, -53, 4000);
  btlock_filter_add_sample(f, -49, 5000);
  int8_t med = btlock_filter_get_filtered(f);
    ASSERT(med == -52, "median should be -52 (robust to outlier)");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_empty_filter(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 5);
  ASSERT(btlock_filter_get_filtered(f) == INT8_MIN, "empty should be INT8_MIN");
  ASSERT(btlock_filter_get_latest(f) == INT8_MIN, "empty latest INT8_MIN");
  ASSERT(btlock_filter_get_count(f) == 0, "empty count 0");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_invalid_rssi_rejected(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 5);
    /* Test with int8_t that overflows when negative */
  int8_t invalid_rssi = (int8_t)-200;
  ASSERT(btlock_filter_add_sample(f, invalid_rssi, 1000) == FALSE, "invalid RSSI rejected");
  ASSERT(btlock_filter_add_sample(f, -60, 1000) == TRUE, "valid RSSI accepted");
  ASSERT(btlock_filter_add_sample(f, -60, 1000) == FALSE, "dup timestamp rejected");
  ASSERT(btlock_filter_get_count(f) == 1, "should have 1 sample");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_reset(void)
{
  BtlockFilter *f = btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 5);
  btlock_filter_add_sample(f, -60, 1000);
  btlock_filter_add_sample(f, -62, 2000);
  ASSERT(btlock_filter_get_count(f) == 2, "should have 2 samples");
  btlock_filter_reset(f);
  ASSERT(btlock_filter_get_count(f) == 0, "count 0 after reset");
  ASSERT(btlock_filter_get_filtered(f) == INT8_MIN, "INT8_MIN after reset");
  btlock_filter_free(f);
  return TRUE;
}

static gboolean test_window_validation(void)
{
  ASSERT(btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 0) == NULL,
         "window size 0 should fail");
  ASSERT(btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 100) != NULL,
         "window size 100 should work");
  ASSERT(btlock_filter_new(BTLOCK_FILTER_MOVING_AVERAGE, 101) == NULL,
         "window size 101 should fail");
  return TRUE;
}

static gboolean test_null_safety(void)
{
  ASSERT(btlock_filter_get_filtered(NULL) == INT8_MIN, "NULL filtered");
  ASSERT(btlock_filter_get_latest(NULL) == INT8_MIN, "NULL latest");
  ASSERT(btlock_filter_get_count(NULL) == 0, "NULL count");
  ASSERT(btlock_filter_is_present(NULL, -80) == FALSE, "NULL is_present");
  btlock_filter_free(NULL);
  btlock_filter_reset(NULL);
  btlock_filter_get_stats(NULL, NULL);
  return TRUE;
}

typedef gboolean (*TestFunc)(void);

static TestFunc filter_tests[] = {
  test_moving_average_simple,
  test_moving_average_multi,
  test_median_filter,
  test_empty_filter,
  test_invalid_rssi_rejected,
  test_reset,
  test_window_validation,
  test_null_safety,
  NULL,
};

int main(void)
{
  g_print("Running %d filter tests...\n",
          (int)(sizeof(filter_tests) / sizeof(filter_tests[0]) - 1));
  for (int i = 0; filter_tests[i] != NULL; i++) {
    gboolean ok = filter_tests[i]();
    if (!ok)
      g_printerr("Test %d FAILED\n", i);
  }
  g_print("\nResults: %d passed, %d failed (of %d)\n",
          tests_passed, tests_failed, tests_run);
  return tests_failed > 0 ? 1 : 0;
}

