# corProm — Prometheus Metrics

Counters, gauges and histograms for a server whose metrics path must not
allocate, block or take a lock: every update is an atomic on a fixed-point
`int64_t`. Rendering happens only when the metrics are asked for — as Prometheus
text, or as a corTree.

- **Version:** 0.1.0
- **Language:** C
- **License:** [Apache License 2.0](LICENSE)

The only dependency is **corTree** (for `corPromMetrics`).

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

int   size = corPromRenderSize();
char* text = malloc(size);
corPromRender(text, size);                 // Prometheus text exposition format

CorNode* treeP = corPromMetrics(kaP);      // the same, as a tree (NULL: malloc)
```

## Build

```sh
make di
```
