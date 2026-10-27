# corProm — Prometheus Metrics

Counters, gauges and histograms for a server whose metrics path must not
allocate, block or take a lock: every update is an atomic on a fixed-point
`int64_t`. Rendering happens only when the metrics are asked for — as Prometheus
text, or as a corTree.

- **Version:** 0.1.0
- **Language:** C
- **License:** [Apache License 2.0](LICENSE)

The only dependency is **corTree** (for `corPromMetrics`); it uses `pthread` and `libm`, which its consumer links.

It is **kprom** under new names, moved onto corTree so that the coraine stack
links one tree library rather than two.

## API

```c
CorPromMetric* reqs = corPromCounterCreate("requests_total", "Requests served");
CorPromMetric* conn = corPromGaugeCreate("connections", "Open connections");
double         b[]  = { 0.001, 0.01, 0.1, 1 };
CorPromMetric* lat  = corPromHistogramCreate("latency_seconds", "Latency", b, 4);

corPromCounterInc(reqs);
corPromGaugeAdd(conn, 1);
corPromHistogramObserve(lat, 0.004);

int   size = corPromRenderSize();          // without the terminating NUL
char* text = malloc(size + 1);
corPromRender(text, size + 1);             // Prometheus text exposition format

CorNode* treeP = corPromMetrics(kaP);      // the same, as a tree (NULL: the tree heap)
```

- **Values.** A counter or gauge is kept in thousandths: a gauge of 2.5 renders `2.5`, of 1/3
  `0.333`; a whole value renders without a fraction (`5`). A histogram's sum is kept in billionths.
- **Creating.** Any thread, at any time: creation takes a mutex, and a metric is in the list only
  once complete, so a render running at the same time shows it whole or not at all. A name already
  created is refused - `NULL` (Prometheus refuses an exposition with a name twice). An update
  (`Inc`, `Add`, `Set`, `Observe`) on `NULL` does nothing.
- **Rendering** takes no lock. Between `corPromRenderSize` and `corPromRender` a metric can be
  created or a value grow: `corPromRender` returns the size it needed when the buffer was too small.

## Build

```sh
make di
```
