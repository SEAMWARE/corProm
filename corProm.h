//
// FILE            corProm.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Ken Zangelin
//
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORPROM_H_
#define CORPROM_H_

#include <stdint.h>                              // int64_t

#include "corAlloc/CorAlloc.h"                      // CorAlloc
#include "corTree/CorNode.h"                        // CorNode

//
// CorPromMetric is an opaque type - internal structure defined in corProm.c
//
typedef struct CorPromMetric CorPromMetric;



// -----------------------------------------------------------------------------
//
// corPromCounterCreate - create a new counter metric
//
extern CorPromMetric* corPromCounterCreate(const char* name, const char* help);



// -----------------------------------------------------------------------------
//
// corPromGaugeCreate - create a new gauge metric
//
extern CorPromMetric* corPromGaugeCreate(const char* name, const char* help);



// -----------------------------------------------------------------------------
//
// corPromHistogramCreate - create a new histogram metric
//
// buckets: array of upper bounds (e.g., {0.01, 0.05, 0.1, 0.5, 1.0})
// bucketCount: number of elements in buckets array
// A +Inf bucket is added automatically.
//
extern CorPromMetric* corPromHistogramCreate(const char* name, const char* help, double* buckets, int bucketCount);



// -----------------------------------------------------------------------------
//
// corPromCounterInc - increment a counter by 1 (atomic, lock-free)
//
extern void corPromCounterInc(CorPromMetric* counter);



// -----------------------------------------------------------------------------
//
// corPromCounterAdd - add a value to a counter (atomic, lock-free)
//
// The value is added as fixed-point (multiplied by 1000 internally).
//
extern void corPromCounterAdd(CorPromMetric* counter, int64_t value);



// -----------------------------------------------------------------------------
//
// corPromGaugeSet - set a gauge to a specific value (atomic)
//
extern void corPromGaugeSet(CorPromMetric* gauge, double value);



// -----------------------------------------------------------------------------
//
// corPromGaugeAdd - add to a gauge (atomic, lock-free)
//
extern void corPromGaugeAdd(CorPromMetric* gauge, double value);



// -----------------------------------------------------------------------------
//
// corPromGaugeSub - subtract from a gauge (atomic, lock-free)
//
extern void corPromGaugeSub(CorPromMetric* gauge, double value);



// -----------------------------------------------------------------------------
//
// corPromHistogramObserve - record an observation in a histogram (atomic, lock-free)
//
extern void corPromHistogramObserve(CorPromMetric* histogram, double value);



// -----------------------------------------------------------------------------
//
// corPromRenderSize - calculate size needed for corPromRender
//
// Returns the number of bytes needed (excluding null terminator).
//
extern int corPromRenderSize(void);



// -----------------------------------------------------------------------------
//
// corPromRender - render all metrics in Prometheus text format
//
// Returns the number of bytes written (excluding null terminator).
// If the buffer is too small, returns the required size (call again with larger buffer).
//
extern int corPromRender(char* buf, int bufLen);



// -----------------------------------------------------------------------------
//
// corPromLookup - find a metric by name
//
extern CorPromMetric* corPromLookup(const char* name);



// -----------------------------------------------------------------------------
//
// corPromMetrics - render all metrics as a CorNode tree
//
// Returns an array of metric objects for a caller's /metrics endpoint.
// Pass NULL to use malloc, or the CorAlloc to build the tree in.
//
extern CorNode* corPromMetrics(CorAlloc* kaP);

#endif  // CORPROM_H_
