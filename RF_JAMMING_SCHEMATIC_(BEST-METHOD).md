# Multi-Radio Passive Survey – Learning Guide

**ESP32-Reaper / doomBOX companion document**  
Version 1.0 – Focus: clean, measurable, educational channel-hopping with three nRF24L01 modules.

This document describes **receive-only** channel stepping.  
No continuous carrier, no noise generation, and no jamming functionality is present or proposed.

---

## 1. Why three radios?

A single nRF24 can only listen on one 1 MHz channel at a time.  
With three independent radios on a shared SPI bus we can observe three channels concurrently. This gives:

- Faster coverage of the 2.4 GHz ISM band
- Better statistical sampling of activity
- A clear demonstration of parallel vs sequential scanning

The educational goal is to make the hopping **predictable, measurable, and easy to visualise**.

---

## 2. Current implementation (baseline)

```cpp
// Simplified version of the existing surveyStep()
void surveyStep(uint8_t start, uint8_t end, uint8_t step) {
  uint8_t range = end - start + 1;
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t offset = (i * step) % range;
    uint8_t ch = start + ((survey_index[i] + offset) % range);
    radios[i].setChannel(ch);
    survey_index[i] = (survey_index[i] + step) % range;
  }
}
```

### Observed problems for learning

| Issue | Effect |
|-------|--------|
| Radios can land on the same channel | Wasted parallelism |
| Coverage over time is uneven | Some channels visited far more often |
| Hard to visualise | Students cannot easily see the pattern |
| Limited runtime configuration | Difficult to experiment |

---

## 3. Recommended hopping strategies

Four strategies are defined. They can be selected at compile time or (preferably) at runtime.

### 3.1 PARTITION – Strict non-overlapping chunks

```
range = end - start + 1
chunk = range / 3

Radio 0:  start ………………… start+chunk-1
Radio 1:  start+chunk ……… start+2*chunk-1
Radio 2:  start+2*chunk …… end   (gets the remainder)
```

Each radio hops **only inside its own chunk**.

**Teaching point:** Spatial division of the band.  
Easiest pattern to draw on a whiteboard and to measure.

### 3.2 INTERLEAVED – Classic parallel sweep

```
At global step t:
  Radio 0 → start + (t * 3)     % range
  Radio 1 → start + (t * 3 + 1) % range
  Radio 2 → start + (t * 3 + 2) % range
```

**Guarantee:** The three radios are always on distinct channels.

**Teaching point:** Maximum concurrent coverage with deterministic motion.

### 3.3 STAGGERED – Fixed phase offset (cleaned-up original idea)

```
phase starts at 0 and increments by `step` every tick

Radio i channel = start + (phase + i * (range / 3)) % range
```

**Teaching point:** Relative motion created by a constant phase difference.

### 3.4 RANDOM_DISTINCT – Controlled randomness

```
Pick three unique channels uniformly from [start … end]
(Reject and re-sample on collision)
```

**Teaching point:** Trade-off between determinism and statistical coverage.

---

## 4. Configuration surface

```cpp
enum class HopStrategy : uint8_t {
  PARTITION       = 0,
  INTERLEAVED     = 1,
  STAGGERED       = 2,
  RANDOM_DISTINCT = 3,
  SEQUENTIAL      = 4   // all three radios march together (inefficient reference)
};

struct SurveyConfig {
  uint8_t     start;          // inclusive, 0-125
  uint8_t     end;            // inclusive, 0-125
  uint8_t     step;           // phase advance per tick
  HopStrategy strategy;
  uint16_t    interval_ms;    // time between hops (default 50)
  bool        force_distinct; // never allow two radios on same channel
};
```

### Default ranges (already present in the firmware)

| Mode       | nRF24 channels | Approx. frequency     |
|------------|----------------|-----------------------|
| Bluetooth  | 2 – 80         | 2402 – 2480 MHz       |
| Wi-Fi      | 1 – 14         | ~2412 – 2484 MHz      |

Everything above should be changeable via serial commands or ESP-NOW packets so students can run live experiments without recompiling.

---

## 5. Observability – the real learning tool

### 5.1 Periodic serial status

```
[SURVEY] t=0042  R1=17  R2=42  R3=67  strategy=INTERLEAVED
[HITS]   ch17=3  ch42=2  ch67=4  ...
[COVERAGE] steps=200  min=2  max=9  avg=5.12  cv=0.31
```

`cv` = coefficient of variation (std-dev / mean).  
Lower = more even coverage.

### 5.2 Hit-count histogram

After a configurable number of steps (or on demand) print a compact histogram:

```
Channel hit counts (2-80):
  2:███ 3:██ 4:████ 5:███ ... 80:██
```

### 5.3 LED feedback (already available)

- Different patterns for each `HopStrategy`
- Short flash on any radio that actually receives a packet
- Slow blink when survey is paused

---

## 6. Suggested student experiments

1. **Evenness test**  
   Run INTERLEAVED vs SEQUENTIAL for 60 s and compare the coefficient of variation.

2. **Step-size effect**  
   Fix strategy = INTERLEAVED, change `step` from 1 → 5 → 11 and watch coverage collapse.

3. **Range partitioning**  
   Switch to PARTITION and measure how long each radio spends on its chunk.

4. **Collision cost**  
   Disable `force_distinct` and count how often two radios land on the same channel.

5. **Wall-clock coverage**  
   Time how long it takes for every channel in the range to be visited at least once under each strategy.

---

## 7. Channel mathematics (quick reference)

| System              | Channel definition          | nRF24 equivalent          |
|---------------------|-----------------------------|---------------------------|
| nRF24L01            | 0–125 (1 MHz steps)         | 2400 + ch MHz             |
| Bluetooth Classic   | 79 channels, 2402–2480 MHz  | nRF24 2–80                |
| BLE Advertising     | 37, 38, 39 (2402/26/80)     | nRF24 2, 26, 80           |
| Wi-Fi 2.4 GHz       | 1–14 (5 MHz centres)        | roughly nRF24 1–14        |

Remember: the nRF24 has **no continuous RSSI**. A “hit” only occurs when a packet is successfully received (or the limited RPD bit is set on some clones).

---

## 8. Implementation roadmap (passive only)

1. Add `HopStrategy` enum and `SurveyConfig` struct.
2. Replace the current `surveyStep()` with a strategy dispatch.
3. Add hit counters and a simple coverage report.
4. Expose configuration via serial (and later ESP-NOW).
5. Document the four strategies with the ASCII diagrams above.
6. Keep PA level low and never call any continuous-TX API.

---

## 9. Design principles

- **Predictable** – a student should be able to predict the next channel of each radio.
- **Measurable** – coverage quality must be quantifiable (histogram + CV).
- **Comparable** – every strategy can be run under identical conditions.
- **Safe** – receive-only; no continuous carrier, no jamming modes.

---

*This document is intended as educational material for the ESP32-Reaper passive survey module. It deliberately excludes any transmit or continuous-carrier functionality.*
