# Time API (Phase 1)
**Module:** `k`  
**Namespace:** `k::time`  
**Sources:** `libk/libk/src/time/*.k`  
**Linking:** Automatic — part of base `k` library  
**Normative specification:** `libk-time-API-Specification.md`

---

## 1. Overview

The `k::time` namespace provides a platform-independent, value-semantic, and
immutable temporal model for the K language.

Phase 1 provides the deterministic core:
- Checked temporal exception hierarchy.
- Checked arithmetic kernel and normalization.
- `Duration`: chronological elapsed amount (seconds + nanoseconds).
- `Instant`: point on the global continuous timeline.
- `EpochDay`: day count relative to 1970-01-01 in the proleptic Gregorian calendar.
- `LocalDate`: Gregorian civil date with year, month, and day.
- `LocalTime`: 24-hour civil clock time with nanosecond precision.
- `Period`: distinct civil calendar amounts (years, months, weeks, days, hours, minutes, seconds).
- `LocalDateTime`: composition of a `LocalDate` and `LocalTime`.
- `ZoneOffset`: fixed displacement from UTC between -18:00 and +18:00.
- `Iso`: canonical ISO-8601 string formatting and parsing.

All Phase 1 operations are pure functions of their inputs and depend on no
host clocks, system locale, or timezone database.

---

## 2. Exceptions (`k::time`)

The hierarchy derives from checked `Exception`:

```text
Exception
  └── TemporalException (500)
       ├── InvalidTemporalValueException (501)
       ├── TemporalArithmeticException (502)
       ├── TemporalParseException (503)
       ├── LeapSecondException (504)
       ├── TimeDataUnavailableException (510)
       │    ├── ZoneRulesUnavailableException (511)
       │    ├── SystemTimeZoneUnavailableException (512)
       │    └── TimeScaleDataUnavailableException (513)
       └── LocalTimeResolutionException (520)
            ├── LocalTimeGapException (521)
            └── LocalTimeOverlapException (522)
```

---

## 3. Core Types

### 3.1 `Duration`
Immutable chronological elapsed time, represented as signed seconds and nanoseconds
(`0 <= nanoAdjustment < 1_000_000_000`).

- **Factories:** `zero()`, `ofNanos(n)`, `ofMicros(us)`, `ofMillis(ms)`, `ofSeconds(s)`, `ofSeconds(s, ns)`, `ofMinutes(m)`, `ofHours(h)`.
- **Accessors:** `secondsPart()`, `nanoAdjustment()`, `toNanosExact()`, `toMicros()`, `toMillis()`, `toSeconds()`, `isZero()`, `isNegative()`.
- **Arithmetic & Operators:** `plus`, `minus`, `negated`, `multipliedBy`, `dividedBy`, `compareTo`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `-()`, `*`, `/`.

### 3.2 `Instant`
Immutable point on K's continuous global timeline, represented as seconds and nanoseconds
from the epoch `1970-01-01T00:00:00Z`.

- **Factories:** `epoch()`, `ofEpochSecond(s)`, `ofEpochSecond(s, ns)`.
- **Accessors:** `epochSeconds()`, `nanoAdjustment()`.
- **Operations:** `plus(Duration)`, `minus(Duration)`, `minus(Instant) -> Duration`, `until(Instant) -> Duration`, comparisons and relational operators.

### 3.3 `EpochDay`
Signed day count relative to the Gregorian epoch `1970-01-01`.

- **Factories:** `ofDaysSinceEpoch(days)`.
- **Accessors & Operations:** `daysSinceEpoch()`, `plusDays(days)`, `minusDays(days)`, operators `+`, `-`, comparison.

### 3.4 `LocalDate`
Proleptic Gregorian civil date (`year`, `month` 1..12, `day` 1..31).

- **Factories:** `of(year, month, day)`, `fromEpochDay(EpochDay)`.
- **Accessors:** `year()`, `month()`, `day()`, `dayOfYear()`, `isLeapYear()`, `lengthOfMonth()`, `toEpochDay()`.
- **Operations:** `plus(Period)`, `minus(Period)`, `atTime(LocalTime) -> LocalDateTime`, operators `+`, `-`, comparison.
- **Period application:** End-of-month clamping applies (e.g. `2026-01-31 + 1 month == 2026-02-28`).

### 3.5 `LocalTime`
Civil time of day without zone (`hour` 0..23, `minute` 0..59, `second` 0..59, `nano` 0..999_999_999).
Supports structural leap second label `23:59:60`. Rejects `24:00:00`.

- **Factories:** `midnight()`, `of(hour, minute)`, `of(hour, minute, second, nano)`.
- **Accessors:** `hour()`, `minute()`, `second()`, `nano()`, `isStructuralLeapSecond()`.
- **Operations:** `plus(Duration)`, `minus(Duration)` (wraps around 24 hours; rejects structural leap second), comparison.

### 3.6 `Period`
Civil amount holding distinct `years`, `months`, `weeks`, `days`, `hours`, `minutes`, and `seconds`.
Fields are preserved without converting months to days or days to seconds.

- **Factories:** `zero()`, `of(y, m, w, d, h, min, s)`, `years(y)`, `months(m)`, `weeks(w)`, `days(d)`, `hours(h)`, `minutes(m)`, `seconds(s)`.
- **Accessors:** `yearsPart()`, `monthsPart()`, `weeksPart()`, `daysPart()`, `hoursPart()`, `minutesPart()`, `secondsPart()`, `isZero()`.
- **Operations:** `plus(Period)`, `minus(Period)`, `negated()`, operators `+`, `-`, `-()`, `==`, `!=`.

### 3.7 `LocalDateTime`
Composition of a `LocalDate` and `LocalTime`.

- **Factories:** `of(date, time)`, `of(year, month, day, hour, minute, second, nano)`.
- **Accessors:** `date()`, `time()`, `year()`, `month()`, `day()`, `hour()`, `minute()`, `second()`, `nano()`.
- **Operations:** `plus(Period)`, `minus(Period)` (with time-to-date carry), `plus(Duration)`, `minus(Duration)` (86,400s civil day arithmetic), comparison.

### 3.8 `ZoneOffset`
Fixed UTC offset in seconds (`-18:00` to `+18:00` / `-64800` to `+64800` seconds).

- **Factories:** `utc()`, `ofTotalSeconds(seconds)`, `parseIso(text)`.
- **Accessors:** `totalSeconds()`, `isUtc()`, `toIsoString()`.
- **Operations:** `compareTo`, operators `==`, `!=`, `<`, `<=`, `>`, `>=`.

### 3.9 `Iso`
Stateless canonical ISO-8601 formatter and parser utility class.

- **Formatters:**
  - `formatDuration(Duration)` -> `PT[sign]S[.fraction]`
  - `formatLocalDate(LocalDate)` -> `YYYY-MM-DD`
  - `formatLocalTime(LocalTime)` -> `HH:MM:SS[.fraction]`
  - `formatLocalDateTime(LocalDateTime)` -> `YYYY-MM-DDTHH:MM:SS[.fraction]`
  - `formatZoneOffset(ZoneOffset)` -> `Z`, `+HH:MM`, `-HH:MM`, `+HH:MM:SS`, `-HH:MM:SS`
- **Parsers:**
  - `parseDuration(text)` -> `Duration`
  - `parseLocalDate(text)` -> `LocalDate`
  - `parseLocalTime(text)` -> `LocalTime`
  - `parseLocalDateTime(text)` -> `LocalDateTime`
