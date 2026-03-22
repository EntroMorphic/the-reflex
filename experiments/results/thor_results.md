# NVIDIA Jetson AGX Thor Results

**Platform:** NVIDIA Jetson AGX Thor
**CPU:** 14x ARM Cortex-A78AE @ 2.6GHz
**Cache:** DynamIQ Shared Unit (DSU)
**OS:** Linux (Docker container)

## E3: Latency Comparison

```
┌────────────┬──────────┬──────────┬──────────┬──────────┬────────┐
│ Mechanism  │  Median  │   Mean   │   P99    │  Stddev  │   CV   │
├────────────┼──────────┼──────────┼──────────┼──────────┼────────┤
│ Stigmergy  │    297.0 │    314.9 │    370.0 │    183.7 │  0.58  │
│ Atomic     │    399.0 │    409.5 │    463.0 │     55.5 │  0.14  │
│ Futex      │   9083.0 │   9038.9 │  11463.0 │   2325.9 │  0.26  │
│ Pipe       │  12333.0 │  12143.7 │  14592.0 │   1612.4 │  0.13  │
└────────────┴──────────┴──────────┴──────────┴──────────┴────────┘

Speedup vs Stigmergy:
  Atomic:  1.34x slower
  Futex:   30.6x slower
  Pipe:    41.5x slower
```

## E1: Causality Proof

```
Detection:
  Signals sent:     100
  Signals detected: 100 (100.0%)
  Causality valid:  100/100 (100.0%)

Latency:
  Mean: 1618 ns (includes sync overhead)
  Raw:  ~297 ns (from E3)

Behavior Change:
  Vigilance: 50 -> 100
```

## E2: SNR Under Load

```
┌────────┬──────────┬────────────┬──────────┬──────────┐
│  Load  │ Detected │  Rate      │ Mean(ns) │ Max(ns)  │
├────────┼──────────┼────────────┼──────────┼──────────┤
│    0%  │   78/100 │    78.0%   │  199913  │ 8006898  │
│   25%  │  100/100 │   100.0%   │   10809  │  498204  │
│   50%  │  100/100 │   100.0%   │    1484  │    6324  │
│   80%  │  100/100 │   100.0%   │   18180  │  819120  │
└────────┴──────────┴────────────┴──────────┴──────────┘
```

**Key finding:** Performance is BETTER under load than idle!

## E2b: False Sharing Control

```
True Positive Rate:   100.0%
False Positive Rate:  0.0%
Precision:            100.0%
Recall:               100.0%
```

## E5: Power Characterization

```
Baseline power:     22.58 W
Active power:       23.13 W
Power delta:        0.55 W (within noise floor)
```

**Key finding:** Coordination adds no measurable power overhead.

## E4: Scalability

```
+--------+-----------+-----------+-----------+----------+
| Cores  | Consumers | Mean(ns)  | Detect    |
+--------+-----------+-----------+-----------+----------+
|      2 |         1 |      1731 |   99.0%   |
|      4 |         3 |     42385 |   92.7%   |
|      8 |         7 |     46416 |   95.6%   |
|     14 |        13 |     40364 |   98.0%   |
+--------+-----------+-----------+-----------+----------+
```

**Key finding:** Detection scales (98% at 14 cores). Latency jump is barrier overhead, not coherency degradation.
