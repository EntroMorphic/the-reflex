# Raspberry Pi 4 Results

**Platform:** Raspberry Pi 4 Model B Rev 1.5
**CPU:** 4x ARM Cortex-A72 @ 1.5GHz
**RAM:** 8GB
**OS:** Debian (64-bit)

## E3: Latency Comparison

```
+------------+----------+----------+----------+----------+
| Mechanism  |  Median  |   Mean   |   P99    |  Stddev  |
+------------+----------+----------+----------+----------+
| Stigmergy  |    166.7 |    171.0 |    166.7 |    602.9 |
| Atomic     |    148.1 |    145.6 |    240.7 |     50.0 |
| Futex      |    777.8 |   6814.6 |  15240.7 |   6615.1 |
| Pipe       |  16000.0 |  15651.1 |  16648.1 |   1135.9 |
+------------+----------+----------+----------+----------+

Speedup vs Stigmergy:
  Atomic: 0.89x (slightly faster)
  Futex:  4.67x slower
  Pipe:   96.00x slower
```

## E1: Causality Proof

```
Detection:
  Signals sent:     100
  Signals detected: 100 (100.0%)
  Causality valid:  100/100 (100.0%)

Latency:
  Mean: 48.7 ns
  Min:  37.0 ns
  Max:  55.6 ns

Behavior Change:
  Vigilance: 50 -> 100
```

## E2: SNR Under Load

```
| Load | Detection | Latency |
|------|-----------|---------|
| 0%   | 100%      | 88 ns   |
| 25%  | 100%      | 1.3 ms  |
| 50%  | 100%      | 1.1 ms  |
| 80%  | 100%      | 1.1 ms  |
```

Note: High latency under load due to only 4 cores (load generators compete with test threads). Detection remains 100%.

## E2b: False Sharing Control

```
True Positive Rate:   100.0%
False Positive Rate:  0.0%
Precision:            100.0%
Recall:               100.0%
```

## Key Findings

1. **Stigmergy works on $35 hardware**
2. **5x faster than futex** (vs 30x on Thor - smaller gap due to Pi's simpler kernel)
3. **100% detection reliability**
4. **Perfect false sharing separation**
