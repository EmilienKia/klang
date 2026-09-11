# Guide to Date and Time Management in K (`k::time`)

The K standard library provides a rich, immutable, and platform-neutral date
and time subsystem in the `k::time` namespace.

Rather than compressing all temporal concepts into a single all-purpose
`DateTime` class, K explicitly separates physical time, civil calendars,
monotonic measurements, time zones, alternative chronologies, and time scales
into dedicated, type-safe structures.

This guide explains the architecture from a user perspective, provides a
lookup matrix to help choose the right class for every task, demonstrates how
classes interact, and illustrates key workflows with complete K examples.

---

## 1. Design Philosophy and Mental Model

Working with time involves several distinct concepts that are often conflated
in legacy APIs:

1. **A point on the physical timeline** (`Instant`): An absolute moment in the
   universe independent of human culture, seasons, or time zones (e.g. "when
   the server crashed").
2. **A civil calendar label** (`LocalDate`, `LocalTime`, `LocalDateTime`): What
   humans read on a calendar or wall clock in a specific location (e.g.
   "Christmas starts at 2026-12-25 00:00:00"). Without a time zone, this does
   not identify a unique instant on the timeline.
3. **Monotonic measurement** (`MonotonicInstant`, `MonotonicClock`): A steadily
   advancing counter used for measuring execution time and timeouts, completely
   immune to system clock jumps, daylight saving adjustments, or NTP updates.
4. **Chronological elapsed time vs civil amounts** (`Duration` vs `Period`):
   - A `Duration` is a fixed number of elapsed physical seconds and
     nanoseconds (e.g. "3600 seconds").
   - A `Period` is a civil amount expressed in calendar units: years, months,
     weeks, and days (e.g. "1 month"). Adding one month to January 31 yields
     February 28 (or 29 in a leap year), not 30 or 31 days.
5. **Time zones and local time ambiguity** (`TimeZone`, `ZonedDateTime`):
   When daylight saving transitions occur, local clock labels can jump forward
   (a **gap**, where a local time never occurred) or jump back (an **overlap**,
   where a local time occurs twice). K requires explicit resolution policies
   rather than silently guessing.

---

## 2. Which Class for Which Use Case?

Use this decision table to select the right class for your requirements:

| What do you want to do? | Recommended type(s) | Key operations |
|-------------------------|---------------------|----------------|
| **Measure execution time or benchmarks** | `MonotonicClock`, `MonotonicInstant` | `SystemMonotonicClock::activeTime().now()`, `until()` |
| **Set an interruptible deadline or timeout** | `Duration` | `Duration::ofMillis(500L)`, `Thread::sleep()` |
| **Store an absolute point on the timeline** | `Instant` | `Instant::epoch()`, `ofEpochSecond()`, `plus(Duration)` |
| **Represent a calendar date without a time or zone** | `LocalDate` | `LocalDate::of(2026, 9, 11)`, `plus(Period)` |
| **Represent a time of day without a date** | `LocalTime` | `LocalTime::of(14, 30)`, `midnight()` |
| **Combine a civil date and time without a time zone** | `LocalDateTime` | `LocalDateTime::of(date, time)` |
| **Add or subtract calendar units (years, months, days)** | `Period` | `Period::months(1)`, `Period::days(7)` |
| **Represent a fixed UTC displacement (+02:00, Z)** | `ZoneOffset` | `ZoneOffset::utc()`, `ZoneOffset::ofTotalSeconds(7200)` |
| **Work with a geographical time zone with daylight saving rules** | `TimeZone`, `ZoneId` | `TimeZone::of(ZoneId::of("Europe/Paris"))`, `TimeZone::system()` |
| **Attach a local date/time to a time zone and handle DST** | `ZonedDateTime` | `local.atZone(zone, resolver)`, `zdt.instant()` |
| **Resolve DST gaps or overlaps when converting local time** | `LocalDateTimeResolver` | `StrictResolver`, `EarlierResolver`, `LaterResolver`, `ShiftForwardResolver` |
| **Convert between calendar systems (Julian, Japanese, Hebrew, etc.)** | `Chronology`, `CalendarDate`, `CalendarDateTime` | `JulianChronology::instance()`, `date.toChronology(target)` |
| **Format or parse canonical ISO-8601 strings** | `Iso` | `Iso::formatLocalDate()`, `Iso::parseInstant()` |
| **Format or parse custom patterns and localized dates** | `TemporalFormatter`, `TemporalParser`, `Locale` | `TemporalFormatters::ofPattern("dd/MM/yyyy")` |
| **Work with atomic or satellite time scales (UTC, TAI, GPS)** | `TimeScales`, `TimeScale` | `TimeScales::tai()`, `TimeScales::gps()`, `instant.toTimeScale()` |
| **Measure process or thread CPU time consumed** | `ProcessCpuClock`, `ThreadCpuClock` | `SystemProcessCpuClock::instance().now()` |
| **Interop with legacy Unix timestamps** | `PosixTimestamp` | `PosixTimestamp::ofSeconds(sec, nanos)`, `systemNow()` |

---

## 3. Subsystem Architecture

### 3.1 Domain Separation

The time subsystem is organized into orthogonal layers:

```text
               ┌────────────────────────────────────────────────────────┐
               │                    k::time Namespace                   │
               └────────────────────────────────────────────────────────┘

    [Monotonic Domain]               [Timeline Domain]              [Civil Domain]
    MonotonicClock                   Instant                        LocalDate
    MonotonicInstant                 Duration                       LocalTime
    FixedMonotonicClock              Clock / SystemClock            LocalDateTime
                                                                    Period / EpochDay
                                             │                              │
                                             │                              │
                                             └──────────────┬───────────────┘
                                                            │
                                                  [Zoned Domain & Rules]
                                                  ZoneOffset / ZoneId
                                                  TimeZone / ZoneRules
                                                  ZoneLocalResolution / Resolver
                                                  ZonedDateTime
                                                            │
                                             ┌──────────────┴───────────────┐
                                             │                              │
                                   [Alternative Chronology]       [Formatting & Scales]
                                   Chronology / Era               Iso
                                   CalendarDate                   TemporalFormatter / Parser
                                   CalendarDateTime               Locale / WeekRules
                                                                  TimeScales (UTC, TAI, GPS)
```

### 3.2 The Bridge Between Civil Time and the Timeline

A `LocalDateTime` lives in human civil time. An `Instant` lives on the global
physical timeline.

To convert a `LocalDateTime` into an `Instant`, you **must** supply a
`TimeZone`. Because daylight saving time can create ambiguities, you can also
supply a `LocalDateTimeResolver`:

```text
LocalDateTime("2026-03-29T02:30:00")
                  │
                  ▼
          + TimeZone("Europe/Paris")
          + LocalDateTimeResolver (e.g. EarlierResolver)
                  │
                  ▼
            ZonedDateTime
                  │
                  ▼
          Instant (Timeline coordinate)
```

### 3.3 Adding a `Duration` vs Adding a `Period`

This distinction is crucial for correct application logic:

- **`Duration` arithmetic** operates on the timeline.
  Adding `Duration.ofHours(24)` moves forward exactly 86,400 physical seconds.
  If a daylight saving spring transition occurs during that day (where clocks
  spring forward one hour), the local clock time tomorrow will be one hour
  later!
- **`Period` arithmetic** operates on calendar fields.
  Adding `Period.days(1)` keeps the same local clock time (e.g. 09:00:00 remains
  09:00:00 tomorrow), even if the day contained 23 or 25 physical hours.

---

## 4. Practical Workflows and Examples

### 4.1 Measuring Elapsed Execution Time

For performance measurement, benchmarks, or timeouts, **always** use
`MonotonicClock` and `MonotonicInstant`. Never use wall-clock time for
benchmarking.

```k
module bench_example;

import k::io;
import k::time;

main() : int {
    // 1. Obtain the system monotonic clock (active time excludes system sleep)
    clock : MonotonicClock& = SystemMonotonicClock::activeTime();

    start : MonotonicInstant = clock.now();

    // 2. Perform work
    total : long = 0L;
    for (i : int = 0; i < 1000000; i++) {
        total += i;
    }

    end : MonotonicInstant = clock.now();

    // 3. Compute elapsed Duration
    elapsed : Duration = end - start;

    io::println("Elapsed time in microseconds: " + String::valueOf(elapsed.toMicros()));
    return 0;
}
```

### 4.2 Civil Date Arithmetic and Clamping

When dealing with contracts, subscriptions, or birthdays, use `LocalDate` and
`Period`. K automatically applies standard end-of-month clamping rules:

```k
module billing_example;

import k::io;
import k::time;

main() : int {
    // January 31, 2026
    jan31 : LocalDate = LocalDate::of(2026L, 1, 31);

    // Adding 1 month clamps to the last valid day of February (Feb 28 in 2026)
    feb : LocalDate = jan31 + Period::months(1L);
    io::println(Iso::formatLocalDate(feb)); // "2026-02-28"

    // In a leap year (2024), adding 1 month clamps to Feb 29
    jan31_leap : LocalDate = LocalDate::of(2024L, 1, 31);
    feb_leap : LocalDate = jan31_leap + Period::months(1L);
    io::println(Iso::formatLocalDate(feb_leap)); // "2024-02-29"

    // Adding 7 days to a date
    nextWeek : LocalDate = jan31 + Period::days(7L);
    io::println(Iso::formatLocalDate(nextWeek)); // "2026-02-07"

    return 0;
}
```

### 4.3 Handling Daylight Saving Transitions (Gaps and Overlaps)

In many time zones, clocks jump forward in spring (e.g. from 02:00 to 03:00,
creating a 1-hour **gap** where local times like 02:30 do not exist), and jump
backward in autumn (e.g. from 03:00 back to 02:00, creating an **overlap**
where local times like 02:30 occur twice).

K makes this handling explicit via `LocalDateTimeResolver`:

```k
module dst_example;

import k::io;
import k::time;

main() : int {
    zone : TimeZone = TimeZone::of(ZoneId::of("Europe/Paris"));

    // 2026-03-29 02:30:00 falls inside the spring DST gap in Paris
    gapLocal : LocalDateTime = LocalDateTime::of(
        LocalDate::of(2026L, 3, 29),
        LocalTime::of(2, 30)
    );

    // StrictResolver rejects nonexistent times with LocalTimeGapException
    try {
        zdtStrict : ZonedDateTime = gapLocal.atZone(zone, StrictResolver::instance());
    } catch (e: LocalTimeGapException&) {
        io::println("Correctly caught gap exception: local time does not exist!");
    }

    // ShiftForwardResolver shifts the time forward by the gap length (to 03:30)
    zdtShifted : ZonedDateTime = gapLocal.atZone(zone, ShiftForwardResolver::instance());
    io::println("Shifted time: " + Iso::formatZonedDateTime(zdtShifted));

    return 0;
}
```

### 4.4 `Duration` vs `Period` on `ZonedDateTime`

Notice how adding a duration vs a period behaves differently across a DST
boundary:

```k
module zdt_math_example;

import k::io;
import k::time;

main() : int {
    zone : TimeZone = TimeZone::of(ZoneId::of("Europe/Paris"));

    // 2026-03-28 09:00:00 (the day before the spring transition)
    start : ZonedDateTime = LocalDateTime::of(
        LocalDate::of(2026L, 3, 28),
        LocalTime::of(9, 0)
    ).atZone(zone);

    // Adding Period::days(1) keeps the same clock hour (09:00:00 on March 29)
    nextDayCivil : ZonedDateTime = start.plus(Period::days(1L), StrictResolver::instance());
    io::println("Civil day: " + Iso::formatZonedDateTime(nextDayCivil));
    // Output: 2026-03-29T09:00:00+02:00[Europe/Paris]

    // Adding Duration::ofHours(24) advances 24 physical hours.
    // Because the clock lost an hour at 02:00, 24 hours later the clock reads 10:00:00!
    nextDayPhysical : ZonedDateTime = start + Duration::ofHours(24L);
    io::println("Physical 24h: " + Iso::formatZonedDateTime(nextDayPhysical));
    // Output: 2026-03-29T10:00:00+02:00[Europe/Paris]

    return 0;
}
```

### 4.5 Formatting and Parsing with ISO-8601

The `Iso` static class provides zero-configuration, canonical ISO-8601
formatting and strict parsing for all core temporal types:

```k
module iso_example;

import k::io;
import k::time;

main() : int {
    // 1. Parsing
    d : LocalDate = Iso::parseLocalDate("2026-09-11");
    t : LocalTime = Iso::parseLocalTime("14:30:15.5");
    dur : Duration = Iso::parseDuration("PT1H30M");
    offset : ZoneOffset = ZoneOffset::parseIso("+02:00");

    // 2. Formatting
    io::println(Iso::formatLocalDate(d));     // "2026-09-11"
    io::println(Iso::formatLocalTime(t));     // "14:30:15.5"
    io::println(Iso::formatDuration(dur));    // "PT5400S"
    io::println(Iso::formatZoneOffset(offset)); // "+02:00"

    // 3. Absolute Instant in UTC
    inst : Instant = Instant::ofEpochSecond(1700000000L);
    io::println(Iso::formatInstant(inst));    // "2023-11-14T22:13:20Z"

    return 0;
}
```

### 4.6 Custom Patterns and Localized Presentation

When presenting dates to end-users or parsing non-standard text representations,
use `TemporalFormatters` and `TemporalParsers`:

```k
module pattern_example;

import k::io;
import k::time;

main() : int {
    date : LocalDate = LocalDate::of(2026L, 9, 11);

    // Custom pattern formatter
    fmt : TemporalFormatter! = TemporalFormatters::ofPattern("dd/MM/yyyy");
    io::println(fmt->formatLocalDate(date)); // "11/09/2026"

    // Custom pattern parser
    parser : TemporalParser! = TemporalParsers::ofPattern("yyyy.MM.dd");
    parsed : LocalDate = parser->parseLocalDate("2026.09.11");
    io::println(Iso::formatLocalDate(parsed)); // "2026-09-11"

    delete fmt;
    delete parser;
    return 0;
}
```

### 4.7 Alternative Chronologies (Julian Calendar)

To work with alternative calendar systems (e.g. historical dates or non-Gregorian
traditions), use `Chronology` and `CalendarDate`. Cross-chronology conversion
is performed through the calendar-neutral `EpochDay`:

```k
module chronology_example;

import k::io;
import k::time;

main() : int {
    gregorian : Chronology& = GregorianChronology::instance();
    julian : Chronology& = JulianChronology::instance();

    // In 1900, the Julian calendar was 13 days behind the Gregorian calendar
    // 1900-02-28 Julian is 1900-03-12 Gregorian
    julianDate : CalendarDate = julian.date(
        CalendarFields::of(null, 1900L, CalendarMonth::of(2, false), 28)
    );

    // Convert to Gregorian calendar
    gregorianDate : CalendarDate = julianDate.toChronology(gregorian);
    io::println("Year: " + String::valueOf(gregorianDate.fields().yearOfEra()));   // 1900
    io::println("Month: " + String::valueOf(gregorianDate.fields().month().number())); // 3
    io::println("Day: " + String::valueOf(gregorianDate.fields().day()));         // 12

    return 0;
}
```

### 4.8 Atomic Time Scales (UTC, TAI, GPS)

K supports high-precision time scales through `TimeScales`. Converting between
scales changes the **representation of the same instant**, never the physical
instant itself:

```k
module scales_example;

import k::io;
import k::time;

main() : int {
    // Current instant
    now : Instant = SystemClock::instance().now();

    // Obtain coordinates in different scales
    utcVal : TimeScaleValue = TimeScales::utc().valueFromInstant(now);
    taiVal : TimeScaleValue = TimeScales::tai().valueFromInstant(now);
    gpsVal : TimeScaleValue = TimeScales::gps().valueFromInstant(now);

    // TAI is ahead of GPS by exactly 19 seconds at all times
    diffGpsTai : long = taiVal.wholeSeconds() - gpsVal.wholeSeconds();
    io::println("TAI - GPS in seconds: " + String::valueOf(diffGpsTai)); // 19

    return 0;
}
```

---

## 5. Summary and Best Practices

1. **Use `MonotonicClock` for elapsed time and deadlines.** Never subtract
   system wall-clock readings to measure time intervals.
2. **Use `Instant` for logs, events, and database timestamps.** An `Instant` is
   unambiguous and globally ordered.
3. **Use `LocalDate` / `LocalTime` / `LocalDateTime` for civil business logic.**
   Only attach a `TimeZone` when you need to convert to an `Instant`.
4. **Choose between `Duration` and `Period` intentionally.** `Duration` is
   elapsed physical time; `Period` is human calendar math with end-of-month
   clamping.
5. **Always specify a resolver when creating a `ZonedDateTime` from local time.**
   Daylight saving transitions cause gaps and overlaps; explicit resolvers
   (`StrictResolver`, `EarlierResolver`, `ShiftForwardResolver`, etc.) make your
   application robust against DST surprises.
6. **Use `Iso` for machine interchange and `TemporalFormatter` for humans.**
   Canonical ISO-8601 formatting is fast, deterministic, and requires no locale
   configuration.
