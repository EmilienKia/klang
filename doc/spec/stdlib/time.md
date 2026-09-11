# Time API (Phase 1, Phase 2 & Phase 3 — Zone Data Core)
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

Phase 2 adds clock domains and native clock adapters:
- `Clock`: injectable absolute time source contract.
- `FixedClock`: deterministic test clock returning a constant `Instant`.
- `SequenceClock`: deterministic test clock advancing through an array of `Instant`s.
- `SystemClock`: system wall-clock facade (`now()` throws 513 until Phase 3).
- `MonotonicClockId`: distinct identity of a monotonic clock domain.
- `MonotonicInstant`: point in time within a specific monotonic clock domain.
- `MonotonicClock`: injectable monotonic time source contract.
- `FixedMonotonicClock`: deterministic test monotonic clock.
- `SystemMonotonicClock`: active (`activeTime()`) and elapsed (`elapsedTime()`) system monotonic clocks.
- `PosixTimestamp`: normalized POSIX coordinate and system real-time reading (`systemNow()`).

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

---

## 4. Phase 2 Types: Clocks and Monotonic Domains

### 4.1 `Clock`, `FixedClock`, `SequenceClock`, `SystemClock`
Injectable absolute-time contracts and implementations.
- `Clock`: interface with `now() : Instant throws(...)` and `resolution() : Duration throws(...)`.
- `FixedClock`: deterministic test clock initialized with an `Instant`.
- `SequenceClock`: deterministic test clock advancing through an array of `Instant`s; throws `TimeDataUnavailableException(515)` when exhausted.
- `SystemClock`: singleton facade via `SystemClock::instance()`; in Phase 2 `now()` throws `TimeScaleDataUnavailableException(513)`.

### 4.2 `MonotonicClockId`, `MonotonicInstant`, `MonotonicClock`
Monotonic measurement types guaranteeing non-decreasing timelines within a domain.
- `MonotonicClockId`: value-semantic clock domain identity; comparison `==`, `!=`.
- `MonotonicInstant`: domain-scoped instant; relational operations and subtraction across different clock domains throw `InvalidTemporalValueException(501)`.
- `MonotonicClock`: interface with `id()`, `now()`, `resolution()`, `includesSuspend()`.
- `FixedMonotonicClock`: deterministic test monotonic clock with `advance(Duration)` and `set(MonotonicInstant)`.
- `SystemMonotonicClock`: platform monotonic clocks via `SystemMonotonicClock::activeTime()` (excludes suspend) and `SystemMonotonicClock::elapsedTime()` (includes suspend).

### 4.3 `PosixTimestamp`
Normalized POSIX timestamp coordinate (`seconds` + `nanos`, `0 <= nanos < 1_000_000_000`).
- **Factories:** `ofSeconds(s)`, `ofSeconds(s, ns)`, `systemNow()`.
- **Accessors:** `seconds()`, `nanoAdjustment()`.
- **Arithmetic:** `plus(Duration)`, `minus(Duration)`, `minus(PosixTimestamp) -> Duration`, `until(PosixTimestamp) -> Duration`.
- **Conversion:** `toInstant()` and `Instant::fromPosixTimestamp()` throw `TimeScaleDataUnavailableException(513)` in Phase 2.

---

## 5. Phase 3 Types: Zone Data Core (Package D6)

### 5.1 `ZoneId`
Immutable validated IANA-style time-zone identity (e.g. `Europe/Paris`, `America/New_York`, `UTC`, `Etc/GMT+1`).
Equality compares normalized identifier text only, not offsets or rules.
- **Factory:** `ZoneId::of(name: const String&) -> ZoneId throws(InvalidTemporalValueException)`
  - Rejects empty strings, leading slash, trailing slash, consecutive `//`, path traversal `..`, and characters outside `[A-Za-z0-9/_+-]`.
- **Accessor:** `name() -> String`
- **Equality:** `operator==`, `operator!=`

### 5.2 `PosixLeapSecondPolicy`
Singletons defining how second 60 is mapped during POSIX interoperation:
- `PosixLeapSecondPolicy::reject()`
- `PosixLeapSecondPolicy::foldToPreviousSecond()`
- `PosixLeapSecondPolicy::foldToFollowingSecond()`
- `operator==`, `operator!=`

### 5.3 `LeapSecondTable`
Access to historical (1972–2017) and loaded leap seconds.
- `isLeapSecond(epochSecond: long) -> bool`
- `taiOffset(epochSecond: long) -> long`
- `count() -> int`
- `isLeapDate(year: long, month: int, day: int) -> bool`
- `validateLeapSecond(year, month, day, hour, minute, second) throws(LeapSecondException, InvalidTemporalValueException)`
- `validate(dateTime: const LocalDateTime&) throws(LeapSecondException)`
- `load(filePath: const String&) -> int`

### 5.4 `Tzdb`
Access to the system time-zone database (default `/usr/share/zoneinfo`) and immutable `KZoneRulesSnapshot` handles.
- `version() -> String`
- `setRoot(rootPath: const String&)`
- `isZoneAvailable(zoneId: const ZoneId&) -> bool`
- `loadZone(zoneId: const ZoneId&) -> KZoneRulesSnapshot* throws(ZoneRulesUnavailableException)`
- `releaseSnapshot(snap: KZoneRulesSnapshot*)`
- `transitionCount`, `transitionTime`, `transitionOffset`, `transitionAbbrev`, `transitionIsDst`
- `offsetAt`, `abbrevAt`, `isDstAt`, `posixTz`

### 5.5 `ZoneTransition`, `ZoneLocalResolution`, and Resolvers
Local-time resolution model for handling DST gaps and overlaps:
- `ZoneTransition`: transition instant, beforeOffset, afterOffset, `isGap()`, `isOverlap()`, `duration()`.
- `ZoneLocalResolution`: abstract base with `UniqueResolution`, `GapResolution`, `OverlapResolution`.
- `LocalDateTimeResolver`:
  - `StrictResolver::instance()`: throws `LocalTimeGapException(521)` on gap, `LocalTimeOverlapException(522)` on overlap.
  - `EarlierResolver::instance()`: throws on gap; on overlap selects the earlier instant / earlier offset.
  - `LaterResolver::instance()`: throws on gap; on overlap selects the later instant / later offset.
  - `PreferOffsetResolver(preferredOffset)`: on overlap matches preferred offset if possible.
  - `ShiftForwardResolver::instance()`: on gap shifts forward by gap duration; throws on overlap.
  - `ShiftBackwardResolver::instance()`: on gap shifts backward by gap duration; throws on overlap.

### 5.6 `ZoneRules` and `TimeZone`
- `ZoneRules`: interface with `version()`, `offsetAt(Instant)`, `validOffsets(LocalDateTime)`, `resolveLocal(LocalDateTime)`, `transition(LocalDateTime)`.
  - Concrete implementations: `FixedZoneRules` and `TzdbZoneRules`.
- `TimeZone`: immutable pairing of `ZoneId` and `ZoneRules`.
  - `TimeZone::of(id) throws(ZoneRulesUnavailableException)`
  - `TimeZone::of(id, version) throws(ZoneRulesUnavailableException)`
  - `TimeZone::fixed(offset)`
  - `TimeZone::system() throws(SystemTimeZoneUnavailableException, ZoneRulesUnavailableException)`

### 5.7 `ZonedDateTime`
Immutable pairing of an absolute `Instant` and a `TimeZone`.
- **Factories:** `fromInstant(instant, zone)`, `of(local, zone)` (strict), `of(local, zone, resolver)`.
- **Accessors:** `instant()`, `zone()`, `offset()`, `localDate()`, `localTime()`, `localDateTime()`.
- **Arithmetic:**
  - `plus(Duration)` / `minus(Duration)`: timeline arithmetic on `Instant`.
  - `plus(Period, resolver)` / `minus(Period, resolver)`: civil calendar arithmetic on `LocalDateTime`, followed by zone resolution.
- **Operations:** `withZone(newZone)`, `isBefore`, `isAfter`, `isSameInstant`, `operator==`, `operator!=`.

### 5.8 `Iso` Additions in Phase 3
- `Iso::formatInstant(instant: const Instant&) -> String` -> `YYYY-MM-DDTHH:MM:SS[.fraction]Z`
- `Iso::formatZonedDateTime(zdt: const ZonedDateTime&) -> String` -> `YYYY-MM-DDTHH:MM:SS[.fraction]±HH:MM[ZoneId]`
- `Iso::parseInstant(text: const String&) -> Instant`
- `Iso::parseZonedDateTime(text: const String&, resolver: const LocalDateTimeResolver&) -> ZonedDateTime`

---

## 6. Phase 4 Types: Alternative Chronologies and Localized Presentation

### 6.1 `ChronologyId`, `Era`, `CalendarMonth`, `CalendarFields`
- `ChronologyId`: immutable identifier (`"Gregorian"`, `"Julian"`, `"Japanese"`, `"Buddhist"`, `"Hebrew"`, `"Islamic"`).
- `Era`: chronology-specific era designator (`id()`, `displayName(locale)`).
- `CalendarMonth`: month number and optional leap-month flag.
- `CalendarFields`: container for era, yearOfEra, month, and day.

### 6.2 `Chronology`, `GregorianChronology`, `JulianChronology`
- `Chronology`: rule system defining validity, month/year lengths, civil arithmetic, and `EpochDay` mapping.
  - `GregorianChronology::instance()`: proleptic Gregorian implementation matching Phase 1 math.
  - `JulianChronology::instance()`: proleptic Julian implementation.
- `CalendarDate`: immutable chronology-specific civil date.
  - `chronology()`, `fields()`, `toEpochDay()`, `toLocalDate()`.
  - `plus(Period)`, `minus(Period)`, `toChronology(target)`.
  - `operator==`, `operator!=`, `operator+`, `operator-`.
- `CalendarDateTime`: pairing of `CalendarDate` and `LocalTime`.
  - `atZone(zone, resolver) -> ZonedDateTime`.
  - `toInstant(zone, resolver) -> Instant`.

### 6.3 `WeekRules` and `Locale`
- `WeekRules`: first day of week and minimal days in first week (`WeekRules::iso()`, `WeekRules::of()`).
- `Locale`: immutable presentation-only configuration with BCP 47 language tag, optional chronology, and week rules.
  - `Locale::of(tag)`, `Locale::system()`.
  - `withChronology(chronology)`, `withWeekRules(rules)`.

### 6.4 `TemporalFormatter` and `TemporalParser`
- `TemporalFormatter`: format `Instant`, `LocalDate`, `LocalDateTime`, `ZonedDateTime`, and `CalendarDateTime`.
  - Builders: `withLocale(locale)`, `withChronology(chronology)`, `withZone(zone)`.
  - Factories: `TemporalFormatters::iso()`, `TemporalFormatters::ofPattern(pattern)`.
- `TemporalParser`: parse temporal values from text with explicit validation and exception mapping.
  - Factories: `TemporalParsers::iso()`, `TemporalParsers::ofPattern(pattern)`.




