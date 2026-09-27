//
// FILE            corProm.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Ken Zangelin
//
// SPDX-License-Identifier: Apache-2.0
//
#include <stdio.h>                               // snprintf
#include <stdlib.h>                              // malloc, free
#include <string.h>                              // strdup, strcmp
#include <stdatomic.h>                           // atomic operations
#include <math.h>                                // INFINITY

#include "kalloc/KAlloc.h"                          // KAlloc
#include "corTree/CorNode.h"                        // CorNode
#include "corTree/corTreeBuilder.h"                  // corTreeArray, corTreeObject, corTreeString, corTreeFloat, corTreeInteger

#include "corProm/corProm.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// CorPromType - metric type
//
typedef enum CorPromType
{
  CorPromTypeCounter,
  CorPromTypeGauge,
  CorPromTypeHistogram
} CorPromType;



// -----------------------------------------------------------------------------
//
// CorPromHistogram - histogram-specific data
//
typedef struct CorPromHistogram
{
  double*                bucketBounds;  // Array of bucket upper bounds (last is +Inf)
  atomic_int_fast64_t*   bucketCounts;  // Array of bucket counts
  int                    bucketCount;   // Number of buckets
  atomic_int_fast64_t    sum;           // Sum of all observed values (fixed-point)
  atomic_int_fast64_t    count;         // Count of observations
} CorPromHistogram;



// -----------------------------------------------------------------------------
//
// CorPromValue - union for metric values
//
typedef union CorPromValue
{
  atomic_int_fast64_t    iValue;        // Counter/Gauge: fixed-point (value * 1000)
  CorPromHistogram         histogram;     // Histogram data
} CorPromValue;



// -----------------------------------------------------------------------------
//
// CorPromMetric - a single metric (counter, gauge, or histogram)
//
// For counters and gauges, value is stored as fixed-point (value * 1000).
// For histograms, a separate struct holds buckets and counts.
//
struct CorPromMetric
{
  char*                  name;          // Metric name (e.g., "http_requests_total")
  char*                  help;          // Description
  CorPromType              type;          // Counter, Gauge, or Histogram
  CorPromValue             value;         // Type-specific value
  struct CorPromMetric*    next;          // Linked list
};



// -----------------------------------------------------------------------------
//
// Global list of metrics
//
static CorPromMetric* metricList = NULL;



// -----------------------------------------------------------------------------
//
// metricCreate - create a new metric (internal helper)
//
static CorPromMetric* metricCreate(const char* name, const char* help, CorPromType type)
{
  CorPromMetric* metric = (CorPromMetric*) malloc(sizeof(CorPromMetric));

  if (metric == NULL)
    return NULL;

  metric->name = strdup(name);
  metric->help = (help != NULL) ? strdup(help) : NULL;
  metric->type = type;
  metric->next = NULL;

  // Initialize value to zero
  atomic_init(&metric->value.iValue, 0);

  // Add to list
  if (metricList == NULL)
    metricList = metric;
  else
  {
    CorPromMetric* last = metricList;
    while (last->next != NULL)
      last = last->next;
    last->next = metric;
  }

  return metric;
}



// -----------------------------------------------------------------------------
//
// corPromCounterCreate - create a new counter metric
//
CorPromMetric* corPromCounterCreate(const char* name, const char* help)
{
  return metricCreate(name, help, CorPromTypeCounter);
}



// -----------------------------------------------------------------------------
//
// corPromGaugeCreate - create a new gauge metric
//
CorPromMetric* corPromGaugeCreate(const char* name, const char* help)
{
  return metricCreate(name, help, CorPromTypeGauge);
}



// -----------------------------------------------------------------------------
//
// corPromHistogramCreate - create a new histogram metric
//
CorPromMetric* corPromHistogramCreate(const char* name, const char* help, double* buckets, int bucketCount)
{
  CorPromMetric* metric = metricCreate(name, help, CorPromTypeHistogram);

  if (metric == NULL)
    return NULL;

  // +1 for +Inf bucket
  metric->value.histogram.bucketCount  = bucketCount + 1;
  metric->value.histogram.bucketBounds = (double*) malloc((bucketCount + 1) * sizeof(double));
  metric->value.histogram.bucketCounts = (atomic_int_fast64_t*) malloc((bucketCount + 1) * sizeof(atomic_int_fast64_t));

  // Fill bucket bounds
  for (int i = 0; i < bucketCount; i++)
  {
    metric->value.histogram.bucketBounds[i] = buckets[i];
    atomic_init(&metric->value.histogram.bucketCounts[i], 0);
  }

  // +Inf bucket
  metric->value.histogram.bucketBounds[bucketCount] = INFINITY;
  atomic_init(&metric->value.histogram.bucketCounts[bucketCount], 0);

  // Initialize sum and count
  atomic_init(&metric->value.histogram.sum, 0);
  atomic_init(&metric->value.histogram.count, 0);

  return metric;
}



// -----------------------------------------------------------------------------
//
// corPromCounterInc - increment a counter by 1 (atomic, lock-free)
//
void corPromCounterInc(CorPromMetric* counter)
{
  if (counter == NULL || counter->type != CorPromTypeCounter)
    return;

  atomic_fetch_add(&counter->value.iValue, 1000);  // 1.000 in fixed-point
}



// -----------------------------------------------------------------------------
//
// corPromCounterAdd - add a value to a counter
//
void corPromCounterAdd(CorPromMetric* counter, int64_t value)
{
  if (counter == NULL || counter->type != CorPromTypeCounter)
    return;

  atomic_fetch_add(&counter->value.iValue, value * 1000);
}



// -----------------------------------------------------------------------------
//
// corPromGaugeSet - set a gauge to a specific value
//
void corPromGaugeSet(CorPromMetric* gauge, double value)
{
  if (gauge == NULL || gauge->type != CorPromTypeGauge)
    return;

  atomic_store(&gauge->value.iValue, (int64_t)(value * 1000.0));
}



// -----------------------------------------------------------------------------
//
// corPromGaugeAdd - add to a gauge
//
void corPromGaugeAdd(CorPromMetric* gauge, double value)
{
  if (gauge == NULL || gauge->type != CorPromTypeGauge)
    return;

  atomic_fetch_add(&gauge->value.iValue, (int64_t)(value * 1000.0));
}



// -----------------------------------------------------------------------------
//
// corPromGaugeSub - subtract from a gauge
//
void corPromGaugeSub(CorPromMetric* gauge, double value)
{
  if (gauge == NULL || gauge->type != CorPromTypeGauge)
    return;

  atomic_fetch_sub(&gauge->value.iValue, (int64_t)(value * 1000.0));
}



// -----------------------------------------------------------------------------
//
// corPromHistogramObserve - record an observation in a histogram
//
void corPromHistogramObserve(CorPromMetric* histogram, double value)
{
  if (histogram == NULL || histogram->type != CorPromTypeHistogram)
    return;

  CorPromHistogram* h = &histogram->value.histogram;

  // Increment the appropriate bucket(s)
  // In Prometheus, buckets are cumulative: each bucket counts values <= its bound
  for (int i = 0; i < h->bucketCount; i++)
  {
    if (value <= h->bucketBounds[i])
      atomic_fetch_add(&h->bucketCounts[i], 1);
  }

  // Update sum and count
  atomic_fetch_add(&h->sum, (int64_t)(value * 1000.0));
  atomic_fetch_add(&h->count, 1);
}



// -----------------------------------------------------------------------------
//
// corPromRenderSize - calculate size needed for corPromRender
//
int corPromRenderSize(void)
{
  int size = 0;

  for (CorPromMetric* m = metricList; m != NULL; m = m->next)
  {
    // HELP line
    if (m->help != NULL)
      size += snprintf(NULL, 0, "# HELP %s %s\n", m->name, m->help);

    // TYPE line
    const char* typeStr = (m->type == CorPromTypeCounter) ? "counter" :
                          (m->type == CorPromTypeGauge)   ? "gauge"   : "histogram";
    size += snprintf(NULL, 0, "# TYPE %s %s\n", m->name, typeStr);

    // Value line(s)
    if (m->type == CorPromTypeCounter)
    {
      int64_t iVal = atomic_load(&m->value.iValue) / 1000;
      size += snprintf(NULL, 0, "%s %ld\n", m->name, iVal);
    }
    else if (m->type == CorPromTypeGauge)
    {
      int64_t iVal = atomic_load(&m->value.iValue) / 1000;
      size += snprintf(NULL, 0, "%s %ld\n", m->name, iVal);
    }
    else if (m->type == CorPromTypeHistogram)
    {
      CorPromHistogram* h = &m->value.histogram;

      // Bucket lines
      for (int i = 0; i < h->bucketCount; i++)
      {
        int64_t count = atomic_load(&h->bucketCounts[i]);

        if (h->bucketBounds[i] == INFINITY)
          size += snprintf(NULL, 0, "%s_bucket{le=\"+Inf\"} %ld\n", m->name, count);
        else
          size += snprintf(NULL, 0, "%s_bucket{le=\"%.3f\"} %ld\n", m->name, h->bucketBounds[i], count);
      }

      // Sum and count
      int64_t sum   = atomic_load(&h->sum);
      int64_t count = atomic_load(&h->count);

      size += snprintf(NULL, 0, "%s_sum %.3f\n", m->name, sum / 1000.0);
      size += snprintf(NULL, 0, "%s_count %ld\n", m->name, count);
    }
  }

  return size;
}



// -----------------------------------------------------------------------------
//
// corPromRender - render all metrics in Prometheus text format
//
int corPromRender(char* buf, int bufLen)
{
  int written = 0;
  int remaining = bufLen;

  for (CorPromMetric* m = metricList; m != NULL; m = m->next)
  {
    int n;

    // HELP line
    if (m->help != NULL)
    {
      n = snprintf(buf + written, remaining, "# HELP %s %s\n", m->name, m->help);
      if (n >= remaining) return written + n;
      written += n;
      remaining -= n;
    }

    // TYPE line
    const char* typeStr = (m->type == CorPromTypeCounter) ? "counter" :
                          (m->type == CorPromTypeGauge)   ? "gauge"   : "histogram";
    n = snprintf(buf + written, remaining, "# TYPE %s %s\n", m->name, typeStr);
    if (n >= remaining) return written + n;
    written += n;
    remaining -= n;

    // Value line(s)
    if (m->type == CorPromTypeCounter)
    {
      int64_t iVal = atomic_load(&m->value.iValue) / 1000;

      n = snprintf(buf + written, remaining, "%s %ld\n", m->name, iVal);
      if (n >= remaining) return written + n;
      written += n;
      remaining -= n;
    }
    else if (m->type == CorPromTypeGauge)
    {
      int64_t iVal = atomic_load(&m->value.iValue) / 1000;

      n = snprintf(buf + written, remaining, "%s %ld\n", m->name, iVal);
      if (n >= remaining) return written + n;
      written += n;
      remaining -= n;
    }
    else if (m->type == CorPromTypeHistogram)
    {
      CorPromHistogram* h = &m->value.histogram;

      // Bucket lines
      for (int i = 0; i < h->bucketCount; i++)
      {
        int64_t count = atomic_load(&h->bucketCounts[i]);

        if (h->bucketBounds[i] == INFINITY)
          n = snprintf(buf + written, remaining, "%s_bucket{le=\"+Inf\"} %ld\n", m->name, count);
        else
          n = snprintf(buf + written, remaining, "%s_bucket{le=\"%.3f\"} %ld\n", m->name, h->bucketBounds[i], count);

        if (n >= remaining) return written + n;
        written += n;
        remaining -= n;
      }

      // Sum and count
      int64_t sum   = atomic_load(&h->sum);
      int64_t count = atomic_load(&h->count);

      n = snprintf(buf + written, remaining, "%s_sum %.3f\n", m->name, sum / 1000.0);
      if (n >= remaining) return written + n;
      written += n;
      remaining -= n;

      n = snprintf(buf + written, remaining, "%s_count %ld\n", m->name, count);
      if (n >= remaining) return written + n;
      written += n;
      remaining -= n;
    }
  }

  return written;
}



// -----------------------------------------------------------------------------
//
// corPromLookup - find a metric by name
//
CorPromMetric* corPromLookup(const char* name)
{
  for (CorPromMetric* m = metricList; m != NULL; m = m->next)
  {
    if (strcmp(m->name, name) == 0)
      return m;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// corPromMetrics - render all metrics as a CorNode tree
//
CorNode* corPromMetrics(KAlloc* kaP)
{
  CorNode* metricsArray = corTreeArray(kaP, NULL);

  for (CorPromMetric* m = metricList; m != NULL; m = m->next)
  {
    CorNode* metricObj = corTreeObject(kaP, NULL);

    corTreeChildAdd(metricObj, corTreeString(kaP, "name", m->name));

    const char* typeStr = (m->type == CorPromTypeCounter) ? "counter" :
                          (m->type == CorPromTypeGauge)   ? "gauge"   : "histogram";
    corTreeChildAdd(metricObj, corTreeString(kaP, "type", typeStr));

    if (m->help != NULL)
      corTreeChildAdd(metricObj, corTreeString(kaP, "help", m->help));

    if (m->type == CorPromTypeCounter || m->type == CorPromTypeGauge)
    {
      int64_t iVal = atomic_load(&m->value.iValue);
      double  val  = iVal / 1000.0;
      corTreeChildAdd(metricObj, corTreeFloat(kaP, "value", val));
    }
    else if (m->type == CorPromTypeHistogram)
    {
      CorPromHistogram* h = &m->value.histogram;

      // Buckets array
      CorNode* bucketsArray = corTreeArray(kaP, "buckets");
      for (int i = 0; i < h->bucketCount; i++)
      {
        CorNode* bucketObj = corTreeObject(kaP, NULL);

        if (h->bucketBounds[i] == INFINITY)
          corTreeChildAdd(bucketObj, corTreeString(kaP, "le", "+Inf"));
        else
          corTreeChildAdd(bucketObj, corTreeFloat(kaP, "le", h->bucketBounds[i]));

        int64_t count = atomic_load(&h->bucketCounts[i]);
        corTreeChildAdd(bucketObj, corTreeInteger(kaP, "count", count));

        corTreeChildAdd(bucketsArray, bucketObj);
      }
      corTreeChildAdd(metricObj, bucketsArray);

      // Sum and count
      int64_t sum   = atomic_load(&h->sum);
      int64_t count = atomic_load(&h->count);
      corTreeChildAdd(metricObj, corTreeFloat(kaP, "sum", sum / 1000.0));
      corTreeChildAdd(metricObj, corTreeInteger(kaP, "count", count));
    }

    corTreeChildAdd(metricsArray, metricObj);
  }

  return metricsArray;
}
