# libk Time API - Architecture and Implementation Plan

**Status:** proposed implementation plan
**Scope:** base `libk`, namespace `::k::time`
**Normative source:** `libk-time-API-Specification.md`

## 1. Objectives and delivery rules

This plan implements the temporal model specified for K without allowing the
host operating system, C runtime, installed locale, POSIX representation, or
current TZDB contents to leak into the public API.

The implementation is intentionally split into four phases:

| Phase | Deliverable | External data or native system access |
| --- | --- | --- |
| 1 | Deterministic chronology, arithmetic, canonical ISO support | None |
| 2 | Local system clock adapters and monotonic measurement | Native C clock APIs only |
| 3 | Versioned time zones, locations, transitions, and leap-second history | System TZDB and leap-second data |
| 4 | Alternative chronologies, locale presentation, time scales, and additional time sources | ICU and optional source-specific native facilities |

The following rules apply to every phase:

- [ ] All public declarations live in `module k` and `namespace time`, and are
      used as `::k::time::Type`.
- [ ] K public types and their observable behavior remain platform independent.
      Platform names, `time_t`, `timespec`, `clock_gettime`, TZif, ICU, errno,
      and native handles remain private implementation details.
- [ ] Fundamental values are immutable and value-semantic. Constructors and
      factories establish all invariants; methods return new values.
- [ ] No operation silently changes a zone, offset, chronology, locale,
      ambiguous local time, or leap-second interpretation in order to succeed.
- [ ] Every operation with invalid input, missing required data, or ambiguity
      has an explicit result or throws a documented temporal exception.
- [ ] `Duration` is elapsed chronological time. `Period` is civil arithmetic.
      Neither is substituted for the other.
- [ ] `Instant`, `MonotonicInstant`, `PosixTimestamp`, and time-scale values
      remain different types. No implicit conversion crosses those domains.
- [ ] New public API documentation is added under `doc/spec/stdlib/` with the
      implementation that exposes it. It describes K semantics, not native
      implementation details.
- [ ] Each completed phase updates the conformance matrix in this document and
      records deliberately deferred normative requirements.

## 2. Target architecture

### 2.1 Source layout and visibility

The legacy monolithic `libk/libk/src/time.k` and `libk/libk/src/time.c` are
replaced in a controlled migration by the following dedicated subsystem. All K
files below begin with `module k;` and declare `namespace time`.

```text
libk/libk/
  src/
    time/
      errors.k
      arithmetic.k
      duration.k
      instant.k
      gregorian.k
      local_date.k
      local_time.k
      local_date_time.k
      period.k
      zone_offset.k
      iso_format.k
      iso_parse.k
      clock.k
      monotonic.k
      posix_timestamp.k
      leap_seconds.k
      zone_id.k
      zone_rules.k
      time_zone.k
      zoned_date_time.k
      local_resolution.k
      tzdb.k
      chronology.k
      calendar_date.k
      time_scale.k
      locale.k
      temporal_formatter.k
      temporal_parser.k
    runtime/
      time/
        platform_clock.h
        platform_clock_posix.c
        platform_clock_windows.c
        platform_clock_stub.c
        tzif_reader.h
        tzif_reader.c
        tzdb_system.h
        tzdb_system.c
        leap_second_data.h
        leap_second_data.c
        time_ffi.c
        icu_bridge.h
        icu_bridge.c
  tests/
    time/
      test-time-arithmetic.cpp
      test-time-iso.cpp
      test-time-clock.cpp
      test-time-tzdb.cpp
      test-time-zones.cpp
      test-time-leap-seconds.cpp
      test-time-chronology.cpp
      test-time-scales.cpp
      test-time-locale.cpp
      fixtures/
```

Only files needed by a phase are introduced in that phase. The names above are
an ownership map, not a requirement to create empty placeholder files.

The boundary is:

```text
K program
    |
    v
::k::time public values, services, errors, formatters
    |
    v
private K FFI declarations in time/*.k
    |
    v
private C bridge and immutable native snapshots
    |
    v
clock APIs, TZif files, ICU, or optional source hardware
```

The K layer owns semantic validation, the public error taxonomy, and all
platform-neutral values. Native code only obtains data, parses native formats,
and returns normalized primitive values or opaque private snapshot handles. It
must not make civil-time policy decisions on behalf of the K API.

### 2.2 Core representations

The representations below prevent the 64-bit-nanosecond range limitation and
make overflow detectable. They are private fields; callers use factories and
accessors.

| Value | Canonical internal representation | Mandatory invariant |
| --- | --- | --- |
| `Duration` | signed `long` seconds plus `int` nanosecond adjustment | `0 <= nanoAdjustment < 1_000_000_000`; the pair is normalized with floor division |
| `Instant` | signed `long` K-chronological epoch seconds plus `int` nanosecond adjustment | same normalization; the seconds are continuous timeline seconds from `1970-01-01T00:00:00Z`, not POSIX seconds |
| `MonotonicInstant` | clock-domain identity plus normalized seconds/nanoseconds | values are comparable only when their domain identities match |
| `LocalDate` | proleptic Gregorian year, month, and day; conversion through `EpochDay` | fields form a valid Gregorian date |
| `EpochDay` | signed `long` day count relative to the K Gregorian epoch | no time zone and no instant implied |
| `LocalTime` | hour, minute, second, and nanosecond | normal times have seconds `0..59`; structural leap label `60` is separately marked for later contextual validation |
| `LocalDateTime` | validated `LocalDate` plus `LocalTime` | never stores a zone, offset, or instant |
| `Period` | separate signed civil components, without conversion to a duration | application order is specified and components are not approximated as seconds |
| `ZoneOffset` | signed total seconds | range and granularity follow the published K contract |

The K chronological epoch coordinate in `Instant` is deliberately continuous:
one chronological second is present for a valid UTC leap second. It is
therefore not a `time_t` or a POSIX timestamp. Conversion between that
coordinate and UTC labels waits for the leap-second table in Phase 3.

All normalization and arithmetic use checked helpers. Addition, subtraction,
negation, scaling, division edge cases, unit conversion, carry/borrow, and
date conversion must either produce an exactly representable value or throw
`TemporalArithmeticException`; wrapping is forbidden.

### 2.3 Error and result model

`time/errors.k` establishes a small checked-exception hierarchy before public
factories are exposed:

```text
Exception
  +-- TemporalException
       +-- InvalidTemporalValueException
       +-- TemporalArithmeticException
       +-- TemporalParseException
       +-- LeapSecondException
       +-- TimeDataUnavailableException
       |    +-- ZoneRulesUnavailableException
       |    +-- SystemTimeZoneUnavailableException
       |    +-- TimeScaleDataUnavailableException
       +-- LocalTimeResolutionException
            +-- LocalTimeGapException
            +-- LocalTimeOverlapException
```

The final inheritance placement is confirmed with a small compiler feasibility
test. In particular, invalid caller-supplied fields may inherit from
`IllegalArgumentException` if that is compatible with the existing checked
exception conventions. Public factories and parsers declare their checked
exceptions. Internal constructors are only used after validation so that an
invalid public construction is never converted into an opaque construction
failure.

For a normal/gap/overlap lookup, absence and multiplicity are expected
outcomes rather than exceptions. `ZoneLocalResolution` represents them as
typed variants. If K does not yet support a suitable sealed algebraic type,
use a closed abstract base with `UniqueResolution`, `GapResolution`, and
`OverlapResolution` subclasses; do not encode the result as a nullable
offset or a boolean.

### 2.4 Public API layering

The externally visible dependency direction is one-way:

```text
Layer 0  checked arithmetic, errors, Duration, Instant, EpochDay
Layer 1  proleptic Gregorian LocalDate, LocalTime, LocalDateTime, Period
Layer 2  canonical ISO formatting/parsing and ZoneOffset
Layer 3  Clock, SystemClock, MonotonicClock, PosixTimestamp
Layer 4  ZoneId, ZoneRules, TimeZone, resolution, ZonedDateTime, leap data
Layer 5  Chronology, CalendarDate, CalendarDateTime, TimeScale, Locale,
         TemporalFormatter, TemporalParser, optional source adapters
```

Later layers may depend on earlier ones; earlier layers never depend on a
system database, locale, or native clock. `ZoneOffset` belongs to the
deterministic core even though a named `TimeZone` does not.

The calendar-and-chronology terminology addendum supersedes the earlier
conceptual `Calendar` interface name. The public rule-system type will be
`Chronology`; `CalendarDate` and `CalendarDateTime` are its values.
`LocalDate` and `LocalDateTime` remain the optimized, explicitly proleptic
Gregorian convenience API. A compatibility alias named `Calendar` is not
added unless a future API review identifies a concrete source-compatibility
need.

### 2.5 Component contracts and delegation inventory

This section is the implementation contract for sub-agents. A sub-agent owns
only the named files and tests in its assigned card. It must not change a
component owned by another card, invent a cross-layer dependency, or expose an
internal/native type. Any API ambiguity, missing K-language capability, or
change to an invariant is escalated to the coordinating agent before code is
written.

Each card identifies:

- **Owner files:** files an implementation agent creates or edits.
- **Depends on:** completed cards or immutable services it may consume.
- **Must not depend on:** layers that would violate the phase boundary.
- **Done when:** behavioral and test obligations that an agent must complete
  before handing the card back.

#### 2.5.1 Phase 1 values, helpers, and canonical tools

| Card | Owner files | Public contract and invariants | Depends on / must not depend on | Done when |
| --- | --- | --- | --- | --- |
| P1-A: temporal errors | `errors.k` | Defines the checked temporal exception hierarchy and stable error-code ranges. Factories use specific exceptions: invalid fields, overflow, lexical parse failure, absent data, leap validation, and local-time resolution are never conflated. Error objects contain K-domain context only, never a native path or errno. | Depends on base `Exception`; must not depend on clocks, TZDB, or ICU. | Invalid factories, parsers, and arithmetic failures compile with declared `throws` clauses and are distinguishable in tests. |
| P1-B: checked arithmetic | `arithmetic.k` | Private functions normalize a signed seconds/nanoseconds pair with `0 <= nanos < 1_000_000_000`, and perform checked add, subtract, negate, scale, divide, floor-divide, and carry/borrow. No public temporal class duplicates this logic. | Depends on P1-A; must not depend on `Duration`, `Instant`, calendar data, or C clock APIs. | Boundary/property vectors cover every helper; every overflow maps to `TemporalArithmeticException`. |
| P1-C: `Duration` | `duration.k` | Immutable chronological elapsed amount. Exposes unit factories, normalized components/total-unit accessors, ordered comparison, `plus`, `minus`, `multipliedBy`, and `dividedBy`. A duration has no calendar, zone, clock, or locale. Division rounding is truncation toward zero and zero division fails. | Depends on P1-A/B; must not depend on `Instant`, civil types, or native code. | Unit conversion, negative normalization, comparison, arithmetic identities, divisor edge cases, and overflows pass. |
| P1-D: `Instant` | `instant.k` | Immutable point on K's continuous global timeline. Exposes `epoch`, checked epoch-second construction, seconds/nanoseconds accessors, total chronological ordering, `plus/minus(Duration)`, and `until`. It has no `now`, zone, chronology, locale, POSIX, or UTC-text operation in Phase 1. | Depends on P1-A/B/C; must not depend on a clock, leap table, civil conversion, or TZDB. | Timeline arithmetic is exact and ordered; no generated KDI public method implies POSIX or monotonic semantics. |
| P1-E: `EpochDay` and Gregorian math | `gregorian.k` | Private-or-narrow value helpers convert a proleptic Gregorian date to/from a signed epoch-day count, calculate leap years/month lengths, and add days/months/years with last-valid-day clamping. A day count has neither zone nor instant semantics. | Depends on P1-A/B; must not depend on `Instant`, a zone, host calendar APIs, or locale. | Negative/zero years, Gregorian century rules, conversion round trips, and representational boundaries pass. |
| P1-F: `LocalDate` | `local_date.k` | Immutable validated Gregorian civil date. `of`, fields, comparison, `plus/minus(Period)`, `atTime`, and internal epoch-day conversion are provided. It contains no offset, zone, clock, or chronology chosen from the environment. | Depends on P1-A/B/E and P1-H once period methods are added; must not depend on TZDB or ICU. | Invalid field input, end-of-month clamping, BCE boundaries, and equality/order vectors pass. |
| P1-G: `LocalTime` | `local_time.k` | Immutable local clock label with hour, minute, second, and nanosecond accessors. It rejects `24:00`, preserves an explicit structural second `60` label, and does not assert that this label is a real leap second. Ordinary time arithmetic returns carry information for `LocalDateTime`; it does not consult a date or zone. | Depends on P1-A/B; must not depend on `LocalDate`, leap data, TZDB, or native time. | Normal field limits, nanosecond rollover, `:60` structural representation, and invalid labels are tested. |
| P1-H: `Period` | `period.k` | Immutable civil amount containing distinct years, months, weeks, days, hours, minutes, and seconds. It preserves fields rather than normalizing months into years or any field into `Duration`. Application order is fixed: years, months, weeks, days, then time carry. | Depends on P1-A/B; must not depend on `Duration`, zones, or host calendar facilities. | Period fields survive construction and addition exactly; month/year behavior is demonstrably not duration arithmetic. |
| P1-I: `LocalDateTime` | `local_date_time.k` | Immutable composition of a `LocalDate` and `LocalTime`; exposes split fields and pure `Period` arithmetic. It can perform documented ordinary 86,400-second local arithmetic with `Duration`, but rejects a structural leap label until Phase 3. It has no implicit instant conversion. | Depends on P1-C/F/G/H; must not depend on `Instant` UTC conversion, zones, clocks, or external data. | Split/recombine, date carry, period ordering, leap-label rejection, and no-zone semantics are covered. |
| P1-J: `ZoneOffset` | `zone_offset.k` | Immutable signed UTC displacement with `UTC`, strict factory, total-seconds accessor, comparison, and canonical ISO text. It models a fixed numeric offset only, never a geographic zone or history. | Depends on P1-A/B and P1-K; must not depend on TZDB or local-zone discovery. | Boundary/range checks, positive/negative offsets, equality, strict parse, and ISO output pass. |
| P1-K: canonical ISO formatter/parser | `iso_format.k`, `iso_parse.k` | Stateless canonical ISO-8601 tools for `Duration`, Gregorian local values, and `ZoneOffset`. Fraction rendering is exact/minimal up to nine digits. Parsing is strict and locale independent. `Instant` and named-zone text are intentionally absent until Phase 3. | Depends on P1-A through P1-J as applicable; must not depend on host locale, TZDB, or C parse APIs. | Each supported value round trips; invalid lexical and semantic forms throw the right error; changing process settings does not change output. |

The following pure implementation helpers are internal and have one owner:

- [ ] `NormalizedSecondNanos` (P1-B) is the only internal pair-normalization
      representation; callers cannot retain an invalid pair.
- [ ] `GregorianMath` (P1-E) is the only source of Gregorian leap/month/day
      rules for Phase 1 values. No class computes its own month table.
- [ ] `IsoWriter` and `IsoReader` (P1-K) share a single field-width and
      fraction-trimming policy so formatter/parser behavior cannot drift.
- [ ] `TemporalValueValidation` (P1-A or P1-B) centralizes range validation
      and creates semantic exceptions; formatter/parser cards do not invent
      duplicate error mapping.

#### 2.5.2 Phase 2 clock-domain classes and native clock tools

| Card | Owner files | Public contract and invariants | Depends on / must not depend on | Done when |
| --- | --- | --- | --- | --- |
| P2-A: `Clock` and test clocks | `clock.k` | `Clock` is the injectable absolute-time contract. `FixedClock` always returns its immutable `Instant`; `SequenceClock` returns its configured finite sequence according to documented exhaustion behavior. These are public deterministic test/simulation tools and never read system time. | Depends on P1-D; must not depend on native clocks, TZDB, or leap history. | A client can receive a `Clock` dependency and be deterministic under both test implementations. |
| P2-B: `MonotonicInstant` | `monotonic.k` | Immutable timestamp in a specific monotonic domain. It supports comparison/subtraction only after exact domain identity is confirmed; cross-domain operations throw `InvalidTemporalValueException` or a documented domain exception. It never converts to `Instant` or serializes as wall time. | Depends on P1-B/C; must not depend on `Instant`, civil types, or a zone. | Same-domain duration and cross-domain rejection vectors pass. |
| P2-C: `MonotonicClock` | `monotonic.k` | Injectable source of `MonotonicInstant`, with explicit active-time and elapsed-time capability variants. `FixedMonotonicClock` supplies deterministic values. The public API says whether suspend advances the clock; it does not expose OS clock names. | Depends on P2-B and the private FFI only for native implementation; must not depend on `Clock`, TZDB, or leap data. | Tests demonstrate domain identity, elapsed calculation, fake injection, unavailable capability, and separation from wall time. |
| P2-D: `PosixTimestamp` | `posix_timestamp.k` | Immutable normalized POSIX coordinate used solely for explicit interoperability. Its API is numeric and ISO-independent; it has no time-zone or monotonic interpretation. Conversion to/from `Instant` is declared but unavailable until the Phase 3 leap table defines its policy. | Depends on P1-A/B; must not depend on SystemClock-to-Instant conversion or TZDB. | Normalization and ordering pass, and attempted premature conversion reports unavailable data. |
| P2-E: system clock facade | `clock.k`, `posix_timestamp.k` | `SystemClock.instance()` exists as the named eventual absolute source, but `now(): Instant` throws `TimeScaleDataUnavailableException` until P3-B supplies the leap table and POSIX mapping. The explicit `PosixTimestamp.systemNow()` path returns an OS reading without claiming leap awareness. | Depends on P2-D and platform bridge; must not depend on a fabricated constant UTC/TAI offset. | Fake provider verifies success/failure mapping; host integration checks only availability/invariants. |
| P2-F: platform clock bridge | `runtime/time/platform_clock.h`, `platform_clock_posix.c`, `platform_clock_windows.c`, `platform_clock_stub.c`, `time_ffi.c` | Private C adapter returns normalized signed seconds/nanoseconds and an internal status enum for realtime, monotonic-active, monotonic-elapsed, process CPU, and thread CPU readings. It checks native errors and owns no K policy. A test seam replaces the provider without changing public ABI. | Depends on C platform headers; must not depend on K class layout, timezone APIs, TZDB, or ICU. | Native return/error normalization is testable through FFI; unsupported platforms report status, not fake readings. |

The private C status enum must distinguish at least unavailable capability,
native failure, invalid native value, and unsupported platform. `time_ffi.c`
maps that enum once to the appropriate K exception. Individual K classes must
not inspect errno or duplicate status conversion.

#### 2.5.3 Phase 3 zone, transition, and leap-second classes and tools

| Card | Owner files | Public contract and invariants | Depends on / must not depend on | Done when |
| --- | --- | --- | --- | --- |
| P3-A: `ZoneId` | `zone_id.k` | Immutable validated IANA-style identity. Equality compares normalized identity text only, not offset, rules, or host location. The validator accepts API identifiers, not filesystem paths. | Depends on P1-A/K; must not depend on TZDB loading or system-zone discovery. | Valid/invalid identifier, equality, and path-escape tests pass. |
| P3-B: leap history and UTC conversion | `leap_seconds.k`, `posix_timestamp.k`, `runtime/time/leap_second_data.h`, `leap_second_data.c` | `LeapSecondTable` is an immutable private/versioned mapping between continuous K timeline seconds, UTC labels, and `PosixLeapSecondPolicy`. It validates `:60` only at declared events. It neither implements DST nor changes `Duration`. | Depends on P1-D/F/G/I/K and native data reader; must not depend on a zone's transition table. | Pinned leap fixture proves UTC conversion, valid/invalid `:60`, and all POSIX policy boundaries. |
| P3-C: TZif parser | `runtime/time/tzif_reader.h`, `tzif_reader.c` | Private, bounds-checked decoder for TZif headers, blocks, transition indexes, types, abbreviations, and POSIX footer. It creates native normalized rules and never calls `localtime`, `mktime`, or changes `TZ`. | Depends on C file/memory APIs only; must not depend on K public values, system-zone identification, or ICU. | All valid fixture variants decode; malformed/truncated inputs reject without out-of-bounds read. |
| P3-D: system TZDB provider | `tzdb.k`, `runtime/time/tzdb_system.h`, `tzdb_system.c` | Private provider validates a configured TZDB root, finds version/leap metadata, safely loads a named TZif file, and creates immutable versioned snapshots. Reload produces a new snapshot; it never mutates an old one. | Depends on P3-B/C and P3-A validation; must not depend on C library timezone globals or locale. | Fixture roots prove version/fingerprint behavior, cache identity, reload isolation, and unavailable-data errors. |
| P3-E: `ZoneRules` | `zone_rules.k` | Immutable rule view for one snapshot. `offsetAt(Instant)` returns exactly one offset. `validOffsets(LocalDateTime)` returns 0/1/2 offsets. `transition(local)` returns a typed transition when applicable. Enumeration has explicit range and completeness semantics. | Depends on P1 values, P1-J, P3-B/D; must not depend on `TimeZone.system()` or formatter defaults. | Fixed, historical, gap, overlap, and future-rule fixtures cover all lookup paths. |
| P3-F: resolution values/policies | `local_resolution.k` | Defines `ZoneTransition`, `Gap`, `Overlap`, `ZoneLocalResolution`, and `LocalDateTimeResolver`. A unique result contains its offset/instant; a gap contains before/after and duration; overlap retains both ordered candidates. `Strict` never guesses. | Depends on P1 values/J and P3-E; must not depend on `ZonedDateTime` construction or native APIs. | Variant contents/order and each resolver policy are independently unit-tested. |
| P3-G: `TimeZone` | `time_zone.k` | Immutable pairing of `ZoneId` and one rules snapshot/version. `of(id)`, `of(id, version)`, and `fixed(offset)` are explicit. `system()` identifies a real IANA zone or fails. Equality includes effective rules version. | Depends on P1-J and P3-A/D/E; must not depend on process locale or infer zone by current offset. | Factory, fixed-zone, version-selection, equality, and no-identifiable-system-zone tests pass. |
| P3-H: `ZonedDateTime` | `zoned_date_time.k` | Immutable absolute `Instant` plus `TimeZone`; local Gregorian fields and offset are derived from that tuple. Constructors from local time consume an explicit resolver. Duration arithmetic stays on the timeline; period arithmetic is local then resolved. It is deliberately Gregorian in Phase 3; Phase 4 adds explicit chronology-aware projections. | Depends on P1-C/D/F/G/H/I, P3-B/E/F/G; must not depend on locale or native zone APIs. | Instant/local construction, derived-field consistency, all resolver results, duration-vs-period DST behavior, `withZone`, and equality pass. |
| P3-I: zone text tools | `iso_format.k`, `iso_parse.k` | Adds canonical UTC `Instant` and zoned ISO forms, including version-preserving serialization. Parsing requires the named snapshot and resolver; it has no implicit system defaults. | Depends on P3-B/G/H; must not depend on ICU/localized patterns. | Pinned UTC/zoned round trips, version mismatch, gap/overlap, and invalid leap-label tests pass. |

`ZoneRulesSnapshot` is a native implementation structure; `ZoneRules` is the
K value-semantic view. The former may contain compact transition arrays and
ref-counted memory. The latter may expose only temporal facts. A lower-level
agent working on the parser must therefore never add public K methods or make
the snapshot layout part of the FFI contract.

#### 2.5.4 Phase 4 chronology, presentation, scale, and source classes

| Card | Owner files | Public contract and invariants | Depends on / must not depend on | Done when |
| --- | --- | --- | --- | --- |
| P4-A: `Chronology` identity and Gregorian adapter | `chronology.k`, `gregorian.k` | Defines stable `ChronologyId`, `Era`, and the public `Chronology` contract: validate fields, construct dates, month/year rules, civil period arithmetic, and `to/fromEpochDay`. `GregorianChronology` delegates to Phase 1 rules and preserves existing `LocalDate` behavior. | Depends on P1-E/F/H and P1-A; must not depend on locale, zone, or ICU for Gregorian behavior. | Gregorian results remain byte-for-byte/semantically compatible with P1; identity and range vectors pass. |
| P4-B: `CalendarDate` and `CalendarDateTime` | `calendar_date.k` | Immutable chronology-specific values. Their field representation includes chronology identity, optional era, explicit leap-month identity, and validated calendar fields; it is not silently flattened to Gregorian fields. Cross-chronology conversion goes through `EpochDay` only. | Depends on P4-A, P1-G/H/I, P3-F/G for explicit `atZone`; must not depend on locale inference. | Synthetic era/leap-month chronology verifies field preservation, same-chronology equality, explicit cross-conversion, and zone-free conversion. |
| P4-C: ICU chronology bridge | `runtime/time/icu_bridge.h`, `icu_bridge.c`, `chronology.k` | Private adapter obtains explicitly requested ICU chronology data/version and converts it into validated K chronology operations. It does not return ICU objects or let ICU's default locale select semantics. | Depends on approved ICU CMake dependency and P4-A/B; must not depend on system default locale. | Pinned ICU vectors cover each enabled chronology, era, leap month, unsupported range, and provider absence. |
| P4-D: `Locale` | `locale.k` | Immutable explicit BCP 47 locale and optional explicit numbering/chronology/week-rules configuration. `Locale.system()` is opt-in and returns a resolved identifier, never an unrecorded formatter default. Locale identity is presentation-only. | Depends on P1-A; ICU provider only for data-backed validation; must not depend on a `TimeZone`. | Tag validation, equality, explicit configuration, and unavailable-provider paths pass. |
| P4-E: `TemporalFormatter` and `TemporalParser` | `temporal_formatter.k`, `temporal_parser.k` | Immutable configured formatter/parser. ISO format remains available without ICU; localized/pattern operations require explicit locale and, when relevant, chronology/zone/resolver. Parsing stages and exception mapping remain visible to callers. | Depends on P1-K, P3-I, P4-A/B/D; must not depend on process locale, current zone, or an implicit clock. | Pinned localized examples are reproducible under changed process settings and preserve temporal value identity. |
| P4-F: `TimeScale` | `time_scale.k` | Defines stable IDs and explicit coordinate types for UTC, TAI, GPS, and POSIX interop. Conversion changes representation of exactly the same `Instant`; UTC/TAI uses leap history, and TAI/GPS uses their specified fixed relation. | Depends on P1-C/D, P2-D, P3-B; must not depend on `TimeZone`, chronology, or a clock source. | Historical leap vectors verify UTC/TAI/GPS conversion and duration invariance across scales. |
| P4-G: observations and optional sources | `time_scale.k`, `clock.k`, `monotonic.k`, `runtime/time/platform_clock_*` | Defines `TimeSource`, `TimeObservation`, source identity, declared scale, resolution, optional uncertainty, and synchronization metadata. Adds distinct `ProcessCpuClock` and `ThreadCpuClock`; optional NTP/PTP/GNSS/RTC adapters follow their own capability contracts. | Depends on P2-F, P3-B, P4-F; must not depend on timezone or locale code. | Mock sources test metadata and uncertainty; CPU and external domains cannot convert implicitly to `Instant` without a declared scale conversion. |
| P4-H: chronology-aware zoned extension | `zoned_date_time.k`, `calendar_date.k` | Adds explicit chronology-aware civil projections/creation without changing the identity of the stored `Instant` or `TimeZone`. The default remains proleptic Gregorian. Changing chronology is a representation operation, not a zone resolution. | Depends on P3-H and P4-A/B; must not depend on locale defaults. | The same instant yields valid different chronology fields; round trips preserve instant and resolver behavior. |

#### 2.5.5 Native and build tools

| Tool | Owner files/configuration | Input -> output | Required behavior and prohibited shortcut |
| --- | --- | --- | --- |
| Private FFI bridge | `runtime/time/time_ffi.c` and private declarations in owning K files | normalized native structs/status -> K primitives/exceptions | One conversion point per native status family. Do not expose C structs, native pointers, errno, or a C memory ownership rule in KDI. |
| Platform clock provider | `platform_clock*.c/.h` | host clock reading -> normalized coordinate/status | Check every call, state resolution/capability internally, and distinguish unavailable from failed. Do not use wall time for elapsed deadlines. |
| TZif reader | `tzif_reader.c/.h` | validated zoneinfo bytes -> immutable transition/future-rule snapshot | No host `localtime`/`mktime` oracle, process-global `TZ`, unchecked offsets, or host-dependent parse policy. |
| System TZDB provider | `tzdb_system.c/.h` | validated root + `ZoneId` -> versioned snapshot or data error | Constrain file access to the root, snapshot file contents, and report unavailable version/data. Do not use current offset as a zone identity. |
| Leap-data reader | `leap_second_data.c/.h` | supported IANA data -> immutable leap table | Validate ordering/expiry and surface missing/malformed data. Do not treat a DST transition as a leap second. |
| ICU bridge | `icu_bridge.c/.h` plus approved root CMake dependency | explicit K locale/chronology request -> provider-neutral fields/status | Pin/report data version, avoid default C/ICU locale, and keep ICU objects private. |
| CMake source registration | root dependency configuration and `libk/libk/CMakeLists.txt` | feature options/dependency discovery -> private compile/link arguments and test target | Accumulate link arguments so optional providers do not erase `liburing`; do not introduce root dependency discovery inside the subproject CMake file. |
| KDI/doc generator | existing libk build targets | public K declarations -> `libk.kdi`, Markdown, HTML | Review generated API after each public card. Private FFI and native provider details must not appear. |
| Test fixture generator | `tests/time/fixtures/` and a documented test-only generator if needed | declarative transition/leap cases -> minimal pinned binary/text fixtures | Generated fixtures are deterministic, version-labelled, reviewable, and never copied from the host at test time. |

#### 2.5.6 Dispatch order and agent handoffs

The coordinating agent may delegate the following independent packages once
their prerequisites are complete. The named test files are owned by the same
agent as the implementation package unless the coordinator deliberately
assigns a separate review-only task.

| Package | May start after | Owned implementation cards | Owned tests | Handoff artifact |
| --- | --- | --- | --- | --- |
| D1: arithmetic foundation | namespace scaffold | P1-A, P1-B, P1-E | `test-time-arithmetic.cpp` | normalized-pair and Gregorian helper contracts, boundary vector list |
| D2: timeline values | D1 | P1-C, P1-D | `test-time-arithmetic.cpp` | `Duration`/`Instant` KDI excerpt and overflow matrix |
| D3: civil values | D1 and D2 for duration methods | P1-F, P1-G, P1-H, P1-I | `test-time-arithmetic.cpp` | period application order and local leap-label behavior |
| D4: canonical text | D2, D3, P1-J | P1-K | `test-time-iso.cpp` | accepted grammar, rejection matrix, golden strings |
| D5: clock domains | D2 | P2-A through P2-F | `test-time-clock.cpp` | native status map, capability table, fake-provider seam |
| D6: zone data core | P1-A/B/F/G/I/J and TZDB approval | P3-A through P3-D | `test-time-tzdb.cpp`, fixtures | fixture manifest, snapshot/version semantics, parsed transition contract |
| D7: zones and leap semantics | D6 and P2-D/E | P3-B, P3-E through P3-I | `test-time-zones.cpp`, `test-time-leap-seconds.cpp` | resolver truth table, POSIX policy table, serialization vectors |
| D8: chronology and locale | D7 and ICU approval for enabled providers | P4-A through P4-E, P4-H | `test-time-chronology.cpp`, `test-time-locale.cpp` | chronology field model, ICU capability/version table, locale golden vectors |
| D9: scales and sources | D5, D7 | P4-F, P4-G | `test-time-scales.cpp` | scale equations, provider capability/status table |

Before accepting a handoff, the coordinator checks the declared public KDI
surface, the owned test vectors, failure semantics, and forbidden dependencies
in that card. A lower-level agent must not mark a phase checkbox complete:
only the coordinator marks it after integrating all packages and running the
phase acceptance checklist.

#### 2.5.7 Minimum public API catalogue and prototype sketches

The following is the minimum planned API, grouped by first delivery phase. It
is intentionally a prototype sketch, not copy-and-paste source: the Phase 1
feasibility card must validate the exact K spelling of singleton references,
arrays, overloads, and checked exceptions before implementation. It is,
however, the authoritative minimum contract for delegation. An agent must not
remove, rename, or broaden a listed operation without coordinator approval.

All value receivers below are `const`; all parameters carrying another
temporal value are immutable references. Exact ownership addresser syntax is
chosen by the implementing agent after the K feasibility spike, while
preserving the no-aliasing and immutability requirements in the component
cards.

**Exceptions (P1, `errors.k`)**

```k
public class TemporalException : public Exception {
    TemporalException(code: int);
}

public class InvalidTemporalValueException : public TemporalException {
    InvalidTemporalValueException(code: int);
}

public class TemporalArithmeticException : public TemporalException {
    TemporalArithmeticException(code: int);
}

public class TemporalParseException : public TemporalException {
    TemporalParseException(code: int);
}

public class LeapSecondException : public TemporalException {
    LeapSecondException(code: int);
}

public class TimeDataUnavailableException : public TemporalException {
    TimeDataUnavailableException(code: int);
}

public class ZoneRulesUnavailableException : public TimeDataUnavailableException {
    ZoneRulesUnavailableException(code: int);
}

public class SystemTimeZoneUnavailableException : public TimeDataUnavailableException {
    SystemTimeZoneUnavailableException(code: int);
}

public class TimeScaleDataUnavailableException : public TimeDataUnavailableException {
    TimeScaleDataUnavailableException(code: int);
}

public class LocalTimeResolutionException : public TemporalException {
    LocalTimeResolutionException(code: int);
}

public class LocalTimeGapException : public LocalTimeResolutionException {
    LocalTimeGapException(code: int);
}

public class LocalTimeOverlapException : public LocalTimeResolutionException {
    LocalTimeOverlapException(code: int);
}
```

Error codes are stable category/subcode identifiers, not a replacement for
the exception subtype. An implementation agent assigns the non-overlapping
numeric ranges in `errors.k` and documents them in the public API reference.

**Foundational values and ISO tools (P1)**

```k
public final struct Duration {
    static zero() : Duration;
    static ofNanos(nanos: long) : Duration;
    static ofMicros(micros: long) : Duration throws(TemporalArithmeticException);
    static ofMillis(millis: long) : Duration throws(TemporalArithmeticException);
    static ofSeconds(seconds: long) : Duration;
    static ofSeconds(seconds: long, nanos: int) : Duration
        throws(TemporalArithmeticException);
    static ofMinutes(minutes: long) : Duration throws(TemporalArithmeticException);
    static ofHours(hours: long) : Duration throws(TemporalArithmeticException);

    const secondsPart() : long;
    const nanoAdjustment() : int;
    const toNanosExact() : long throws(TemporalArithmeticException);
    const toMicros() : long throws(TemporalArithmeticException);
    const toMillis() : long throws(TemporalArithmeticException);
    const toSeconds() : long;
    const isZero() : bool;
    const isNegative() : bool;
    const plus(other: const Duration&) : Duration throws(TemporalArithmeticException);
    const minus(other: const Duration&) : Duration throws(TemporalArithmeticException);
    const multipliedBy(scalar: long) : Duration throws(TemporalArithmeticException);
    const dividedBy(divisor: long) : Duration throws(TemporalArithmeticException);
    const compareTo(other: const Duration&) : int;
    const operator==(other: const Duration&) : bool;
    const operator<(other: const Duration&) : bool;
}

public final struct Instant {
    static epoch() : Instant;
    static ofEpochSecond(seconds: long) : Instant;
    static ofEpochSecond(seconds: long, nanos: int) : Instant
        throws(TemporalArithmeticException);

    const epochSeconds() : long;
    const nanoAdjustment() : int;
    const plus(duration: const Duration&) : Instant throws(TemporalArithmeticException);
    const minus(duration: const Duration&) : Instant throws(TemporalArithmeticException);
    const minus(other: const Instant&) : Duration throws(TemporalArithmeticException);
    const until(other: const Instant&) : Duration throws(TemporalArithmeticException);
    const compareTo(other: const Instant&) : int;
    const operator==(other: const Instant&) : bool;
    const operator<(other: const Instant&) : bool;
}

public final struct EpochDay {
    static ofDaysSinceEpoch(days: long) : EpochDay;

    const daysSinceEpoch() : long;
    const plusDays(days: long) : EpochDay throws(TemporalArithmeticException);
    const minusDays(days: long) : EpochDay throws(TemporalArithmeticException);
    const compareTo(other: const EpochDay&) : int;
    const operator==(other: const EpochDay&) : bool;
    const operator<(other: const EpochDay&) : bool;
}

public final struct Period {
    static zero() : Period;
    static of(years: long, months: long, weeks: long, days: long,
              hours: long, minutes: long, seconds: long) : Period;
    static years(value: long) : Period;
    static months(value: long) : Period;
    static weeks(value: long) : Period;
    static days(value: long) : Period;
    static hours(value: long) : Period;
    static minutes(value: long) : Period;
    static seconds(value: long) : Period;

    const yearsPart() : long;
    const monthsPart() : long;
    const weeksPart() : long;
    const daysPart() : long;
    const hoursPart() : long;
    const minutesPart() : long;
    const secondsPart() : long;
    const plus(other: const Period&) : Period throws(TemporalArithmeticException);
    const minus(other: const Period&) : Period throws(TemporalArithmeticException);
    const isZero() : bool;
    const operator==(other: const Period&) : bool;
}

public final struct LocalDate {
    static of(year: long, month: int, day: int) : LocalDate
        throws(InvalidTemporalValueException);
    static fromEpochDay(day: const EpochDay&) : LocalDate
        throws(TemporalArithmeticException);

    const year() : long;
    const month() : int;
    const day() : int;
    const dayOfYear() : int;
    const toEpochDay() : EpochDay;
    const isLeapYear() : bool;
    const lengthOfMonth() : int;
    const plus(period: const Period&) : LocalDate
        throws(TemporalArithmeticException);
    const minus(period: const Period&) : LocalDate
        throws(TemporalArithmeticException);
    const atTime(time: const LocalTime&) : LocalDateTime;
    const compareTo(other: const LocalDate&) : int;
    const operator==(other: const LocalDate&) : bool;
    const operator<(other: const LocalDate&) : bool;
}

public final struct LocalTime {
    static midnight() : LocalTime;
    static of(hour: int, minute: int) : LocalTime
        throws(InvalidTemporalValueException);
    static of(hour: int, minute: int, second: int, nano: int) : LocalTime
        throws(InvalidTemporalValueException);

    const hour() : int;
    const minute() : int;
    const second() : int;
    const nano() : int;
    const isStructuralLeapSecond() : bool;
    const plus(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const minus(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const compareTo(other: const LocalTime&) : int;
    const operator==(other: const LocalTime&) : bool;
    const operator<(other: const LocalTime&) : bool;
}

public final struct LocalDateTime {
    static of(date: const LocalDate&, time: const LocalTime&) : LocalDateTime;
    static of(year: long, month: int, day: int, hour: int, minute: int,
              second: int, nano: int) : LocalDateTime
        throws(InvalidTemporalValueException);

    const date() : LocalDate;
    const time() : LocalTime;
    const year() : long;
    const month() : int;
    const day() : int;
    const hour() : int;
    const minute() : int;
    const second() : int;
    const nano() : int;
    const plus(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException);
    const minus(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException);
    const plus(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const minus(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const compareTo(other: const LocalDateTime&) : int;
    const operator==(other: const LocalDateTime&) : bool;
    const operator<(other: const LocalDateTime&) : bool;
}

public final struct ZoneOffset {
    static utc() : ZoneOffset;
    static ofTotalSeconds(seconds: int) : ZoneOffset
        throws(InvalidTemporalValueException);
    static parseIso(text: const String&) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException);

    const totalSeconds() : int;
    const isUtc() : bool;
    const toIsoString() : String;
    const compareTo(other: const ZoneOffset&) : int;
    const operator==(other: const ZoneOffset&) : bool;
    const operator<(other: const ZoneOffset&) : bool;
}

public final class Iso {
    static formatDuration(value: const Duration&) : String;
    static formatLocalDate(value: const LocalDate&) : String;
    static formatLocalTime(value: const LocalTime&) : String;
    static formatLocalDateTime(value: const LocalDateTime&) : String;
    static formatZoneOffset(value: const ZoneOffset&) : String;

    static parseDuration(text: const String&) : Duration
        throws(TemporalParseException, TemporalArithmeticException);
    static parseLocalDate(text: const String&) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException);
    static parseLocalTime(text: const String&) : LocalTime
        throws(TemporalParseException, InvalidTemporalValueException);
    static parseLocalDateTime(text: const String&) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException);
}
```

`toNanosExact`, `toMicros`, and `toMillis` are explicitly checked because a
wide normalized value need not fit in `long` in a smaller unit. The Phase 1
agent must add a separately named saturating or truncating operation only if
there is a proven caller requirement; it must not alter these exact methods.

The Phase 1 structural leap-label rule is deliberately narrow:
`second == 60` is accepted only as `23:59:60`. It is not proof that a leap
second occurred on any particular date. All other second-60 forms are invalid
field values; Phase 3 supplies the date/history validation required to turn
this structural label into a valid UTC instant.

**Clock domains and POSIX boundary (P2)**

```k
public interface Clock {
    now() : Instant throws(TimeScaleDataUnavailableException,
                           TimeDataUnavailableException);
    resolution() : Duration throws(TimeDataUnavailableException);
}

public final class FixedClock : public Clock {
    FixedClock(instant: const Instant&);
    override now() : Instant;
    override resolution() : Duration;
}

public final class SequenceClock : public Clock {
    SequenceClock(values: const Instant[]&);
    override now() : Instant throws(TimeDataUnavailableException);
    override resolution() : Duration;
    const remaining() : int;
}

public final struct MonotonicClockId {
    const operator==(other: const MonotonicClockId&) : bool;
}

public final struct MonotonicInstant {
    const clockId() : MonotonicClockId;
    const secondsPart() : long;
    const nanoAdjustment() : int;
    const until(other: const MonotonicInstant&) : Duration
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const compareTo(other: const MonotonicInstant&) : int
        throws(InvalidTemporalValueException);
    const operator==(other: const MonotonicInstant&) : bool;
}

public interface MonotonicClock {
    id() : MonotonicClockId;
    now() : MonotonicInstant throws(TimeDataUnavailableException);
    resolution() : Duration throws(TimeDataUnavailableException);
    includesSuspend() : bool;
}

public final class FixedMonotonicClock : public MonotonicClock {
    FixedMonotonicClock(id: const MonotonicClockId&,
                        initial: const MonotonicInstant&);
    override id() : MonotonicClockId;
    override now() : MonotonicInstant;
    override resolution() : Duration;
    override includesSuspend() : bool;
}

public final class SystemClock : public Clock {
    static instance() : SystemClock&;
    override now() : Instant throws(TimeScaleDataUnavailableException,
                                    TimeDataUnavailableException);
    override resolution() : Duration throws(TimeDataUnavailableException);
}

public final class SystemMonotonicClock : public MonotonicClock {
    static activeTime() : SystemMonotonicClock&
        throws(TimeDataUnavailableException);
    static elapsedTime() : SystemMonotonicClock&
        throws(TimeDataUnavailableException);
    override id() : MonotonicClockId;
    override now() : MonotonicInstant throws(TimeDataUnavailableException);
    override resolution() : Duration throws(TimeDataUnavailableException);
    override includesSuspend() : bool;
}

public final struct PosixTimestamp {
    static ofSeconds(seconds: long, nanos: int) : PosixTimestamp
        throws(TemporalArithmeticException);
    static systemNow() : PosixTimestamp throws(TimeDataUnavailableException);

    const seconds() : long;
    const nanoAdjustment() : int;
    const compareTo(other: const PosixTimestamp&) : int;
    const operator==(other: const PosixTimestamp&) : bool;
}
```

`SequenceClock` exhaustion must throw `TimeDataUnavailableException` with a
dedicated test-clock subcode; it must not repeat the last value. A test agent
may add a cycling clock only as a separately named utility. `SystemClock.now`
becomes successful only in Phase 3 after leap-history initialization.

**Zones, UTC conversion, and civil resolution (P3)**

```k
// Phase 3 additions to values delivered in earlier phases.
public final struct Instant {
    static fromPosixTimestamp(value: const PosixTimestamp&) : Instant
        throws(TimeScaleDataUnavailableException);

    const toUtcDateTime() : LocalDateTime
        throws(TimeScaleDataUnavailableException);
    const toPosixTimestamp(policy: const PosixLeapSecondPolicy&) : PosixTimestamp
        throws(LeapSecondException, TimeScaleDataUnavailableException);
    const atZone(zone: const TimeZone&) : ZonedDateTime;
}

public final struct LocalDateTime {
    const atZone(zone: const TimeZone&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException);
    const atZone(zone: const TimeZone&,
                 resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException);
}

public final class PosixLeapSecondPolicy {
    static reject() : PosixLeapSecondPolicy&;
    static foldToPreviousSecond() : PosixLeapSecondPolicy&;
    static foldToFollowingSecond() : PosixLeapSecondPolicy&;
    const operator==(other: const PosixLeapSecondPolicy&) : bool;
}

public final class ZoneId {
    static of(name: const String&) : ZoneId
        throws(InvalidTemporalValueException);

    const name() : String;
    const operator==(other: const ZoneId&) : bool;
}

public final class ZoneTransition {
    const instant() : Instant;
    const offsetBefore() : ZoneOffset;
    const offsetAfter() : ZoneOffset;
    const localBefore() : LocalDateTime;
    const localAfter() : LocalDateTime;
    const isGap() : bool;
    const isOverlap() : bool;
    const duration() : Duration;
}

public interface ZoneLocalResolution {
    isUnique() : bool;
    isGap() : bool;
    isOverlap() : bool;
}

public final class UniqueResolution : public ZoneLocalResolution {
    const offset() : ZoneOffset;
    const instant() : Instant;
    override isUnique() : bool;
    override isGap() : bool;
    override isOverlap() : bool;
}

public final class GapResolution : public ZoneLocalResolution {
    const transition() : ZoneTransition;
    const duration() : Duration;
    override isUnique() : bool;
    override isGap() : bool;
    override isOverlap() : bool;
}

public final class OverlapResolution : public ZoneLocalResolution {
    const transition() : ZoneTransition;
    const earlierOffset() : ZoneOffset;
    const earlierInstant() : Instant;
    const laterOffset() : ZoneOffset;
    const laterInstant() : Instant;
    override isUnique() : bool;
    override isGap() : bool;
    override isOverlap() : bool;
}

public interface LocalDateTimeResolver {
    resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeGapException, LocalTimeOverlapException);
}

public final class StrictResolver : public LocalDateTimeResolver {
    static instance() : StrictResolver&;
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeGapException, LocalTimeOverlapException);
}

public final class EarlierResolver : public LocalDateTimeResolver {
    static instance() : EarlierResolver&;
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeGapException);
}

public final class LaterResolver : public LocalDateTimeResolver {
    static instance() : LaterResolver&;
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeGapException);
}

public final class PreferOffsetResolver : public LocalDateTimeResolver {
    PreferOffsetResolver(offset: const ZoneOffset&);
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeGapException, LocalTimeOverlapException);
}

public final class ShiftForwardResolver : public LocalDateTimeResolver {
    static instance() : ShiftForwardResolver&;
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeOverlapException);
}

public final class ShiftBackwardResolver : public LocalDateTimeResolver {
    static instance() : ShiftBackwardResolver&;
    override resolve(resolution: const ZoneLocalResolution&) : Instant
        throws(LocalTimeOverlapException);
}

public interface ZoneRules {
    version() : String;
    offsetAt(instant: const Instant&) : ZoneOffset;
    validOffsets(local: const LocalDateTime&) : ZoneOffset[]!;
    resolveLocal(local: const LocalDateTime&) : ZoneLocalResolution;
    transition(local: const LocalDateTime&) : ZoneTransition?;
    transitions(from: const Instant&, until: const Instant&) : ZoneTransition[]!;
}

public final class TimeZone {
    static of(id: const ZoneId&) : TimeZone throws(ZoneRulesUnavailableException);
    static of(id: const ZoneId&, version: const String&) : TimeZone
        throws(ZoneRulesUnavailableException);
    static fixed(offset: const ZoneOffset&) : TimeZone;
    static system() : TimeZone throws(SystemTimeZoneUnavailableException,
                                      ZoneRulesUnavailableException);

    const id() : ZoneId;
    const version() : String;
    const rules() : ZoneRules&;
    const operator==(other: const TimeZone&) : bool;
}

public final class ZonedDateTime {
    static fromInstant(instant: const Instant&, zone: const TimeZone&) : ZonedDateTime;
    static of(local: const LocalDateTime&, zone: const TimeZone&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException);
    static of(local: const LocalDateTime&, zone: const TimeZone&,
              resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException);

    const instant() : Instant;
    const zone() : TimeZone&;
    const offset() : ZoneOffset;
    const localDate() : LocalDate;
    const localTime() : LocalTime;
    const localDateTime() : LocalDateTime;
    const plus(duration: const Duration&) : ZonedDateTime
        throws(TemporalArithmeticException);
    const plus(period: const Period&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException,
               TemporalArithmeticException);
    const minus(duration: const Duration&) : ZonedDateTime
        throws(TemporalArithmeticException);
    const minus(period: const Period&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException,
               TemporalArithmeticException);
    const withZone(zone: const TimeZone&) : ZonedDateTime;
    const operator==(other: const ZonedDateTime&) : bool;
}

public final class Iso {
    static formatInstant(value: const Instant&) : String
        throws(TimeScaleDataUnavailableException);
    static formatZonedDateTime(value: const ZonedDateTime&) : String
        throws(TimeScaleDataUnavailableException);
    static parseInstant(text: const String&) : Instant
        throws(TemporalParseException, LeapSecondException,
               TimeScaleDataUnavailableException);
    static parseZonedDateTime(text: const String&,
                               resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(TemporalParseException, LeapSecondException,
               ZoneRulesUnavailableException, LocalTimeGapException,
               LocalTimeOverlapException);
}
```

`ZoneRules.validOffsets` returns a newly owned immutable-by-convention array
whose cardinality is exactly zero, one, or two. `transition` returns no value
outside a transition; the exact optional type spelling is verified during the
P1 K-language spike. `TimeZone.rules()` returns a non-null immutable view,
not an object mutable by a later data refresh.

**Chronologies, presentation, scales, and source observations (P4)**

```k
public final class ChronologyId {
    static of(value: const String&) : ChronologyId
        throws(InvalidTemporalValueException);
    const value() : String;
    const operator==(other: const ChronologyId&) : bool;
}

public final class Era {
    const id() : String;
    const displayName(locale: const Locale&) : String
        throws(TimeDataUnavailableException);
    const operator==(other: const Era&) : bool;
}

public final struct CalendarMonth {
    static of(number: int, isLeapMonth: bool) : CalendarMonth
        throws(InvalidTemporalValueException);
    const number() : int;
    const isLeapMonth() : bool;
    const operator==(other: const CalendarMonth&) : bool;
}

public final class CalendarFields {
    static of(era: const Era?, yearOfEra: long, month: const CalendarMonth&,
              day: int) : CalendarFields throws(InvalidTemporalValueException);
    const hasEra() : bool;
    const era() : Era?;
    const yearOfEra() : long;
    const month() : CalendarMonth;
    const day() : int;
}

public interface Chronology {
    id() : ChronologyId;
    isValid(fields: const CalendarFields&) : bool;
    date(fields: const CalendarFields&) : CalendarDate
        throws(InvalidTemporalValueException);
    daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int
        throws(InvalidTemporalValueException);
    monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException);
    isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException);
    plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    toEpochDay(date: const CalendarDate&) : EpochDay;
    fromEpochDay(day: const EpochDay&) : CalendarDate
        throws(InvalidTemporalValueException);
}

public final class GregorianChronology : public Chronology {
    static instance() : GregorianChronology&;
    // Implements every Chronology operation using the Phase 1 Gregorian rules.
}

public final class CalendarDate {
    const chronology() : Chronology&;
    const fields() : CalendarFields;
    const toEpochDay() : EpochDay;
    const plus(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const minus(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const toChronology(target: const Chronology&) : CalendarDate
        throws(InvalidTemporalValueException);
    const compareTo(other: const CalendarDate&) : int
        throws(InvalidTemporalValueException);
    const operator==(other: const CalendarDate&) : bool;
}

public final class CalendarDateTime {
    static of(date: const CalendarDate&, time: const LocalTime&) : CalendarDateTime;
    const chronology() : Chronology&;
    const date() : CalendarDate;
    const time() : LocalTime;
    const plus(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const minus(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const atZone(zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException,
               InvalidTemporalValueException);
    const toInstant(zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : Instant
        throws(LocalTimeGapException, LocalTimeOverlapException,
               InvalidTemporalValueException);
}

public final struct WeekRules {
    static iso() : WeekRules;
    static of(firstDayOfWeek: int, minimalDaysInFirstWeek: int) : WeekRules
        throws(InvalidTemporalValueException);
    const firstDayOfWeek() : int;
    const minimalDaysInFirstWeek() : int;
    const operator==(other: const WeekRules&) : bool;
}

public final class Locale {
    static of(languageTag: const String&) : Locale
        throws(InvalidTemporalValueException, TimeDataUnavailableException);
    static system() : Locale throws(TimeDataUnavailableException);
    const languageTag() : String;
    const withChronology(chronology: const Chronology&) : Locale;
    const withWeekRules(rules: const WeekRules&) : Locale;
    const operator==(other: const Locale&) : bool;
}

public interface TemporalFormatter {
    formatInstant(value: const Instant&, zone: const TimeZone&) : String
        throws(TimeDataUnavailableException);
    formatLocalDate(value: const LocalDate&) : String
        throws(TimeDataUnavailableException);
    formatLocalDateTime(value: const LocalDateTime&) : String
        throws(TimeDataUnavailableException);
    formatZonedDateTime(value: const ZonedDateTime&) : String
        throws(TimeDataUnavailableException);
    formatCalendarDateTime(value: const CalendarDateTime&) : String
        throws(TimeDataUnavailableException);
    withLocale(locale: const Locale&) : TemporalFormatter!;
    withChronology(chronology: const Chronology&) : TemporalFormatter!;
    withZone(zone: const TimeZone&) : TemporalFormatter!;
}

public interface TemporalParser {
    parseLocalDate(text: const String&) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException);
    parseLocalDateTime(text: const String&) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException);
    parseInstant(text: const String&) : Instant
        throws(TemporalParseException, LeapSecondException,
               TimeScaleDataUnavailableException);
    parseZonedDateTime(text: const String&, resolver: const LocalDateTimeResolver&)
        : ZonedDateTime throws(TemporalParseException, LeapSecondException,
                               ZoneRulesUnavailableException,
                               LocalTimeGapException, LocalTimeOverlapException);
    parseCalendarDateTime(text: const String&) : CalendarDateTime
        throws(TemporalParseException, InvalidTemporalValueException,
               TimeDataUnavailableException);
    withLocale(locale: const Locale&) : TemporalParser!;
    withChronology(chronology: const Chronology&) : TemporalParser!;
    withZone(zone: const TimeZone&) : TemporalParser!;
}

public final class TemporalFormatters {
    static iso() : TemporalFormatter!;
    static ofPattern(pattern: const String&) : TemporalFormatter!
        throws(TemporalParseException, TimeDataUnavailableException);
}

public final class TemporalParsers {
    static iso() : TemporalParser!;
    static ofPattern(pattern: const String&) : TemporalParser!
        throws(TemporalParseException, TimeDataUnavailableException);
}

public final class TimeScaleId {
    static of(value: const String&) : TimeScaleId
        throws(InvalidTemporalValueException);
    const value() : String;
    const operator==(other: const TimeScaleId&) : bool;
}

public final struct TimeScaleValue {
    const wholeSeconds() : long;
    const nanoAdjustment() : int;
    const operator==(other: const TimeScaleValue&) : bool;
}

public interface TimeScale {
    id() : TimeScaleId;
    epoch() : Instant;
    valueFromInstant(instant: const Instant&) : TimeScaleValue
        throws(TimeScaleDataUnavailableException);
    instantFromValue(value: const TimeScaleValue&) : Instant
        throws(TimeScaleDataUnavailableException, InvalidTemporalValueException);
}

public final class TimeScales {
    static utc() : TimeScale&;
    static tai() : TimeScale&;
    static gps() : TimeScale&;
}

public final class TimeSourceId {
    static of(value: const String&) : TimeSourceId
        throws(InvalidTemporalValueException);
    const value() : String;
    const operator==(other: const TimeSourceId&) : bool;
}

public final class TimeObservation {
    const instant() : Instant;
    const sourceId() : TimeSourceId;
    const scale() : TimeScale&;
    const resolution() : Duration;
    const hasUncertainty() : bool;
    const uncertainty() : Optional<Duration>;
}

public interface TimeSource {
    id() : TimeSourceId;
    scale() : TimeScale&;
    observation() : TimeObservation throws(TimeDataUnavailableException,
                                           TimeScaleDataUnavailableException);
}

public interface ProcessCpuClock {
    now() : Duration throws(TimeDataUnavailableException);
    resolution() : Duration throws(TimeDataUnavailableException);
}

public interface ThreadCpuClock {
    now() : Duration throws(TimeDataUnavailableException);
    resolution() : Duration throws(TimeDataUnavailableException);
}
```

`TemporalFormatter.with*` and `TemporalParser.with*` are functional
configuration operations: they return a new immutable formatter/parser and
never mutate a shared instance. If the available K interface rules cannot
express that return covariance, concrete immutable builder methods are used
instead; mutable global formatter state is prohibited.

No public `GnssTimeSource`, `NtpTimeSource`, `PtpTimeSource`, or `RtcTimeSource`
is introduced until its provider is approved and implemented. Each approved
source implements exactly `TimeSource`, declares its actual `TimeScale`, and
adds a component card, prototype block, capability tests, and CMake dependency
before its public type is added.

##### 2.5.7.1 Arithmetic, comparison, and assignment operators

This operator catalogue extends the minimum prototypes above. The named
methods remain the canonical implementation entry points: an operator delegates
to its corresponding named method, never duplicates the arithmetic, validation,
overflow checks, resolver policy, or comparison logic.

The exact declaration syntax shown below is supported by the K operator model
for member operators. The Phase 1 feasibility test verifies that checked
exceptions on an operator retain their `throws` contract through KDI export.
If that compiler capability is unavailable, the named method remains public
and the operator is deferred rather than losing its error semantics.

**Pure value arithmetic (P1)**

```k
public final struct Duration {
    const operator +(other: const Duration&) : Duration
        throws(TemporalArithmeticException);
    const operator -(other: const Duration&) : Duration
        throws(TemporalArithmeticException);
    const operator -() : Duration throws(TemporalArithmeticException);
    const operator *(scalar: long) : Duration
        throws(TemporalArithmeticException);
    const operator /(divisor: long) : Duration
        throws(TemporalArithmeticException);

    const operator ==(other: const Duration&) : bool;
    const operator !=(other: const Duration&) : bool;
    const operator <(other: const Duration&) : bool;
    const operator <=(other: const Duration&) : bool;
    const operator >(other: const Duration&) : bool;
    const operator >=(other: const Duration&) : bool;
}

public final struct Instant {
    const operator +(duration: const Duration&) : Instant
        throws(TemporalArithmeticException);
    const operator -(duration: const Duration&) : Instant
        throws(TemporalArithmeticException);
    const operator -(other: const Instant&) : Duration
        throws(TemporalArithmeticException);

    const operator ==(other: const Instant&) : bool;
    const operator !=(other: const Instant&) : bool;
    const operator <(other: const Instant&) : bool;
    const operator <=(other: const Instant&) : bool;
    const operator >(other: const Instant&) : bool;
    const operator >=(other: const Instant&) : bool;
}

public final struct EpochDay {
    const operator +(days: long) : EpochDay throws(TemporalArithmeticException);
    const operator -(days: long) : EpochDay throws(TemporalArithmeticException);
    const operator -(other: const EpochDay&) : long
        throws(TemporalArithmeticException);

    const operator ==(other: const EpochDay&) : bool;
    const operator !=(other: const EpochDay&) : bool;
    const operator <(other: const EpochDay&) : bool;
    const operator <=(other: const EpochDay&) : bool;
    const operator >(other: const EpochDay&) : bool;
    const operator >=(other: const EpochDay&) : bool;
}

public final struct Period {
    const operator +(other: const Period&) : Period
        throws(TemporalArithmeticException);
    const operator -(other: const Period&) : Period
        throws(TemporalArithmeticException);
    const operator -() : Period throws(TemporalArithmeticException);

    const operator ==(other: const Period&) : bool;
    const operator !=(other: const Period&) : bool;
}

public final struct LocalDate {
    const operator +(period: const Period&) : LocalDate
        throws(TemporalArithmeticException);
    const operator -(period: const Period&) : LocalDate
        throws(TemporalArithmeticException);

    const operator ==(other: const LocalDate&) : bool;
    const operator !=(other: const LocalDate&) : bool;
    const operator <(other: const LocalDate&) : bool;
    const operator <=(other: const LocalDate&) : bool;
    const operator >(other: const LocalDate&) : bool;
    const operator >=(other: const LocalDate&) : bool;
}

public final struct LocalTime {
    const operator +(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const operator -(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);

    const operator ==(other: const LocalTime&) : bool;
    const operator !=(other: const LocalTime&) : bool;
    const operator <(other: const LocalTime&) : bool;
    const operator <=(other: const LocalTime&) : bool;
    const operator >(other: const LocalTime&) : bool;
    const operator >=(other: const LocalTime&) : bool;
}

public final struct LocalDateTime {
    const operator +(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException);
    const operator -(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException);
    const operator +(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const operator -(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException);

    const operator ==(other: const LocalDateTime&) : bool;
    const operator !=(other: const LocalDateTime&) : bool;
    const operator <(other: const LocalDateTime&) : bool;
    const operator <=(other: const LocalDateTime&) : bool;
    const operator >(other: const LocalDateTime&) : bool;
    const operator >=(other: const LocalDateTime&) : bool;
}

public final struct ZoneOffset {
    const operator ==(other: const ZoneOffset&) : bool;
    const operator !=(other: const ZoneOffset&) : bool;
    const operator <(other: const ZoneOffset&) : bool;
    const operator <=(other: const ZoneOffset&) : bool;
    const operator >(other: const ZoneOffset&) : bool;
    const operator >=(other: const ZoneOffset&) : bool;
}
```

`LocalTime + Duration` intentionally wraps within the local clock day. It
cannot return a changed date, so `LocalDateTime + Duration` is the operation
to use when date carry matters. Neither operation resolves time zones. No
`LocalDate - LocalDate`, `LocalDateTime - LocalDateTime`, or
`Period * scalar` operator is introduced: those expressions have plausible
but incompatible calendar semantics and must use a future explicitly named API
if a real caller requires one.

**Clock-domain and POSIX arithmetic (P2)**

```k
public final struct MonotonicInstant {
    const operator -(other: const MonotonicInstant&) : Duration
        throws(InvalidTemporalValueException, TemporalArithmeticException);
    const operator <(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException);
    const operator <=(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException);
    const operator >(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException);
    const operator >=(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException);
    const operator ==(other: const MonotonicInstant&) : bool;
    const operator !=(other: const MonotonicInstant&) : bool;
}

public final struct PosixTimestamp {
    const operator +(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException);
    const operator -(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException);
    const operator -(other: const PosixTimestamp&) : Duration
        throws(TemporalArithmeticException);

    const operator ==(other: const PosixTimestamp&) : bool;
    const operator !=(other: const PosixTimestamp&) : bool;
    const operator <(other: const PosixTimestamp&) : bool;
    const operator <=(other: const PosixTimestamp&) : bool;
    const operator >(other: const PosixTimestamp&) : bool;
    const operator >=(other: const PosixTimestamp&) : bool;
}
```

`MonotonicInstant ==` is true only when both the domain identity and coordinate
match. Relational operations reject different domains rather than imposing an
arbitrary ordering. POSIX arithmetic is arithmetic on its declared POSIX
coordinate only; it must not be documented as leap-aware `Instant` arithmetic.

**Zone and chronology operators (P3-P4)**

```k
public final class ZoneId {
    const operator ==(other: const ZoneId&) : bool;
    const operator !=(other: const ZoneId&) : bool;
}

public final class TimeZone {
    const operator ==(other: const TimeZone&) : bool;
    const operator !=(other: const TimeZone&) : bool;
}

public final class ZonedDateTime {
    const operator +(duration: const Duration&) : ZonedDateTime
        throws(TemporalArithmeticException);
    const operator -(duration: const Duration&) : ZonedDateTime
        throws(TemporalArithmeticException);

    const isBefore(other: const ZonedDateTime&) : bool;
    const isAfter(other: const ZonedDateTime&) : bool;
    const isSameInstant(other: const ZonedDateTime&) : bool;
    const operator ==(other: const ZonedDateTime&) : bool;
    const operator !=(other: const ZonedDateTime&) : bool;
}

public final class ChronologyId {
    const operator ==(other: const ChronologyId&) : bool;
    const operator !=(other: const ChronologyId&) : bool;
}

public final class Era {
    const operator ==(other: const Era&) : bool;
    const operator !=(other: const Era&) : bool;
}

public final struct CalendarMonth {
    const operator ==(other: const CalendarMonth&) : bool;
    const operator !=(other: const CalendarMonth&) : bool;
}

public final class CalendarDate {
    const operator +(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const operator -(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const operator ==(other: const CalendarDate&) : bool;
    const operator !=(other: const CalendarDate&) : bool;
}

public final class CalendarDateTime {
    const operator +(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const operator -(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException);
    const operator ==(other: const CalendarDateTime&) : bool;
    const operator !=(other: const CalendarDateTime&) : bool;
}

public final struct WeekRules {
    const operator ==(other: const WeekRules&) : bool;
    const operator !=(other: const WeekRules&) : bool;
}

public final class Locale {
    const operator ==(other: const Locale&) : bool;
    const operator !=(other: const Locale&) : bool;
}

public final class TimeScaleId {
    const operator ==(other: const TimeScaleId&) : bool;
    const operator !=(other: const TimeScaleId&) : bool;
}

public final struct TimeScaleValue {
    const operator ==(other: const TimeScaleValue&) : bool;
    const operator !=(other: const TimeScaleValue&) : bool;
}

public final class TimeSourceId {
    const operator ==(other: const TimeSourceId&) : bool;
    const operator !=(other: const TimeSourceId&) : bool;
}
```

`ZonedDateTime` deliberately has no relational operators. Its equality includes
zone-context equivalence, while chronological order depends only on its
instant; using both definitions for `<`/`>` would violate the usual ordering
contract. `isBefore`, `isAfter`, and `isSameInstant` make the desired
chronological comparison explicit. Likewise, `CalendarDate` and
`CalendarDateTime` have no ordering operators across chronologies: callers
must convert explicitly before invoking `compareTo`.

`TimeZone`, `ZoneId`, `ChronologyId`, `Era`, `CalendarMonth`, `WeekRules`,
`Locale`, `TimeScaleId`, and `TimeSourceId` are identity/configuration values,
not quantities. They therefore have equality only. `ZoneRules`,
`ZoneTransition`, `ZoneLocalResolution`, `LocalDateTimeResolver`, clocks,
formatters, parsers, `TimeScale`, and `TimeSource` deliberately expose no
arithmetic, comparison, cast, truthiness, increment, decrement, bitwise, or
subscript operator.

**Assignment and compound-assignment policy**

K struct variables need ordinary copy assignment for ergonomic use:

```k
// Compiler-generated for every temporal struct; do not implement by hand.
operator =(other: const Duration&) : Duration&;
operator =(other: const Instant&) : Instant&;
operator =(other: const EpochDay&) : EpochDay&;
operator =(other: const Period&) : Period&;
operator =(other: const LocalDate&) : LocalDate&;
operator =(other: const LocalTime&) : LocalTime&;
operator =(other: const LocalDateTime&) : LocalDateTime&;
operator =(other: const ZoneOffset&) : ZoneOffset&;
operator =(other: const MonotonicInstant&) : MonotonicInstant&;
operator =(other: const PosixTimestamp&) : PosixTimestamp&;
operator =(other: const CalendarMonth&) : CalendarMonth&;
operator =(other: const WeekRules&) : WeekRules&;
operator =(other: const TimeScaleValue&) : TimeScaleValue&;
```

This assignment changes the caller's variable binding/storage, not the value
of an already constructed temporal object. It must remain the compiler's
ordinary memberwise assignment; no temporal class or struct defines a custom
`operator =`, move assignment, or assignment from a primitive.

Compound assignment and increments would mutate a temporal struct in place.
They are intentionally not part of this immutable API:

```k
// These are forbidden for every temporal value type:
operator +=(...) -> delete;
operator -=(...) -> delete;
operator *=(...) -> delete;
operator /=(...) -> delete;
operator ++_() -> delete;
operator --_() -> delete;
operator _++() -> delete;
operator _--() -> delete;
```

The implementation agent first checks that deleted compound declarations can
coexist with K's implicit copy assignment. If not, it omits the declarations
and adds compile-negative tests proving the same operations are unavailable.
The intended client spelling is explicit rebinding:

```k
elapsed = elapsed + Duration.ofSeconds(1);
deadline = deadline - Duration.ofMillis(250);
date = date + Period.months(1);
```

There are no implicit cast operators between `Instant`, `Duration`,
`PosixTimestamp`, `MonotonicInstant`, local civil values, zones, chronologies,
or time-scale values. Explicit factories and conversion methods are mandatory.

**Operator validation owned by the implementing cards**

- [ ] P1-A/P1-B add compile-positive and compile-negative operator declarations
      to the feasibility test before the value cards rely on them.
- [ ] P1-C through P1-J add tests proving every arithmetic operator delegates
      to its named counterpart, preserves immutability, and returns the same
      overflow/validation error.
- [ ] P1-C through P1-J test all six total-order comparisons for less-than,
      equality, and greater-than cases, including normalized negative values.
- [ ] P2-B verifies same-domain and cross-domain monotonic operator behavior;
      P2-D verifies POSIX arithmetic remains explicitly POSIX-only.
- [ ] P3-H verifies chronological `ZonedDateTime` predicates across two zones
      and asserts that relational operators do not appear in generated KDI.
- [ ] P4-A/P4-B verify chronology-aware equality and the absence of accidental
      cross-chronology ordering; P4-F verifies `TimeScaleValue` equality is
      used only within an explicitly selected `TimeScale`.
- [ ] Add compile-negative tests for compound assignment, increment/decrement,
      implicit casts, and forbidden quantity operators on zones, clocks,
      formatters, parsers, and resolver objects.

### 2.6 Migration from existing time primitives

The current `::k::Duration` and `::k::Instant` are in the root namespace, use
a single signed nanosecond count, and define `Instant` as a monotonic reading.
That conflicts with the specification, which reserves `Instant` for an
absolute chronological point and requires a separate monotonic domain.

- [ ] Inventory current `Duration`, `Instant`, `Thread.sleep`, timed waits,
      and all generated KDI references before deleting or moving symbols.
- [ ] Move the new types to `::k::time`; update libk internals to use
      `time::Duration` and `time::MonotonicClock`/`MonotonicInstant` for
      timeout and elapsed-time work.
- [ ] Do not alias the legacy monotonic `Instant` to `time::Instant`.
      If source compatibility is required, retain it temporarily under an
      explicitly deprecated compatibility name whose documentation says it is
      monotonic-only, then schedule removal.
- [ ] Replace `time.c` only after all users have moved to the new private
      bridge. No new public K declaration may bind directly to its symbols.
- [ ] Regenerate and inspect `libk.kdi` to ensure only `k::time` public types
      are exported for the new API.

## 3. Cross-cutting build, documentation, and test design

### 3.1 CMake integration

- [ ] Add each K file to `LIBK_SOURCES` in dependency order.
- [ ] Compile native time sources as individual private objects and add them
      to `C_RUNTIME_OBJS`, following the existing libk runtime-object pattern.
- [ ] Introduce a single accumulated runtime link-argument list. Do not
      overwrite the existing optional `liburing` arguments when a later
      time dependency is enabled.
- [ ] Add `libk-tests-time` through `libk_add_test`, initially with a 120
      second timeout, and keep time tests in `tests/time/`.
- [ ] Make time's optional external data and library dependencies explicit in
      top-level CMake dependency configuration, then pass only the resolved
      include paths, compile definitions, and link arguments to
      `libk/libk/CMakeLists.txt`. This respects the repository rule that
      subproject CMake files do not introduce root-level `find_package`
      configuration.
- [ ] A missing optional provider builds a capability that reports explicit
      unavailability; it never substitutes host defaults or a fabricated
      result.

### 3.2 Test levels

| Test level | Location | Purpose |
| --- | --- | --- |
| K semantic unit tests | `tests/time/test-time-*.cpp` | JIT-compile small K programs and assert the public API, as existing libk tests do |
| Deterministic property tests | same target | fixed-seed generated dates, durations, and transitions; validate algebraic identities and boundary behavior |
| Native adapter tests | same target through private test seams | validate normalized native results and error mapping without exposing C ABI publicly |
| TZDB fixture integration | `tests/time/fixtures/` | parse small checked-in TZif and leap fixtures independent of the host database |
| Host integration tests | `test-time-clock.cpp`, `test-time-tzdb.cpp` | assert capabilities and invariants only; never assume the machine zone, locale, clock accuracy, or installed TZDB release |

All deterministic tests must set their `Clock`, `TimeZone`, `Chronology`,
`Locale`, data root, and resolver explicitly. Tests must not use the current
date, the host's configured local zone, sleeping for timing assertions, or
the host TZDB's current transition rules as their oracle.

For each public type, tests cover construction, normalization, equality,
ordering when defined, hashing/map-key behavior when K provides it, formatting,
parsing, documented failure behavior, and round trips. Every native failure
path is exercised through an injected adapter or fixture.

### 3.3 Documentation and conformance checklist

- [ ] Add `doc/spec/stdlib/time.md` when Phase 1 first exposes the public
      namespace. It links to the normative specification and documents only
      implemented API.
- [ ] Document every exact error and resolver policy where it first becomes
      public.
- [ ] Add valid and invalid examples for calendar values, DST gaps and
      overlaps, leap seconds, POSIX conversion, and chronology conversion as
      each feature arrives.
- [ ] Keep `libk-time-API-Specification.md` as the normative design source;
      update it only if an intentional semantic decision changes the
      specification.
- [ ] Regenerate libk API documentation from KDI and check that private FFI
      types, native types, and provider handles are absent.

## 4. Phase 1 - deterministic temporal arithmetic and canonical formatting

**Exit criterion:** all delivered operations are pure functions of their K
arguments. They read no clock, environment variable, locale, file, TZDB, or
leap-second table.

### 4.1 Establish the namespace, error rules, and migration baseline

- [ ] Create `src/time/` and move only the replacement time API into
      `namespace time`; do not place unrelated threading logic there.
- [ ] Add `errors.k` and checked-error documentation before APIs that can
      reject input.
- [ ] Add a compile-only KDI smoke test that proves `::k::time` is a nested
      namespace of the base module and that internal FFI declarations are
      private.
- [ ] Capture baseline behavior of the legacy time primitives and all current
      thread timeout consumers. Classify each as elapsed time, absolute time,
      or compatibility behavior before changing it.

**Tests**

- [ ] A user K program reaches `::k::time::Duration` without importing `k`.
- [ ] KDI exports the intended public names and does not export a native
      bridge symbol.
- [ ] Existing thread and timeout tests continue to compile during the
      migration.

### 4.2 Build the checked arithmetic kernel

- [ ] Implement `arithmetic.k` helpers for checked signed addition,
      subtraction, negation, multiplication, division, quotient/remainder,
      floor division, and normalized second/nanosecond carry/borrow.
- [ ] Define named unit constants and checked conversion helpers for
      nanoseconds, microseconds, milliseconds, seconds, minutes, and hours.
- [ ] Define `EpochDay` conversion helpers using a proven proleptic Gregorian
      civil-date algorithm valid for negative and zero years. Keep the
      algorithm in K, with no C date functions.
- [ ] Specify and implement a single overflow policy for every public
      factory and arithmetic method: throw
      `TemporalArithmeticException`, never wrap or saturate.
- [ ] Verify with a compiler spike that signed boundary constants and checked
      operations can be expressed safely in K. If a primitive operation cannot
      be made safe in K, introduce a private deterministic C arithmetic helper
      with the same exact contract; this is still Phase 1 because it consumes
      no external data.

**Tests**

- [ ] Test `long` minimum/maximum boundaries for every helper and unit
      conversion.
- [ ] Test normalized positive and negative fractions, including borrow across
      zero such as negative one nanosecond.
- [ ] Run fixed-seed property tests for associativity where no intermediate
      overflow occurs, inverse operations, and normalization idempotence.
- [ ] Test Gregorian leap-year rules, century exceptions, month lengths,
      BCE/zero-year behavior, and `LocalDate <-> EpochDay` round trips at
      representative and extreme supported values.

### 4.3 Implement `Duration` and absolute `Instant`

- [ ] Implement immutable `Duration` with `zero`, `ofNanos`, `ofMicros`,
      `ofMillis`, `ofSeconds`, `ofMinutes`, and `ofHours`, total accessors,
      comparison, addition, subtraction, scalar multiplication, and scalar
      division.
- [ ] Specify scalar division rounding explicitly. The recommended default is
      truncation toward zero because it matches integral arithmetic; document
      the behavior for negative values and reject division by zero.
- [ ] Implement immutable `Instant` as a continuous K epoch coordinate with
      `epoch`, `ofEpochSecond`, `epochSeconds`, `nanoAdjustment`, comparison,
      `plus(Duration)`, `minus(Duration)`, and `until(Instant)`.
- [ ] Do not add `Instant.now()` in this phase. A source of current time is
      deliberately outside the deterministic core.
- [ ] Do not call this coordinate a POSIX timestamp and do not expose a
      `toEpochNanos()` accessor whose range or semantics invite that mistake.

**Tests**

- [ ] Test every unit factory and accessor, including negative values and
      fractional-second normalization.
- [ ] Test `Instant + Duration - Duration == Instant` and
      `end.minus(start) == duration` in non-overflowing cases.
- [ ] Test total chronological ordering and equality independently of all
      civil values.
- [ ] Test all arithmetic overflow, zero-divisor, and minimum-value negation
      failures.

### 4.4 Implement Gregorian civil values and `Period`

- [ ] Implement `LocalDate.of(year, month, day)` with strict proleptic
      Gregorian validation, field accessors, comparison, and conversion to
      and from `EpochDay`.
- [ ] Implement `LocalTime.of(hour, minute, second, nano)` with nanosecond
      precision. It rejects `24:00:00` and values outside ordinary field
      ranges.
- [ ] Represent a structural `23:59:60` label without claiming it is a valid
      UTC leap second. Its contextual validity, UTC conversion, and parsing
      against history are Phase 3 work.
- [ ] Implement `LocalDateTime` only as `LocalDate + LocalTime`; it has no
      offset, zone, or instant conversion in this phase.
- [ ] Implement `Period` with separate years, months, weeks, days, hours,
      minutes, and seconds. Preserve components rather than reducing months
      or years to a duration.
- [ ] Define period application order and use it everywhere: years, months
      with last-valid-day clamping, weeks, days, then time fields with
      day carry. This supplies the specification's
      `2026-01-31 + one month == 2026-02-28` rule.
- [ ] Provide `LocalDate.plus/minus(Period)` and
      `LocalDateTime.plus/minus(Period)`.
- [ ] Provide `LocalDateTime.plus/minus(Duration)` only for ordinary
      86,400-second civil-day arithmetic, and reject a structural leap-second
      operand until leap history is available. Its documentation must make
      clear that elapsed arithmetic across a real zone transition belongs on
      `Instant` after resolution.

**Tests**

- [ ] Test all field validation failures, including invalid leap days,
      invalid month ends, `24:00`, invalid nanoseconds, and uncontextualized
      invalid leap labels.
- [ ] Test end-of-month clamping in common and leap years, negative periods,
      cross-year carry, and period arithmetic around year zero.
- [ ] Test date/time split and recombination without introducing a zone.
- [ ] Test that a `Period` month is not interchangeable with 28, 30, or 31
      duration-days.

### 4.5 Deliver deterministic ISO formatting and parsing

- [ ] Implement allocation-conscious canonical ISO-8601 formatting for
      `Duration`, `LocalDate`, ordinary `LocalTime`, `LocalDateTime`, and
      `ZoneOffset`.
- [ ] Format a structural leap label as `:60` only when the value carries
      that explicit label; do not validate it against nonexistent host data.
- [ ] Define precision rendering once: omit a zero fractional component and
      otherwise emit the minimal exact decimal fraction up to nine digits.
- [ ] Implement strict canonical parsers for the same data-only values. A
      parser distinguishes lexical failure from invalid date/time fields using
      `TemporalParseException` and `InvalidTemporalValueException`.
- [ ] Defer ISO text for `Instant` and `ZonedDateTime`. Correct rendering as
      UTC needs leap-second history and correct zone rendering needs
      `ZoneRules`; a plausible POSIX-based string would be non-conforming.
- [ ] Do not add pattern syntax, locale-sensitive names, locale defaults, or
      system-default parsing in this phase.

**Tests**

- [ ] Test canonical output for positive/negative years, fractions from one
      to nine digits, negative durations, and positive/negative offsets.
- [ ] Test parse/format round trips and reject whitespace, malformed signs,
      unsupported offset shapes, invalid dates, and over-precision.
- [ ] Test that formatters produce identical results under different process
      locales and time-zone environment variables.

### 4.6 Phase 1 acceptance checklist

- [ ] `libk-tests-time` contains deterministic arithmetic, Gregorian, ISO,
      error, and property tests.
- [ ] The complete Phase 1 suite has no native clock, system zone, locale,
      environment, file, or network dependency.
- [ ] The public reference documents the distinction among `Instant`,
      `Duration`, `Period`, and `LocalDateTime`.
- [ ] The current root-level monotonic `Instant` has not been misrepresented
      as the new absolute `::k::time::Instant`.

## 5. Phase 2 - local clock readings through private C adapters

**Exit criterion:** K exposes honest clock domains and testable native-source
adapters. A POSIX wall-clock reading is not silently presented as a fully
leap-aware K `Instant`.

### 5.1 Introduce separate clock-domain types and injectable interfaces

- [ ] Define `Clock` as the absolute-time interface that ultimately returns
      `Instant`; all code that needs "now" receives a `Clock` dependency.
- [ ] Define `MonotonicClock` separately. Its `now()` returns
      `MonotonicInstant`, and subtraction is legal only in the same
      monotonic-clock domain.
- [ ] Provide deterministic `FixedClock`, `SequenceClock`, and
      `FixedMonotonicClock` implementations for tests and simulation. They
      consume no native data and may be added with Phase 2's public API.
- [ ] Define `PosixTimestamp` as an explicit interoperability value with
      normalized seconds/nanoseconds, comparison, and ISO-independent numeric
      representation. It is neither an `Instant` nor a `MonotonicInstant`.
- [ ] Define an internal `NativeClockProvider` boundary whose operations
      return normalized values plus a mapped availability/error status.
      It is injectable in native tests but not public K API.

### 5.2 Implement the local native clock adapter

- [ ] Replace direct use of the old `time.c` functions with
      `runtime/time/platform_clock.h` and private C implementation files.
- [ ] On POSIX, obtain wall-clock data from the best real-time source and
      monotonic data from the strongest relevant monotonic source. Check every
      native return code and normalize before crossing FFI.
- [ ] Provide explicitly named monotonic capabilities for active time
      (excluding suspend) and elapsed time (including suspend) when the host
      can distinguish them. If a capability is unavailable, report it
      explicitly rather than treating the two semantics as equivalent.
- [ ] Add platform-specific adapters behind the same internal interface and a
      stub implementation that reports `TimeDataUnavailableException` on
      unsupported platforms. No K public signature changes by platform.
- [ ] Never expose a native clock ID, `timespec`, `time_t`, `errno`, or C
      function name through K.

### 5.3 Preserve leap-second correctness while exposing current system time

The local system real-time API normally returns POSIX time, which cannot label
a leap second. Phase 2 must not claim otherwise.

- [ ] Expose a private/native `PosixSystemClock` adapter and explicit
      `PosixTimestamp.systemNow()` interoperability operation.
- [ ] Keep `SystemClock.instance().now(): Instant` unavailable until Phase 3
      supplies a versioned leap-second table and the documented POSIX mapping.
      It must throw `TimeScaleDataUnavailableException` instead of returning a
      semantically mislabeled value.
- [ ] Ensure existing timed waits use `MonotonicClock`/native monotonic
      deadlines, not the wall clock and not the absolute `Instant`.
- [ ] Document that an OS current-time read can jump forward, backward, or
      repeat; no Phase 2 assertion assumes monotonic wall time.

### 5.4 Phase 2 tests

- [ ] Test the K interfaces with fixed and sequence clocks; no test requiring
      deterministic behavior reads the real clock.
- [ ] Test `MonotonicInstant` subtraction, cross-domain rejection, and no
      implicit conversion to `Instant`.
- [ ] Test normalized negative and positive native results through an injected
      C provider seam and test mapping of each native failure.
- [ ] Test host wall-clock and monotonic integrations only for availability,
      representable resolution, correct domain type, and non-fabricated
      nanosecond precision.
- [ ] Test that a clock-dependent service is deterministic under `FixedClock`
      and that a wall-clock correction cannot affect monotonic elapsed-time
      logic.
- [ ] Test that `SystemClock.now()` explicitly reports missing time-scale data
      until the Phase 3 leap provider is installed.

### 5.5 Phase 2 acceptance checklist

- [ ] The old monotonic `Instant.now()` contract has been migrated or
      quarantined as a documented compatibility API.
- [ ] All native clock APIs are private and have one normalized FFI boundary.
- [ ] `MonotonicClock` and a POSIX interoperability reading work without a
      TZDB, locale, or leap-second data file.
- [ ] No public API equates POSIX seconds with K `Instant` seconds.

## 6. Phase 3 - system TZDB, locations, leap seconds, and zone resolution

**Exit criterion:** named zones are immutable snapshots of explicit system
data, their version is observable, local-time ambiguity is typed, and UTC
leap seconds are represented correctly.

### 6.1 Data-dependency approval gate

This phase uses the system IANA zoneinfo data, usually supplied by the
operating-system `tzdata` package. It does not require a foreign timezone
library: the native layer parses TZif directly so that public behavior and
snapshot lifetime remain under libk control.

- [ ] At the start of Phase 3, detect whether the configured TZDB root
      contains the required zoneinfo data, version metadata, and a supported
      leap-second source.
- [ ] If `tzdata` or equivalent system data is missing, stop and ask for
      approval before installing it. Do not install or download packages
      implicitly.
- [ ] After approval, register the dependency in CMake as a discovered data
      dependency: expose a configurable `LIBK_TIME_TZDB_ROOT`, validate the
      selected directory during configuration, and make its absence an
      explicit disabled capability or configuration error according to the
      chosen build profile. It is a runtime data dependency, not a pretend C
      link library.
- [ ] Record the configured data root and provider capability in private
      diagnostics only; do not make the path part of `TimeZone` identity or
      public serialized data.

### 6.2 Load TZDB into immutable snapshots

- [ ] Implement a bounds-checked TZif v1/v2/v3 reader in
      `runtime/time/tzif_reader.c`. It validates magic, byte counts,
      transition ordering, index bounds, signed 64-bit transition timestamps,
      abbreviations, and the version-2/3 64-bit data block.
- [ ] Read named zone files only below the configured TZDB root. Validate
      `ZoneId` syntax and reject absolute paths, empty components, `.` and
      `..` traversal, embedded native separators, and implementation-reserved
      identifiers.
- [ ] Parse the POSIX future-rule footer when present; future behavior outside
      explicit transition entries must be derived from this rule, not guessed
      from the final offset.
- [ ] Construct an immutable native `ZoneRulesSnapshot` containing normalized
      transitions, standard and wall offsets, abbreviations, future rules, and
      an identity fingerprint. Copy all needed data into the snapshot.
- [ ] Create a `TzdbSnapshot` with a stable database/version identity. Prefer
      an official IANA version from `tzdata.zi` or a version file; if absent,
      use a documented content fingerprint over the relevant database metadata
      and loaded rule/leap data. Never expose `"unknown"` as though it were a
      reproducible version.
- [ ] Cache snapshots by `(database identity, zone id)` only while preserving
      immutability. A data refresh creates a new `TzdbSnapshot`; it never
      mutates an existing `TimeZone` or `ZoneRules`.

### 6.3 Load and apply leap-second history

- [ ] Implement a private immutable `LeapSecondTable` from the configured
      TZDB's supported IANA leap-second source. Support the standard textual
      data where available and a documented TZif `right/` fallback where it is
      available; otherwise report unavailable leap history explicitly.
- [ ] Validate table ordering, duplicate events, effective dates, and
      expiration/version metadata. A malformed source is a data error, not a
      reason to ignore leap seconds.
- [ ] Define and publish the POSIX interoperability policy:
      `PosixTimestamp -> Instant` maps a POSIX label to its unique
      non-leap UTC second using the table; an actual K leap-second `Instant`
      has no bijective POSIX representation.
- [ ] Make the reverse conversion explicit through a policy, such as reject,
      fold to the preceding POSIX second, or fold to the following POSIX
      second. The default must be documented and tests cover every policy.
- [ ] Enable `SystemClock.instance().now()` only after it converts the native
      POSIX reading through this table to a K `Instant`. It never manufactures
      a leap-second observation that the OS did not provide.
- [ ] Implement `Instant.toUtcDateTime()` and strict UTC ISO parsing using
      the table. A `:60` label succeeds only on an actual leap date; ordinary
      labels remain unambiguous.

### 6.4 Implement named zones and explicit local-time resolution

- [ ] Implement immutable `ZoneId` and numerical `ZoneOffset` API completion,
      including strict ISO offset parsing and `UTC`.
- [ ] Implement `ZoneRules.offsetAt(Instant)`, `validOffsets(LocalDateTime)`,
      `transition(LocalDateTime)`, and bounded transition enumeration using
      the snapshot, never C `localtime`, `mktime`, `tzset`, or process-global
      `TZ`.
- [ ] Implement `TimeZone.of(zoneId)`, `TimeZone.of(zoneId, version)`,
      `TimeZone.fixed(offset)`, `id()`, `version()`, and `rules()`.
      Fixed zones use an immutable fixed-rule implementation and have no DST
      transitions.
- [ ] Implement `ZoneTransition`, `Gap`, `Overlap`, and the typed
      `ZoneLocalResolution` variants. A gap returns zero valid offsets, a
      normal local time one, and an overlap two, ordered explicitly by their
      chronological instants.
- [ ] Implement `LocalDateTimeResolver`: `Strict`, `Earlier`, `Later`,
      `PreferOffset`, `ShiftForward`, and `ShiftBackward`. Strict is the
      default wherever caller intent is absent. Each resolver documents its
      gap and overlap behavior.
- [ ] Implement the Phase 3 `ZonedDateTime` as an immutable tuple of
      `Instant` and `TimeZone` (including rules version), whose local
      Gregorian fields and offset are derived, not independently mutable
      state. Reserve only a private extension point for Phase 4; do not
      expose a selectable chronology before `Chronology` is public.
- [ ] Implement `Instant.atZone(zone)`, `ZonedDateTime.fromInstant`,
      `LocalDateTime.atZone(zone, resolver)`, `toInstant`, `withZone`,
      chronological `plus(Duration)`, and civil `plus(Period)`. The latter
      applies the period locally then resolves with an explicit resolver.
- [ ] Define equality exactly: `TimeZone` includes effective rule version;
      `ZonedDateTime` includes equal instant and equivalent zone context and
      does not compare rendered local text alone.

### 6.5 Identify the local system zone safely

- [ ] Implement `TimeZone.system()` as a private platform adapter that first
      accepts a valid configured IANA zone identifier, then recognizes a
      zoneinfo symlink or other documented native zone configuration.
- [ ] If local configuration cannot identify an IANA zone, throw
      `SystemTimeZoneUnavailableException`. Never infer a name from the
      current offset, locale, abbreviation, or a geographic guess.
- [ ] Keep system-zone discovery separate from TZDB loading so a caller can
      use a named zone even if host local-zone discovery is unavailable.

### 6.6 Complete machine-oriented temporal text

- [ ] Add canonical ISO formatting/parsing for `Instant`,
      `ZonedDateTime`, and reproducible zoned serialization.
- [ ] A reproducible zoned representation contains `Instant`, `ZoneId`, and
      rule/database version. Offset and local text are derived validation data,
      not a replacement for the snapshot identity.
- [ ] Parsing separates lexical errors, invalid calendar values, invalid
      leap-second labels, unavailable zone/version, gaps, and overlaps.
- [ ] No parser uses a process locale, a process default zone, or a host
      parser as an unstated fallback.

### 6.7 Phase 3 test matrix

- [ ] Add minimal checked-in TZif v2/v3 fixtures for a fixed-offset zone, a
      forward gap, a backward overlap, a historical offset change, and a
      POSIX-footer future rule. The fixtures must not require the host TZDB.
- [ ] Add a compact checked-in leap-second fixture containing ordinary,
      inserted, and boundary seconds. Test the specification's sequence
      `23:59:59`, `23:59:60`, `00:00:00`.
- [ ] Test TZif parser truncation, invalid count, invalid transition index,
      unsorted transition, malformed footer, and path-traversal rejection.
- [ ] Test `offsetAt`, `validOffsets`, gap/overlap variant contents, each
      resolver policy, and the difference between adding a duration and adding
      a period across a transition.
- [ ] Test old and new TZDB snapshots side by side to prove an existing
      `TimeZone` and `ZonedDateTime` retain their old version and behavior.
- [ ] Test every POSIX conversion policy at a leap second and prove normal
      timestamp round trips.
- [ ] Test `TimeZone.system()` through injected native configuration for a
      valid identifier, symlink, unidentifiable data, and an invalid
      configured value. Host integration only asserts either a valid
      identified zone or the explicit unavailable error.
- [ ] Test UTC and zoned ISO text/parse/serialization round trips with a
      pinned fixture database and version.

### 6.8 Phase 3 acceptance checklist

- [ ] Public zone values contain no native data path, handle, POSIX timestamp,
      or C runtime type.
- [ ] An existing zone rules object cannot change after a system TZDB update.
- [ ] Unknown zone/version, invalid leap label, gap, overlap, and unavailable
      system zone all fail explicitly and distinctly.
- [ ] A real `SystemClock` is available only with an explicit leap-data
      provider and cannot be mistaken for a monotonic measurement source.

## 7. Phase 4 - chronologies, locale presentation, time scales, and sources

**Exit criterion:** non-Gregorian civil systems, localized presentation,
absolute time-scale representations, and additional native time sources extend
the same immutable model without changing the identity of `Instant`.

### 7.1 External-library approval gate

Full locale-aware formatting/parsing and production-quality alternative
chronologies require a maintained Unicode/calendar data provider. The proposed
provider is ICU through its stable C API.

- [ ] Before enabling ICU-backed functionality, ask for approval to install
      the platform ICU development package (for example `libicu-dev`) and any
      corresponding runtime data package. Do not install it implicitly.
- [ ] After approval, add ICU as a first-class root CMake dependency with the
      required `i18n`, `uc`, and data components; pass resolved include,
      compile, and link settings to the private `icu_bridge.c` object.
- [ ] If ICU is intentionally unavailable, retain the fully functional
      Gregorian canonical ISO API from earlier phases and report explicit
      unavailability for ICU-dependent locale/chronology features. Do not
      silently delegate their semantics to the host C locale.
- [ ] Before adding NTP, PTP, GNSS, RTC, or hardware-specific provider
      libraries, repeat the same approval and CMake-dependency process for
      each concrete provider.

### 7.2 Generalize Gregorian civil values to `Chronology`

- [ ] Define stable `ChronologyId`, `Era`, `EpochDay`, and immutable
      `Chronology` interfaces. `EpochDay` is the calendar-neutral whole-day
      bridge and is never an `Instant`.
- [ ] Implement `GregorianChronology` by delegating to the Phase 1 algorithms;
      this proves that existing `LocalDate` behavior remains deterministic.
- [ ] Define `CalendarDate` and `CalendarDateTime` with an explicit
      chronology identity and validated chronology-specific field set. Support
      eras and explicit leap-month identity; do not force every chronology
      into Gregorian year/month/day assumptions.
- [ ] Ensure chronology implementations convert through `EpochDay`, define
      their own validity and period arithmetic, and expose any bounded or
      historical applicability range.
- [ ] Implement `CalendarDate.toChronology(other)` as a civil conversion
      through `EpochDay`; it introduces no zone and no `Instant`.
- [ ] Implement `CalendarDateTime.atZone(zone, resolver)` by first converting
      to the corresponding chronological local date/time and then using the
      Phase 3 resolver. A chronology never chooses a DST resolver.
- [ ] Start with proleptic Gregorian and Julian as deterministic reference
      implementations. Add ICU-backed Japanese, Buddhist, Hebrew, Islamic,
      and other chronologies only where the mapping, era, leap-month, and
      version semantics are fully specified and testable.
- [ ] Keep chronology identity and data version in values or their immutable
      context whenever a provider can revise historical rules.

### 7.3 Implement locale-aware presentation without semantic mutation

- [ ] Define immutable `Locale` from an explicit BCP 47 language tag and
      explicit optional numbering, chronology, and week-rule configuration.
      `Locale.system()` is an opt-in provider operation, never a hidden
      formatter default.
- [ ] Implement `TemporalFormatter` and `TemporalParser` as immutable
      configured objects. Canonical ISO formatters remain locale-independent;
      pattern/localized formatters require explicit locale and, where relevant,
      chronology, zone, and resolver.
- [ ] Separate parser stages and errors: lexical text, chronology field
      validity, zone ID/version lookup, leap-second validity, and local
      gap/overlap resolution.
- [ ] Formatting may render an `Instant` in a requested zone and chronology,
      but neither formatting nor parsing may mutate an `Instant`, zone
      snapshot, chronology, or locale.
- [ ] Support explicit `WeekRules`; week numbering must not be universal or
      accidentally inherited from the process locale.

### 7.4 Implement time scales and richer source observations

- [ ] Define `TimeScale`, `TimeScaleId`, and explicit scale-coordinate value
      types. A scale changes representation of an `Instant`, never the
      instant's identity.
- [ ] Implement UTC using the Phase 3 `LeapSecondTable`, TAI as a continuous
      atomic scale, and GPS with its defined epoch and fixed relation to TAI.
      TAI/UTC conversions always use the historical leap table, never today's
      offset as a constant.
- [ ] Reuse the Phase 3 `PosixLeapSecondPolicy` for all POSIX conversions and
      keep it separate from `TimeScale`.
- [ ] Define `TimeSource` and immutable `TimeObservation` with an `Instant`,
      source identity, declared scale, resolution, optional uncertainty, and
      synchronization metadata. These metadata describe an observation; they
      do not alter `Instant`.
- [ ] Complete `SystemClock` as a UTC-associated source and preserve its
      non-monotonic contract. Complete `MonotonicClock` with clearly named
      active-time and elapsed-time semantics where supported.
- [ ] Add separate `ProcessCpuClock` and `ThreadCpuClock` types. Their values
      represent consumed CPU time and are not `Instant` values.
- [ ] Add RTC, NTP, PTP, or GNSS adapters only behind separate capabilities,
      with a declared source and scale. A GNSS GPS reading becomes an
      `Instant` through `TimeScale.gps`; it is not a new kind of instant.
- [ ] If a correlation between a system and monotonic clock is exposed, model
      it as a sampled observation with uncertainty, never as an everlasting
      conversion function.

### 7.5 Phase 4 tests

- [ ] Test Gregorian and Julian conversion vectors and fixed-seed
      chronology-to-`EpochDay` round trips.
- [ ] Test a synthetic chronology with an era, a leap month, and variable
      month lengths to prove the generalized API does not rely on Gregorian
      assumptions.
- [ ] Test that cross-chronology comparison requires explicit conversion and
      that chronology conversion neither reads nor resolves a time zone.
- [ ] Test a `CalendarDateTime` through a DST gap and overlap to prove that
      chronology conversion and zone resolution remain separate.
- [ ] Test formatter/parser output using pinned ICU data and explicitly
      configured locale, chronology, week rules, zone, and resolver; run the
      same vectors with different process locale settings.
- [ ] Test TAI, UTC, and GPS against pinned leap-second vectors, including
      intervals crossing a UTC leap second. Verify the elapsed `Duration` is
      scale-independent.
- [ ] Test clock source metadata, uncertainty propagation, CPU-clock domain
      separation, and mock GNSS/NTP/PTP providers without live network or
      hardware dependencies.
- [ ] Test every unavailable optional provider returns its documented
      capability/error rather than a system default or fabricated value.

### 7.6 Phase 4 acceptance checklist

- [ ] Chronology, time zone, locale, time scale, and time source are
      independently configurable dimensions.
- [ ] The same `Instant` can be rendered in multiple zones and chronologies
      without changing equality or chronological ordering.
- [ ] No calendar arithmetic is implemented as a fixed duration.
- [ ] No localized result depends on an unspecified process locale or time
      zone.
- [ ] All optional native and library dependencies are approved, explicit in
      CMake, capability-gated, and covered by deterministic tests.

## 8. Final release checklist

- [ ] Build the focused `libk-tests-time` target and run its complete CTest
      entry.
- [ ] Run all existing libk tests after the legacy type migration, especially
      thread, synchronization, executor, I/O, UUID, and foundational-type
      tests that use durations or deadlines.
- [ ] Inspect generated KDI and generated Markdown/HTML API documentation for
      visibility, namespacing, and absence of private bridge details.
- [ ] Review every documented conversion for an implicit POSIX, locale, zone,
      calendar, or current-time assumption.
- [ ] Confirm all fixture data is pinned, minimal, legally redistributable,
      version-labelled, and independent of the host configuration.
- [ ] Confirm updates to system TZDB, leap data, ICU data, or clock
      synchronization cannot mutate previously created immutable temporal
      values.
- [ ] Mark the conformance table below only after its test evidence exists.

## 9. Conformance matrix

| Specification area | Phase | Status | Evidence to record on completion |
| --- | --- | --- | --- |
| `Duration`, `Instant`, deterministic Gregorian civil arithmetic | 1 | [ ] | `libk-tests-time` arithmetic and property cases |
| Canonical ISO data-only text | 1 | [ ] | parse/format test vectors |
| Separate wall-clock and monotonic domains | 2 | [ ] | fake-clock and native-adapter cases |
| Explicit POSIX interoperability boundary | 2-3 | [ ] | conversion-policy vectors |
| UTC leap seconds and current `SystemClock` to `Instant` | 3 | [ ] | pinned leap fixture cases |
| Versioned named zones, gaps, overlaps, and local resolvers | 3 | [ ] | pinned TZif integration cases |
| Reproducible zoned serialization | 3 | [ ] | versioned snapshot round trips |
| Alternative chronologies and `EpochDay` conversion | 4 | [ ] | Gregorian/Julian/synthetic chronology cases |
| Locale presentation and localized parsing | 4 | [ ] | pinned ICU locale cases |
| UTC, TAI, GPS, CPU clocks, and optional observations | 4 | [ ] | scale/source test vectors |
