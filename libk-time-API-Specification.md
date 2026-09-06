# K Standard Library — Time API Specification

## 1. Scope

This specification defines the time, date, calendar, time-zone, clock, and temporal formatting APIs provided by the K standard library (`libk`).

The API is designed to provide a single, platform-independent temporal model whose observable semantics do not depend on:

- the host operating system;

- the host C/C++ runtime;

- the native representation of time;

- POSIX time semantics;

- the host time-zone database implementation;

- the host locale implementation;

- the availability or behavior of a system clock;

- whether leap seconds are handled natively by the underlying platform.

Implementations may use any internal representation or operating-system facility, provided that all externally observable behavior conforms to this specification.

The API explicitly distinguishes between:

1. **absolute time** — a point on the global timeline;

2. **elapsed time** — a physical duration between two points on that timeline;

3. **civil time** — calendar dates and clock times;

4. **time-zone rules** — the mapping between civil time and the global timeline;

5. **calendar periods** — human-oriented date/time amounts;

6. **presentation** — formatting and localization.

These concepts MUST NOT be implicitly conflated.

---

# 2. Fundamental Temporal Model

## 2.1 Timeline

K defines a global chronological timeline.

A point on this timeline is represented by `Instant`.

An `Instant` is not:

- a Unix/POSIX timestamp;

- a local date/time;

- a date/time associated with a time zone;

- a value whose meaning depends on the current time-zone database.

It represents one precise point in chronological time.

The timeline is UTC-based and is capable of representing leap-second instants.

The reference epoch is:

> `1970-01-01T00:00:00Z`

The epoch is a convenient numerical reference only. It does not make K's `Instant` a POSIX timestamp.

---

## 2.2 Precision

The normative API resolution of `Instant` is **one nanosecond**.

Implementations MAY use:

- nanoseconds;

- finer-grained internal representations;

- coarser-grained system clocks.

However, externally observable `Instant` values have nanosecond precision.

If the underlying clock cannot provide nanosecond precision, the implementation MUST expose the actual representable instant according to its clock semantics; it MUST NOT manufacture precision that is not available.

---

## 2.3 Leap seconds

Leap seconds are part of the semantic model.

The civil representation

```text
23:59:60
```

is valid where the applicable UTC history defines a leap second.

A leap-second instant is a distinct point on the timeline.

Consequently:

```text
23:59:59
23:59:60
00:00:00
```

represent three consecutive chronological seconds.

An implementation MAY internally use POSIX time, TAI-derived representations, a leap-smearing clock, or another representation, but these are implementation details.

If the implementation uses a representation which cannot naturally represent leap seconds, it MUST provide an equivalent externally observable behavior.

A POSIX timestamp MUST therefore never be treated as the normative representation of `Instant`.

---

# 3. Core Types

The API is divided into the following conceptual groups.

### Group A — Absolute and elapsed time

- `Instant`

- `Duration`

### Group B — Civil calendar values

- `LocalDate`

- `LocalTime`

- `LocalDateTime`

- `Period`

### Group C — UTC offsets and time zones

- `ZoneOffset`

- `ZoneId`

- `ZoneRules`

- `TimeZone`

- `ZonedDateTime`

### Group D — Resolution of civil time

- `ZoneLocalResolution`

- `ZoneTransition`

- `Gap`

- `Overlap`

- `LocalDateTimeResolver`

### Group E — Clocks

- `Clock`

- `SystemClock`

- `MonotonicClock`

### Group F — Calendar and localization

- `Calendar`

- `Locale`

### Group G — Formatting and parsing

- `TemporalFormatter`

- `TemporalParser`

The precise module/package organization is implementation-defined, but the conceptual separation is normative.

---

# 4. `Instant`

## 4.1 Purpose

`Instant` represents one precise point on the global chronological timeline.

It is immutable and value-semantic.

Two `Instant` values are equal if and only if they represent the same point on the global timeline.

---

## 4.2 Construction

An `Instant` MAY be constructed from:

- the epoch;

- a UTC date/time;

- another `Instant` plus or minus a `Duration`;

- a system clock;

- a time-zone conversion from a valid `ZonedDateTime`;

- a serialized representation.

Example:

```k
let epoch = Instant.epoch()
let now = Instant.now()
```

---

## 4.3 Properties

Conceptually:

```k
Instant
```

provides access to its position relative to the epoch.

For example:

```k
instant.epochSeconds()
instant.nanoAdjustment()
```

These accessors expose the K temporal representation, not a POSIX timestamp.

Where a POSIX timestamp is explicitly required for interoperability, it MUST be exposed through an explicitly named conversion API.

---

## 4.4 Comparison

`Instant` is totally ordered.

```k
a < b
a <= b
a == b
a >= b
a > b
```

Ordering is chronological.

No time zone or calendar information participates in comparison.

---

## 4.5 Arithmetic

Adding a `Duration` to an `Instant` moves along the global timeline.

```k
instant + duration
instant - duration
```

Subtracting two `Instant` values produces a `Duration`.

```k
let elapsed = end - start
```

This operation is purely chronological.

For example, if a DST transition occurs between two instants, subtraction does not use local clock arithmetic.

---

## 4.6 Conversion to UTC civil time

An `Instant` can be represented in UTC:

```k
instant.toUtcDateTime()
```

The result represents the corresponding UTC civil value, including `23:59:60` where applicable.

---

## 4.7 Time-zone conversion

An `Instant` can be represented in any time zone:

```k
instant.atZone(zone)
```

This operation is always unambiguous.

A single `Instant` maps to exactly one local civil time for a given time-zone rule set.

---

# 5. `Duration`

## 5.1 Purpose

`Duration` represents an amount of elapsed chronological time.

It is independent of:

- calendar systems;

- time zones;

- daylight-saving rules;

- local civil time;

- locale.

Its semantics are therefore physical/chronological rather than civil.

---

## 5.2 Units

The normative finest unit is one nanosecond.

The API SHOULD provide constructors and accessors for:

```k
nanoseconds
microseconds
milliseconds
seconds
minutes
hours
```

A `Duration` may contain any integral number of nanoseconds representable by the implementation's specified numeric domain.

---

## 5.3 Arithmetic

```k
duration + duration
duration - duration
duration * scalar
duration / scalar
```

where mathematically meaningful.

---

## 5.4 `Instant` interaction

```k
Instant + Duration -> Instant
Instant - Duration -> Instant
Instant - Instant -> Duration
```

These operations are chronological.

For example:

```text
2026-03-29T00:30 Europe/Paris
+
1 hour
```

must NOT be interpreted as "same local clock time plus one hour".

The operation is:

1. resolve the starting `ZonedDateTime` to an `Instant`;

2. add one chronological hour;

3. convert the resulting `Instant` back to the zone.

If the DST transition removes a local hour, the resulting local time reflects that transition.

---

# 6. `LocalDate`

## 6.1 Purpose

`LocalDate` represents a calendar date without:

- time of day;

- time zone;

- UTC offset;

- global timeline position.

Example:

```k
let date = LocalDate(2026, 9, 6)
```

---

## 6.2 Calendar

`LocalDate` uses the K default proleptic Gregorian calendar unless another explicit `Calendar` is supplied.

The Gregorian calendar is therefore not an implicit property of the operating system.

---

## 6.3 Arithmetic

Date arithmetic uses `Period`, not `Duration`.

```k
date + Period.days(1)
date + Period.months(1)
date + Period.years(1)
```

Month and year arithmetic is calendar arithmetic.

For example:

```text
January 31 + 1 month
```

cannot be interpreted as a fixed number of seconds.

The result MUST follow the calendar's defined month-resolution policy.

The default policy is:

> clamp to the last valid day of the target month.

Thus:

```text
2026-01-31 + 1 month
=
2026-02-28
```

---

# 7. `LocalTime`

## 7.1 Purpose

`LocalTime` represents a time of day without:

- date;

- time zone;

- UTC offset;

- global timeline position.

---

## 7.2 Precision

`LocalTime` has nanosecond precision.

It MAY represent:

```text
00:00:00
12:34:56.123456789
23:59:59.999999999
```

The value `24:00:00` is not a normal `LocalTime` value.

---

## 7.3 Leap second

`LocalTime` MAY represent second `60` only when associated with a date/time context in which the leap second is valid.

A bare `LocalTime(23, 59, 60)` therefore does not by itself establish that a leap second exists.

Validation against an `Instant`/UTC context is required when semantic validity depends on leap-second history.

---

# 8. `LocalDateTime`

## 8.1 Purpose

`LocalDateTime` combines:

```text
LocalDate + LocalTime
```

It does not identify an instant.

A `LocalDateTime` may therefore be:

- valid and unique in a time zone;

- invalid because it falls in a DST gap;

- ambiguous because it falls in a DST overlap.

This is fundamental to the API.

---

## 8.2 Construction

```k
let local = LocalDateTime(
    LocalDate(2026, 3, 29),
    LocalTime(2, 30)
)
```

This value alone does not identify a point on the global timeline.

---

## 8.3 Arithmetic

Arithmetic with `Duration` is permitted when explicitly requested as timeline-oriented arithmetic, but civil/calendar arithmetic MUST use `Period`.

For civil manipulation:

```k
local + Period.days(1)
local + Period.months(1)
```

For elapsed-time manipulation after resolving to an instant:

```k
zoned.toInstant() + Duration.hours(24)
```

The distinction MUST remain explicit.

---

# 9. `Period`

## 9.1 Purpose

`Period` represents a calendar-relative amount.

Its components may include:

```text
years
months
weeks
days
hours
minutes
seconds
```

However, date-based components and time-based components have distinct semantics.

---

## 9.2 Calendar semantics

The meaning of:

```k
date + Period.months(1)
```

is calendar-based.

It is not equivalent to:

```k
date + Duration.days(30)
```

and MUST NOT be implemented as such.

---

## 9.3 Zoned arithmetic

When a `Period` is added to a `ZonedDateTime`, the operation is local/civil arithmetic.

Conceptually:

1. obtain the local date/time;

2. apply the period using calendar rules;

3. resolve the resulting local date/time against the same time zone;

4. apply the specified gap/overlap resolution policy.

This is deliberately different from `Duration` arithmetic.

---

# 10. `ZoneOffset`

## 10.1 Purpose

`ZoneOffset` represents a fixed displacement from UTC.

Examples:

```text
Z
+01:00
+05:30
-04:00
```

It contains no historical or future rule information.

---

## 10.2 Properties

A `ZoneOffset` is immutable and value-semantic.

Its value is an offset in seconds, with the API-defined precision required for UTC offsets.

---

## 10.3 Fixed zones

A `ZoneOffset` can be used as a fixed time zone.

For example:

```k
let utc = TimeZone.fixed(ZoneOffset.UTC)
let parisWinter = TimeZone.fixed(ZoneOffset(+01:00))
```

A fixed zone never performs DST transitions or historical rule changes.

---

# 11. `ZoneId`

## 11.1 Purpose

`ZoneId` identifies a named time zone.

Examples include:

```text
Europe/Paris
America/New_York
Asia/Tokyo
UTC
```

A `ZoneId` is an identity, not a set of rules.

This distinction is essential.

The string:

```text
Europe/Paris
```

does not by itself contain the historical or future transition rules.

---

## 11.2 Equality

`ZoneId` equality compares zone identity.

It does not compare the current offset.

Thus two different zones having the same current offset are not equal.

---

# 12. `ZoneRules`

## 12.1 Purpose

`ZoneRules` defines the temporal rules associated with a `ZoneId` for a particular time-zone database version.

It describes:

- historical offsets;

- future transitions;

- daylight-saving transitions;

- standard offsets;

- gaps;

- overlaps.

---

## 12.2 Versioning

Rules are immutable and versioned.

A `ZoneRules` instance MUST NOT silently change its behavior during its lifetime.

If the TZDB is updated, the implementation creates a new rule set/version.

Existing `TimeZone` and `ZonedDateTime` instances retain their semantic rule version.

This prevents an object created yesterday from changing meaning merely because the host updated its time-zone database today.

---

## 12.3 Offset lookup

Given an `Instant`:

```k
rules.offsetAt(instant)
```

returns exactly one offset.

Given a `LocalDateTime`:

```k
rules.validOffsets(local)
```

returns:

- zero offsets for a gap;

- one offset for a normal local time;

- two offsets for an overlap.

This is the fundamental operation for resolving local civil time.

---

# 13. `TimeZone`

## 13.1 Purpose

`TimeZone` binds:

```text
ZoneId + ZoneRules version
```

into a stable time-zone context.

It is therefore more than a string identifier.

---

## 13.2 Construction

```k
TimeZone.of(ZoneId("Europe/Paris"))
```

resolves the zone using the implementation's current/default TZDB.

An explicit database/version MAY be supplied:

```k
TimeZone.of(zoneId, tzdbVersion)
```

---

## 13.3 System time zone

```k
TimeZone.system()
```

returns the host's configured time zone.

If the host does not provide an identifiable time zone, this operation MUST fail explicitly.

The implementation MUST NOT silently substitute:

- UTC;

- the process locale;

- a guessed geographical zone;

- a fixed offset.

---

## 13.4 Fixed zones

```k
TimeZone.fixed(offset)
```

creates a zone with no transitions.

---

## 13.5 TZDB version

The API MUST expose the rule/database version associated with a `TimeZone`.

For example:

```k
zone.id()
zone.version()
zone.rules()
```

This is required for reproducibility and serialization.

---

# 14. `ZonedDateTime`

## 14.1 Purpose

`ZonedDateTime` represents a local civil representation of a specific `Instant` in a specific time zone.

Semantically it contains:

```text
Instant
ZoneId
ZoneRules version
```

The local date/time and offset are derived from these values.

---

## 14.2 Invariant

A `ZonedDateTime` always identifies exactly one `Instant`.

Consequently:

```k
zoned.toInstant()
```

is unambiguous.

---

## 14.3 Construction from an `Instant`

```k
instant.atZone(zone)
```

is always valid.

The local date/time and offset are calculated from the zone rules applicable to the instant.

---

## 14.4 Construction from local time

Constructing a `ZonedDateTime` from:

```text
LocalDateTime + TimeZone
```

may be ambiguous or invalid.

The API MUST NOT silently select an arbitrary interpretation.

The resolver MUST explicitly deal with:

- normal local times;

- gaps;

- overlaps.

---

# 15. Local-Time Resolution

## 15.1 General model

For a given:

```text
LocalDateTime + ZoneRules
```

there are three possible cases.

### Normal time

Exactly one offset applies.

```text
LocalDateTime -> one Instant
```

### Gap

No offset applies.

```text
LocalDateTime -> no Instant
```

This occurs when clocks move forward.

### Overlap

Two offsets apply.

```text
LocalDateTime -> two possible Instants
```

This occurs when clocks move backward.

---

# 16. `ZoneLocalResolution`

The API exposes the result of resolving a local date/time against zone rules.

Conceptually:

```k
ZoneLocalResolution
```

has three variants:

```text
Unique(offset, instant)
Gap(transition)
Overlap(earlier, later)
```

The exact K representation may be an algebraic/variant type.

The important semantic requirement is that ambiguity is explicit.

---

# 17. `ZoneTransition`

`ZoneTransition` describes a discontinuity in local time.

It contains at least:

- transition `Instant`;

- offset before;

- offset after;

- local date/time before;

- local date/time after;

- transition type.

The transition is classified as:

```text
Gap
Overlap
```

---

# 18. Gaps

A gap occurs when:

```text
offsetAfter > offsetBefore
```

in the relevant local-time interpretation.

A range of local times does not exist on the global timeline.

Example:

```text
01:59:59
03:00:00
```

means that:

```text
02:30
```

does not exist in that zone on that date.

The API MUST NOT silently construct an `Instant` for such a value unless the caller explicitly requests a gap-resolution policy.

---

# 19. Overlaps

An overlap occurs when the offset decreases.

A local time may then correspond to two different instants.

For example:

```text
02:30 +02:00
02:30 +01:00
```

are distinct instants.

The API MUST preserve this distinction.

An overlap resolver may select:

- the earlier instant;

- the later instant;

- a specific offset;

- reject the ambiguity.

The default strict construction API MUST reject an ambiguous local time unless an explicit policy is supplied.

---

# 20. `LocalDateTimeResolver`

This type defines policies for resolving a local date/time into an instant.

Supported policies include conceptually:

```text
Strict
Earlier
Later
PreferOffset
ShiftForward
ShiftBackward
```

### `Strict`

- gap → error;

- overlap → error.

This is the default for APIs where silent temporal changes would be dangerous.

### `Earlier`

- overlap → earlier instant;

- gap → error.

### `Later`

- overlap → later instant;

- gap → error.

### `ShiftForward`

For a gap, shift the local time forward by the gap duration.

For an overlap, use the implementation-defined default explicit policy or earlier occurrence.

### `ShiftBackward`

Analogous, but shifts a gap backward.

Resolver policy MUST be explicit whenever ambiguity affects correctness.

---

# 21. Time-Zone Historical Changes

Time-zone rules are historical data.

A zone such as:

```text
Europe/Paris
```

does not have one immutable offset.

Its offset is a function of time:

```text
offset = f(zone, instant, rule-version)
```

The API therefore MUST support historical queries.

For example:

```k
zone.offsetAt(instant)
```

may return different values for different historical instants.

The same zone may have:

- changed standard offset;

- introduced DST;

- abolished DST;

- changed DST rules;

- undergone political changes.

These changes MUST be represented by `ZoneRules`.

---

# 22. Future Time-Zone Rules

Future zone rules are inherently dependent on the available rule database.

The API MUST NOT claim that future behavior is an eternal property of a zone.

A `ZonedDateTime` created using a specific rule version remains associated with that rule version.

Applications requiring reproducible scheduling MUST therefore be able to retain the zone rule/database version.

---

# 23. Rule Updates

An implementation MAY update its TZDB independently of the application.

Such an update MUST NOT mutate existing immutable `ZoneRules`.

Consequently:

```text
TimeZone(version A)
```

and:

```text
TimeZone(version B)
```

may coexist.

The application may explicitly choose which rule set to use.

---

# 24. Serialization

Serialization of an absolute time MUST preserve enough information to recover the same `Instant`.

Serialization of a `ZonedDateTime` intended to be reproducible MUST preserve:

1. the `Instant`;

2. the `ZoneId`;

3. the rule/database version.

Serializing only:

```text
2026-10-25T02:30:00+02:00[Europe/Paris]
```

may be insufficient for every reproducibility requirement if historical rule data can later change.

The implementation SHOULD therefore provide a canonical serialization form containing the rule version when reproducibility is required.

---

# 25. `Clock`

## 25.1 Purpose

`Clock` provides a source of current `Instant` values.

It abstracts the source of wall-clock time from temporal calculations.

---

## 25.2 Interface

Conceptually:

```k
interface Clock {
    now(): Instant
}
```

A clock MUST be injectable.

Code MUST NOT be required to access the global system clock directly.

This permits:

- testing;

- simulation;

- deterministic execution;

- replay;

- virtual time.

---

# 26. `SystemClock`

`SystemClock` represents the system's best available real-time clock.

Its value is wall-clock time.

It is not guaranteed to be monotonic.

The clock may therefore:

- jump forward;

- jump backward;

- be corrected by NTP;

- be adjusted by an administrator;

- reflect system time corrections.

It MUST NOT be used to measure elapsed durations where monotonicity is required.

---

# 27. `MonotonicClock`

`MonotonicClock` measures elapsed time using a monotonic source.

It MUST NOT move backward.

Its numerical origin has no civil-time meaning.

It MUST NOT be convertible directly to `Instant`.

Its intended use is:

```k
let start = clock.now()
...
let elapsed = clock.now() - start
```

for performance measurement, timeout handling, scheduling, and similar operations.

---

# 28. Relationship Between System and Monotonic Clocks

The API MUST NOT assume that:

```text
MonotonicClock + fixed offset = Instant
```

for all time.

The two clocks represent different concepts.

A platform MAY correlate them internally, but this is not part of the stable API contract.

---

# 29. `Calendar`

## 29.1 Purpose

`Calendar` defines calendar-system rules.

The default K calendar is the proleptic Gregorian calendar.

The calendar abstraction exists so that calendar arithmetic is not intrinsically tied to the host platform.

---

## 29.2 Operations

A calendar provides operations conceptually including:

```k
daysInMonth(year, month)
isLeapYear(year)
dayOfWeek(date)
weekOfYear(date)
resolve(year, month, day)
```

Calendar arithmetic MUST be delegated to the calendar rather than implemented using fixed durations.

---

# 30. `Locale`

## 30.1 Purpose

`Locale` describes cultural presentation conventions.

Locale is independent of time zone.

A locale may define:

- language;

- numbering conventions;

- month names;

- weekday names;

- date formatting conventions;

- time formatting conventions;

- decimal symbols.

---

## 30.2 Independence from time zone

The following concepts MUST remain separate:

```text
Locale
TimeZone
Calendar
```

For example:

```k
locale = Locale("fr-FR")
zone   = TimeZone.of("America/New_York")
```

is completely valid.

Changing the locale MUST NOT change the time zone.

Changing the time zone MUST NOT change the locale.

---

# 31. Formatting

## 31.1 General principle

Formatting is presentation.

Temporal values MUST NOT depend on formatting rules for their semantic identity.

A formatter converts a temporal value into text.

---

## 31.2 `TemporalFormatter`

Conceptually:

```k
formatter.format(value)
```

The formatter may be:

- locale-aware;

- pattern-based;

- ISO-based;

- custom.

---

## 31.3 Canonical formats

The API SHOULD provide standard ISO-oriented formats for:

- `Instant`;

- `LocalDate`;

- `LocalTime`;

- `LocalDateTime`;

- `ZoneOffset`;

- `ZonedDateTime`.

Canonical machine-readable formats MUST NOT depend on the current locale.

---

# 32. Parsing

## 32.1 `TemporalParser`

Parsing converts text into a temporal value.

Parsing MUST distinguish:

- syntactic validity;

- calendar validity;

- time-zone validity;

- gap/overlap resolution;

- leap-second validity.

A parser MUST NOT silently interpret an invalid or ambiguous value differently merely because the host platform has a different locale or time-zone database.

---

# 33. ISO / Machine Representation

Machine-oriented representations SHOULD use ISO-8601-compatible forms.

Examples:

```text
2026-09-06
12:34:56.123456789
2026-09-06T12:34:56.123456789
2026-09-06T12:34:56Z
2026-09-06T12:34:56+02:00
2026-09-06T12:34:56+02:00[Europe/Paris]
```

Leap seconds may be represented as:

```text
2016-12-31T23:59:60Z
```

where that instant is defined by UTC leap-second history.

---

# 34. Time-Zone Conversion Rules

The following conversions are normative.

### `Instant -> ZonedDateTime`

Always succeeds.

```k
instant.atZone(zone)
```

### `ZonedDateTime -> Instant`

Always succeeds.

```k
zoned.toInstant()
```

### `LocalDateTime -> ZonedDateTime`

May fail or be ambiguous.

```k
zone.resolve(local, resolver)
```

### `LocalDate + LocalTime`

Always produces a `LocalDateTime`.

No time-zone semantics are introduced.

---

# 35. Absolute vs Civil Arithmetic

This distinction is one of the central rules of the API.

## Absolute arithmetic

Uses `Duration`.

```k
instant + Duration.hours(24)
```

This means exactly 24 chronological hours.

## Civil arithmetic

Uses `Period`.

```k
zoned + Period.days(1)
```

This means one calendar day in the local civil time of the zone.

These two operations may produce different instants.

For example, across a DST transition:

```text
civil + 1 day
```

does not necessarily equal:

```text
absolute + 24 hours
```

This difference is intentional and normative.

---

# 36. Example: DST Spring Transition

Suppose:

```text
Europe/Paris
2026-03-29
```

contains a transition from UTC+01 to UTC+02.

A local time such as:

```text
2026-03-29T02:30
```

does not exist.

Therefore:

```k
zone.resolve(
    LocalDateTime(2026, 3, 29, 2, 30),
    Strict
)
```

MUST fail.

An explicitly configured `ShiftForward` resolver may instead produce the corresponding valid local time after the gap.

The behavior MUST never depend on whatever arbitrary policy the host C library happens to use.

---

# 37. Example: DST Autumn Overlap

Suppose:

```text
Europe/Paris
```

moves from UTC+02 to UTC+01.

Then:

```text
2026-10-25T02:30
```

may occur twice.

The API MUST expose both possibilities.

Conceptually:

```k
let resolution = zone.rules().resolve(local)
```

returns:

```text
Overlap(
    02:30+02:00,
    02:30+01:00
)
```

The caller can explicitly select the desired occurrence.

---

# 38. Leap-Second Example

For a valid leap second:

```text
23:59:59
23:59:60
00:00:00
```

are three consecutive seconds.

Therefore:

```k
instantOf(00:00:00) - instantOf(23:59:59)
```

is:

```text
2 seconds
```

when the interval contains that leap second.

This is deliberately different from POSIX arithmetic.

---

# 39. POSIX Interoperability

The API SHOULD provide explicit interoperability functions such as:

```k
instant.toPosixTimestamp()
Instant.fromPosixTimestamp(...)
```

These APIs MUST explicitly document that POSIX time does not represent leap seconds.

Conversion may therefore be:

- non-bijective;

- lossy;

- dependent on the defined leap-second conversion policy.

POSIX interoperability MUST never redefine the semantics of `Instant`.

---

# 40. Host Platform Independence

A conforming implementation may use:

- Linux `clock_gettime`;

- Windows system clocks;

- BSD clock APIs;

- ICU;

- system TZDB;

- embedded TZDB;

- custom astronomical/UTC implementations.

None of these implementation choices may alter the API semantics.

The implementation MAY expose capabilities indicating unavailable functionality, but MUST NOT silently change semantic definitions.

---

# 41. Failure Semantics

Temporal operations MUST distinguish at least:

- invalid calendar date;

- invalid local time;

- nonexistent local time caused by a gap;

- ambiguous local time caused by an overlap;

- unavailable time-zone identifier;

- unavailable requested TZDB version;

- invalid leap-second representation;

- unavailable system time zone;

- unavailable or unsuitable system clock.

Errors MUST be explicit.

A conforming implementation MUST NOT silently substitute a different zone, offset, calendar, or instant merely to make an operation succeed.

---

# 42. Immutability

All fundamental temporal values SHOULD be immutable:

```text
Instant
Duration
LocalDate
LocalTime
LocalDateTime
Period
ZoneOffset
ZoneId
ZoneRules
TimeZone
ZonedDateTime
```

Operations create new values.

This ensures temporal values can safely be:

- shared;

- cached;

- used as map keys;

- passed between threads;

- retained across TZDB updates.

---

# 43. Equality Semantics

Different temporal types have deliberately different equality semantics.

### `Instant`

Equality means same point on the global timeline.

### `Duration`

Equality means same elapsed amount.

### `LocalDate`

Equality means same calendar date.

### `LocalTime`

Equality means same local clock value.

### `LocalDateTime`

Equality means same local civil value.

### `ZoneOffset`

Equality means same numerical UTC offset.

### `ZoneId`

Equality means same zone identity.

### `TimeZone`

Equality MUST include the effective rule version, because two instances of the same `ZoneId` backed by different rule sets are not necessarily semantically equivalent.

### `ZonedDateTime`

Equality SHOULD require the same `Instant` and equivalent zone context.

An implementation MUST NOT reduce `ZonedDateTime` equality merely to local date/time text.

---

# 44. Recommended API Surface

A minimal normative API can therefore be summarized as follows.

```text
Instant
    epoch()
    now(clock?)
    toUtcDateTime()
    atZone(zone)
    plus(duration)
    minus(duration)
    until(other) -> Duration

Duration
    ofNanos(...)
    ofMicros(...)
    ofMillis(...)
    ofSeconds(...)
    ofMinutes(...)
    ofHours(...)
    plus(...)
    minus(...)
    multipliedBy(...)
    dividedBy(...)

LocalDate
    year()
    month()
    day()
    plus(period)
    minus(period)
    atTime(time)

LocalTime
    hour()
    minute()
    second()
    nano()
    plus(duration)
    minus(duration)

LocalDateTime
    date()
    time()
    plus(period)
    plus(duration)
    minus(...)
    atZone(zone, resolver?)

Period
    years(...)
    months(...)
    weeks(...)
    days(...)
    hours(...)
    minutes(...)
    seconds(...)

ZoneOffset
    UTC
    ofSeconds(...)
    totalSeconds()

ZoneId
    of(...)
    name()

ZoneRules
    offsetAt(instant)
    validOffsets(localDateTime)
    transition(localDateTime)
    transitions(...)

TimeZone
    of(zoneId)
    of(zoneId, version)
    fixed(offset)
    system()
    id()
    version()
    rules()

ZonedDateTime
    of(local, zone, resolver)
    fromInstant(instant, zone)
    instant()
    zone()
    offset()
    localDate()
    localTime()
    localDateTime()
    plus(duration)
    plus(period)
    minus(...)
    withZone(zone)

Clock
    now()

SystemClock
    instance()

MonotonicClock
    now()
    elapsedSince(...)

Calendar
    default()
    isLeapYear(...)
    daysInMonth(...)
    ...

Locale
    system()
    of(...)
```

---

# 45. Design Rules Summary

The following rules are normative and constitute the core philosophy of the API.

### Rule 1 — `Instant` is the global timeline

An `Instant` identifies one point in chronological time and is independent of locale and time zone.

### Rule 2 — `Duration` is elapsed time

`Duration` represents chronological elapsed time and is used for timeline arithmetic.

### Rule 3 — `Period` is civil/calendar time

`Period` represents calendar-relative quantities and is used for civil arithmetic.

### Rule 4 — Local time does not imply an instant

`LocalDateTime` alone does not identify a point on the global timeline.

### Rule 5 — Time-zone resolution is not always bijective

A local time may correspond to zero, one, or two instants.

### Rule 6 — Ambiguity must be explicit

Gaps and overlaps MUST NOT be silently hidden by implementation-defined behavior.

### Rule 7 — Zone identity and zone rules are different concepts

`ZoneId` identifies a zone; `ZoneRules` defines its behavior.

### Rule 8 — Zone rules are versioned

Changing the TZDB MUST NOT mutate the semantics of existing temporal objects.

### Rule 9 — Locale and time zone are independent

Formatting language/culture does not determine geographical time-zone rules.

### Rule 10 — Leap seconds belong to the temporal model

The API's `Instant` semantics are not defined by POSIX time.

### Rule 11 — POSIX is an interoperability format

POSIX timestamps are explicitly defined conversions, not the underlying semantic model.

### Rule 12 — Wall clocks and monotonic clocks are different

A system wall clock provides civil/absolute time; a monotonic clock provides elapsed-time measurement.

### Rule 13 — The host platform does not define semantics

Operating-system behavior is an implementation detail.

### Rule 14 — Temporal ambiguity must be represented in the type system

Where an operation may produce zero or multiple results, the API MUST expose this rather than silently selecting an arbitrary result.

### Rule 15 — Reproducibility requires rule-version awareness

Applications that need stable historical/future interpretation MUST be able to retain the time-zone database version used for resolution.

---

# 46. Conceptual Model

The complete model can be understood as the following transformation graph:

```text
                         Calendar
                            │
                            ▼
LocalDate ────────┐
                  ├──► LocalDateTime
LocalTime ────────┘          │
                             │
                             │ ZoneRules
                             ▼
                      0 / 1 / 2 offsets
                             │
                      Resolver policy
                             │
                             ▼
                         Instant
                             │
                             │ + ZoneRules
                             ▼
                      ZonedDateTime
                             │
                             ├── LocalDate
                             ├── LocalTime
                             ├── ZoneId
                             └── ZoneOffset


Instant ◄──────────── Duration ────────────► Instant

LocalDate / LocalDateTime ◄──── Period ────►
```

The essential architectural principle is that **civil time and absolute time meet only at an explicit time-zone resolution boundary**.

Everything before that boundary is civil/calendar semantics.

Everything after that boundary is chronological semantics.

---

# 47. Implementation Conformance

An implementation conforms to this specification if observable behavior is equivalent to the semantic model defined above.

In particular, an implementation is conforming even if internally it uses:

```text
POSIX timestamps
system time_t
Windows FILETIME
TAI
UTC + leap-second tables
ICU
IANA TZDB
system zoneinfo
custom time-zone tables
```

provided that those mechanisms are hidden behind the normative K semantics.

Conversely, an implementation is non-conforming if it exposes host-specific behavior such as:

```text
"02:30 automatically means the first occurrence"
"02:30 automatically means the second occurrence"
"nonexistent times are silently normalized"
"unknown system timezone becomes UTC"
"POSIX timestamp arithmetic defines Duration"
"updating TZDB mutates existing ZonedDateTime semantics"
"locale determines timezone"
```

without an explicit API contract permitting that behavior.

---

# 48. Guiding Principle

The K time API is intentionally designed around one fundamental separation:

> **A civil time is a human/calendar description. An instant is a point on the physical chronological timeline. A time zone is the rule system that relates the two.**

No one of these concepts is allowed to masquerade as another.

This separation is what makes the API capable of correctly representing:

- ordinary dates and times;

- historical time-zone changes;

- DST gaps;

- DST overlaps;

- arbitrary fixed offsets;

- leap seconds;

- TZDB version changes;

- reproducible historical computations;

- future schedules;

- system-clock irregularities;

- monotonic elapsed-time measurement;

- localized presentation;

without making the semantic behavior dependent on the platform on which `libk` happens to run.









# Addendum — Alternative Calendar Systems

## A.1. Purpose

This addendum specifies how alternative calendar systems are integrated into the K temporal API.

The primary objective is to allow the standard library to support calendars other than the default proleptic Gregorian calendar without altering the semantics of:

- `Instant`;

- `Duration`;

- `ZoneOffset`;

- `ZoneId`;

- `ZoneRules`;

- `TimeZone`;

- chronological comparison;

- time-zone transitions;

- leap-second handling.

A calendar system is a **civil representation and arithmetic system**.

It is not a property of the global timeline.

---

# A.2. Fundamental Principle

A calendar defines how a chronological date is represented as a civil date.

It therefore participates in the following conceptual transformation:

```text
                  Calendar
                     │
                     ▼
Instant ───────► Civil Date/Time
                     │
                     ▼
                  Calendar
                     │
                     ▼
                  Instant
```

The calendar MUST NOT participate in:

```text
Instant <-> Duration
Instant <-> ZoneOffset
Instant <-> ZoneRules
Instant comparison
Duration arithmetic
```

In particular, the same `Instant` MUST have the same chronological identity regardless of the calendar used to represent it.

For example, an instant may simultaneously be represented as:

```text
Gregorian:       2026-09-06
Julian:          2026-08-24
Hebrew:          ...
Islamic:         ...
```

These are different civil representations of the same chronological instant.

---

# A.3. `Calendar`

`Calendar` represents a calendar system.

It defines:

- eras;

- years;

- months;

- days;

- month lengths;

- leap-year rules;

- conversion to and from a canonical chronological date representation;

- civil arithmetic;

- field validation;

- field interpretation.

A `Calendar` is therefore analogous to a mathematical coordinate system for civil dates.

---

# A.4. Calendar Identity

A calendar MUST have a stable identity.

Examples include:

```text
Gregorian
Julian
Hebrew
Islamic
Japanese
Buddhist
```

The identity MUST NOT be inferred from the locale.

For example:

```text
Locale("fr-FR")
```

does not imply:

```text
Gregorian
```

and:

```text
Locale("ar")
```

does not imply a particular Islamic calendar.

Locale and calendar are independent concepts.

---

# A.5. Default Calendar

The K standard library defines the **proleptic Gregorian calendar** as the default calendar.

This means that `LocalDate` and related civil types without an explicitly specified calendar use the proleptic Gregorian calendar.

For example:

```k
let date = LocalDate(2026, 9, 6)
```

is Gregorian.

This default MUST be deterministic and MUST NOT depend on the host operating system or locale.

---

# A.6. Calendar-Aware Civil Types

The introduction of alternative calendars requires distinguishing between:

1. calendar-neutral civil values;

2. calendar-specific civil values.

The existing `LocalDate` / `LocalDateTime` types are therefore defined as **Gregorian civil types** when used without an explicit calendar.

Alternative calendars MUST NOT silently reinterpret an existing `LocalDate`.

Instead, the API SHOULD introduce calendar-parametrized civil types.

Conceptually:

```k
CalendarDate<C>
CalendarDateTime<C>
```

where `C` identifies the calendar.

If K's generic type system makes this inconvenient for the standard library, the implementation MAY instead use:

```k
CalendarDate
CalendarDateTime
```

with an explicit `Calendar` field.

The semantic distinction remains mandatory.

---

# A.7. `CalendarDate`

`CalendarDate` represents a date interpreted according to a particular `Calendar`.

Conceptually:

```text
CalendarDate =
    Calendar
    + calendar-specific date fields
```

For example:

```k
let gregorian = Gregorian.date(2026, 9, 6)
let julian    = Julian.date(2026, 8, 24)
```

These are different civil values.

They MAY nevertheless represent the same chronological day when converted through their respective calendar systems.

---

# A.8. Calendar Date vs `LocalDate`

`LocalDate` remains the simple Gregorian civil date type.

It is therefore equivalent to:

```text
GregorianCalendarDate
```

for purposes of this specification.

`CalendarDate` is the generalized form required when an application explicitly uses an alternative calendar.

This avoids making every ordinary date operation unnecessarily calendar-generic.

---

# A.9. `CalendarDateTime`

`CalendarDateTime` combines:

```text
CalendarDate
+
LocalTime
```

under a specific calendar.

It does not contain:

- a time zone;

- an offset;

- an `Instant`.

Consequently:

```text
CalendarDateTime
```

has the same fundamental limitation as `LocalDateTime`: it does not identify a point on the global timeline.

---

# A.10. Calendar and Time Zones

Calendars and time zones are orthogonal.

A complete civil representation may therefore be expressed as:

```text
Calendar
+
Local Date/Time
+
TimeZone
```

For example:

```k
let local = HebrewDateTime(...)
let zone  = TimeZone.of("Europe/Paris")
```

The calendar defines the interpretation of the date fields.

The time zone defines the relationship between the resulting civil date/time and the global timeline.

Neither concept replaces the other.

---

# A.11. Conversion from `Instant`

A calendar-aware representation MUST be obtainable from an `Instant`.

Conceptually:

```k
instant.toCalendarDate(calendar)
instant.toCalendarDateTime(calendar, zone)
```

The conversion process is:

```text
Instant
   │
   ├── TimeZone / ZoneRules
   ▼
Gregorian-like chronological civil date/time
   │
   └── Calendar
        ▼
CalendarDateTime
```

More precisely, the implementation MUST derive the local chronological day and time according to the selected time zone and then express the calendar date using the selected calendar system.

The selected calendar MUST NOT modify the underlying instant or zone offset.

---

# A.12. Conversion to `Instant`

A calendar-aware date/time can be resolved to an `Instant` by supplying a time zone and an explicit resolution policy.

Conceptually:

```k
calendarDateTime.atZone(zone, resolver)
```

The process is:

```text
CalendarDateTime
       │
       ▼
Calendar interpretation
       │
       ▼
Chronological LocalDateTime
       │
       │ + ZoneRules
       ▼
0 / 1 / 2 possible Instants
       │
       ▼
Resolver
       │
       ▼
Instant
```

The same gap/overlap semantics defined for `LocalDateTime` apply unchanged.

---

# A.13. Calendar Conversion MUST NOT Resolve Time Zones

Calendar conversion alone does not perform time-zone resolution.

For example:

```k
calendarDateTime.toGregorian()
```

does not produce an `Instant`.

Likewise:

```k
calendarDateTime.toCalendar(otherCalendar)
```

does not require a time zone.

Calendar conversion is a civil-to-civil operation.

Time-zone resolution is a separate operation.

---

# A.14. Calendar-to-Calendar Conversion

A date MAY be converted directly between calendars.

For example:

```k
let julianDate = ...
let gregorianDate = julianDate.toCalendar(Gregorian)
```

This conversion MUST preserve the same **civil day**, not a clock duration.

Internally, the implementation MAY use a canonical day number such as:

- Julian Day Number;

- Rata Die;

- another mathematically equivalent representation.

The choice of internal representation is implementation-defined.

The canonical representation MUST NOT be exposed as the semantic identity of `Instant`.

---

# A.15. Calendar Conversion and `Instant`

When converting:

```text
Calendar A -> Calendar B
```

without a time zone, the operation preserves the represented civil date/day.

When converting:

```text
Instant -> Calendar A -> Calendar B
```

the result MUST represent the same `Instant`.

Consequently:

```text
instant
    -> Calendar A
    -> Calendar B
```

MUST NOT alter the instant.

---

# A.16. Calendar Arithmetic

Calendar arithmetic belongs to the calendar-specific civil layer.

For example:

```k
date.plus(Period.months(1))
```

must use the selected calendar's month definitions.

This is particularly important for calendars whose:

- months have variable lengths;

- years have variable lengths;

- leap months exist;

- eras change;

- year numbering differs from Gregorian;

- month numbering is not equivalent to Gregorian numbering.

---

# A.17. Calendar Arithmetic MUST NOT Use Fixed Durations

An implementation MUST NOT define:

```text
one calendar month = N seconds
```

or:

```text
one calendar year = N seconds
```

for the purpose of calendar arithmetic.

For example:

```k
date.plus(Period.months(1))
```

must be evaluated according to:

```text
Calendar.monthArithmetic(...)
```

and not by adding an approximation expressed in `Duration`.

---

# A.18. Variable-Length Calendars

The API MUST support calendars with variable-length months and years.

For example, a calendar may define:

```text
month 1 = 30 days
month 2 = 29 days
month 3 = 30 days
...
```

or may introduce an additional leap month.

The API MUST NOT assume:

```text
12 months = 1 year
```

unless that is explicitly a property of the selected calendar.

---

# A.19. Leap Months

Calendars supporting leap months MUST represent them explicitly.

A calendar date MUST therefore contain enough information to distinguish:

```text
month N
```

from:

```text
leap month N
```

when both may exist in the same calendar year.

The API MUST NOT encode a leap month merely by assigning it an arbitrary Gregorian month number.

---

# A.20. Eras

Calendars MAY contain multiple eras.

`CalendarDate` MUST therefore not universally assume that:

```text
year = absolute integer
```

is sufficient to identify a date.

Where necessary, a date contains:

```text
Era
YearOfEra
Month
Day
```

The calendar defines the relationship between these fields and its underlying chronological day.

---

# A.21. Negative and Zero Years

Whether a calendar supports:

- year zero;

- negative years;

- multiple eras;

is a property of that calendar.

The generalized calendar API MUST NOT impose Gregorian year-numbering assumptions on alternative calendars.

Where a calendar does not define a year zero, the API MUST reject or otherwise explicitly represent an attempted year zero.

---

# A.22. Calendar Epochs

A calendar MAY define a historical epoch.

This epoch is a property of the calendar.

It MUST NOT be confused with the K temporal epoch:

```text
1970-01-01T00:00:00Z
```

The K temporal epoch is used only for representing `Instant`.

A calendar's epoch is used only for civil/calendar calculations.

---

# A.23. Calendar Validity

Each calendar defines its own validity rules.

For example:

```k
calendar.isValid(year, month, day)
```

MUST use that calendar's rules.

The API MUST NOT validate an alternative-calendar date using Gregorian month lengths.

---

# A.24. Calendar Reform and Historical Calendars

A calendar implementation MAY be:

1. **proleptic** — rules are extended indefinitely;

2. **historically bounded** — valid only over a defined interval;

3. **historically discontinuous** — containing one or more reforms.

The calendar MUST expose its applicable validity domain where it is not universally defined.

For example, a historical calendar may define a transition from one rule set to another.

Such transitions are calendar semantics and are independent of time-zone transitions.

---

# A.25. Calendar Reform vs Time-Zone Transition

These two concepts MUST NOT be conflated.

A time-zone transition changes:

```text
local clock offset
```

relative to the global timeline.

A calendar reform changes:

```text
calendar interpretation of civil dates
```

These events may occur on the same historical date, but they are independent mechanisms.

---

# A.26. Historical Calendar Context

For calendars whose historical rules vary with location, an explicit calendar context MAY be required.

For example, a historical calendar reform may have occurred at different dates in different jurisdictions.

In such cases, the API MUST NOT infer the applicable reform solely from the current `Locale`.

The application MUST provide an explicit calendar configuration or historical calendar profile.

Conceptually:

```k
Calendar.of("Julian")
Calendar.of("Gregorian")
Calendar.of("Gregorian", reformProfile)
```

---

# A.27. Calendar vs Locale

The following are independent:

```text
Calendar
Locale
TimeZone
```

For example, all of the following are valid:

```text
Gregorian + French + Europe/Paris
Gregorian + Japanese + Asia/Tokyo
Japanese + English + America/New_York
Islamic + French + Europe/Paris
```

A locale MAY provide a **default calendar preference for presentation**, but that preference MUST NOT silently alter the semantic calendar of an existing temporal value.

---

# A.28. Formatting Alternative Calendars

A formatter MAY display a temporal value using a calendar different from its semantic calendar.

For example:

```k
formatter
    .locale(Locale("fr-FR"))
    .calendar(Japanese)
```

This means:

> Present the value using French localization conventions and Japanese calendar fields.

Formatting MUST NOT mutate the underlying temporal value.

---

# A.29. Parsing Alternative Calendars

A parser MUST know which calendar is being used to interpret calendar-specific fields.

For example:

```k
parser
    .calendar(Hebrew)
    .parseCalendarDate(text)
```

If the calendar is not explicitly supplied, the parser MAY use its configured default.

It MUST NOT silently infer a calendar from ambiguous textual data unless that inference rule is explicitly part of the parser configuration.

---

# A.30. Calendar-Neutral APIs

APIs which do not involve civil date fields SHOULD remain calendar-neutral.

In particular:

```text
Instant
Duration
Clock
MonotonicClock
ZoneOffset
ZoneRules
TimeZone
```

MUST NOT acquire calendar parameters merely for the purpose of supporting alternative calendars.

For example:

```k
instant + Duration.days(1)
```

remains valid and calendar-independent.

---

# A.31. Calendar-Sensitive APIs

The following operations are calendar-sensitive:

```text
date + Period
date - Period
date.year()
date.month()
date.day()
date.dayOfYear()
date.weekOfYear()
date.toString()
date.parse(...)
```

when those operations depend on calendar fields.

These operations MUST be performed in the context of the relevant calendar.

---

# A.32. Week-Based Calendars

Week numbering MUST be treated as a calendar/presentation rule rather than as an intrinsic property of `Instant`.

A calendar or locale configuration may define:

- first day of week;

- minimal number of days in first week;

- week-based year;

- week numbering system.

Therefore:

```k
date.weekOfYear()
```

MUST NOT have a universal meaning independent of calendar/locale configuration.

Where ambiguity exists, the API SHOULD require an explicit `WeekRules` configuration.

---

# A.33. `CalendarDate` Equality

Two `CalendarDate` values from different calendars MUST NOT automatically be considered equal merely because they represent the same civil day.

For example:

```text
Gregorian 2026-09-06
Julian    2026-08-24
```

are different calendar representations.

Equality of calendar dates is therefore normally:

```text
same calendar
+
same calendar fields
```

If the application wants chronological-day equality, it MUST explicitly convert both values to a calendar-neutral representation.

---

# A.34. Calendar Date Comparison

Calendar dates MAY be ordered within the same calendar.

For dates from different calendars, comparison SHOULD require explicit conversion.

For example:

```k
julianDate.toCalendar(Gregorian) < gregorianDate
```

is preferable to defining an implicit cross-calendar ordering.

This prevents accidental mixing of incompatible field systems.

---

# A.35. `CalendarDateTime` Equality

`CalendarDateTime` equality is civil equality within the same calendar.

It MUST NOT imply equality of instants.

For example:

```text
Gregorian 2026-09-06 12:00
```

and:

```text
Gregorian 2026-09-06 12:00
```

are equal as civil values.

But neither has a global chronological identity until a time zone is supplied.

---

# A.36. Recommended Type Hierarchy

The conceptual type hierarchy becomes:

```text
                 Temporal Value
                       │
          ┌────────────┴────────────┐
          │                         │
     Absolute Time             Civil Time
          │                         │
       Instant              ┌───────┴────────┐
          │                 │                │
      Duration         Gregorian         CalendarDate
                         LocalDate       CalendarDateTime
```

The Gregorian types are convenience types.

The generalized calendar types provide the extensibility mechanism.

---

# A.37. Recommended API

The generalized API SHOULD expose operations conceptually equivalent to:

```k
interface Calendar {
    id(): CalendarId

    date(year, month, day): CalendarDate

    isValid(year, month, day): bool

    daysInMonth(year, month): int
    monthsInYear(year): int

    isLeapYear(year): bool

    plus(date, period): CalendarDate
    minus(date, period): CalendarDate

    toChronologicalDay(date): ChronologicalDay
    fromChronologicalDay(day): CalendarDate
}
```

and:

```k
CalendarDate {
    calendar(): Calendar
    era(): Era
    yearOfEra(): int
    month(): int
    day(): int

    plus(period): CalendarDate
    minus(period): CalendarDate

    toCalendar(calendar): CalendarDate
}
```

For date/time:

```k
CalendarDateTime {
    calendar(): Calendar
    date(): CalendarDate
    time(): LocalTime

    plus(period): CalendarDateTime
    minus(period): CalendarDateTime

    atZone(zone, resolver): ZonedDateTime
}
```

The exact K syntax may differ, but the semantic separation is normative.

---

# A.38. `ChronologicalDay`

The implementation MAY use an internal or public calendar-neutral day representation.

If public, it SHOULD be called something such as:

```text
ChronologicalDay
```

rather than `Instant`, because it represents a civil day rather than a precise point in time.

It represents an integral day on the canonical chronological calendar.

Its purpose is to permit:

```text
Calendar A
      ↓
ChronologicalDay
      ↓
Calendar B
```

without introducing time-zone semantics.

---

# A.39. Relationship Between `ChronologicalDay` and `Instant`

A `ChronologicalDay` identifies a calendar-neutral day boundary.

It does not identify an arbitrary instant within that day.

Therefore:

```text
ChronologicalDay -> Instant
```

is not inherently defined without specifying a time-zone and a clock time.

Conversely:

```text
Instant -> ChronologicalDay
```

requires a definition of which civil day the instant belongs to.

For a global UTC day this can be defined directly; for a local civil day a `TimeZone` is required.

---

# A.40. Recommended Conversion APIs

The API SHOULD provide explicit conversions such as:

```k
instant.toCalendarDate(calendar)
instant.toCalendarDateTime(calendar, zone)

calendarDate.toCalendar(otherCalendar)

calendarDateTime.atZone(zone, resolver)

calendarDateTime.toInstant(zone, resolver)
```

The overloads SHOULD make the required context obvious.

---

# A.41. Example — Same Instant, Different Calendars

Conceptually:

```k
let instant = Instant.parse("2026-09-06T12:00:00Z")

let gregorian =
    instant.toCalendarDateTime(Gregorian, TimeZone.UTC)

let julian =
    instant.toCalendarDateTime(Julian, TimeZone.UTC)
```

The two values represent the same instant.

Their calendar fields differ.

Converting either back to an instant MUST produce the original value.

---

# A.42. Example — Calendar Conversion Without Time Zone

```k
let julian = Julian.date(2026, 8, 24)

let gregorian =
    julian.toCalendar(Gregorian)
```

This is a pure civil-calendar conversion.

No time zone is involved.

No `Instant` is created.

---

# A.43. Example — Alternative Calendar with Time Zone

```k
let hebrewDateTime =
    Hebrew.dateTime(
        year,
        month,
        day,
        LocalTime(18, 30)
    )

let zoned =
    hebrewDateTime.atZone(
        TimeZone.of("Europe/Paris"),
        Strict
    )

let instant =
    zoned.toInstant()
```

The calendar interprets the date.

The time zone resolves that civil date/time onto the global timeline.

The resolver handles DST ambiguity.

---

# A.44. Example — Calendar Arithmetic vs Duration Arithmetic

Given a calendar date/time:

```k
let local = ...
```

then:

```k
local + Period.days(1)
```

means:

> the corresponding civil time on the next calendar day.

After resolution, it may differ from:

```k
local.atZone(zone, Strict)
    .toInstant()
    + Duration.hours(24)
```

because of DST transitions.

This remains true for alternative calendars.

---

# A.45. API Layering

The resulting architecture is therefore:

```text
Layer 0 — Chronology
    Instant
    Duration
    Clock
    MonotonicClock

Layer 1 — Civil Gregorian Convenience API
    LocalDate
    LocalTime
    LocalDateTime
    Period

Layer 2 — Calendar Abstraction
    Calendar
    CalendarDate
    CalendarDateTime
    CalendarId
    Era
    ChronologicalDay

Layer 3 — Time Zones
    ZoneOffset
    ZoneId
    ZoneRules
    TimeZone
    ZonedDateTime
    ZoneLocalResolution
    ZoneTransition

Layer 4 — Presentation
    Locale
    TemporalFormatter
    TemporalParser
```

The layers SHOULD remain conceptually independent.

---

# A.46. Critical Invariants

The following invariants are normative.

### Invariant 1

A calendar MUST NOT alter the identity of an `Instant`.

### Invariant 2

A time zone MUST NOT alter the identity of an `Instant`.

### Invariant 3

A locale MUST NOT alter the identity of an `Instant`.

### Invariant 4

`Duration` arithmetic MUST NOT depend on the calendar.

### Invariant 5

Calendar arithmetic MUST NOT be implemented as fixed-duration arithmetic.

### Invariant 6

Calendar conversion MUST NOT implicitly introduce a time zone.

### Invariant 7

Time-zone conversion MUST NOT implicitly change the calendar unless explicitly requested.

### Invariant 8

Calendar identity MUST be explicit whenever an alternative calendar is involved.

### Invariant 9

Cross-calendar comparison SHOULD require explicit conversion.

### Invariant 10

Calendar rules MUST be implementation-independent and deterministic for a given calendar definition/version.

---

# A.47. Overall Model

The complete temporal architecture can therefore be summarized as:

```text
                         ┌──────────────┐
                         │   Calendar   │
                         └──────┬───────┘
                                │
                                ▼
                         CalendarDate
                                │
                                ▼
                     CalendarDateTime
                                │
                                │
                         TimeZone/Rules
                                │
                                ▼
                             Instant
                                │
                         ┌──────┴──────┐
                         │             │
                     Duration       Calendar
                         │             │
                         ▼             ▼
                      Instant     CalendarDate
```

The key semantic boundary is:

> **Calendars define how civil dates are named and manipulated; time zones define how civil date/time values map to the global timeline; neither concept defines the global timeline itself.**

This allows K to support arbitrary calendar systems while preserving the fundamental temporal model of the standard library.







# Addendum — Calendar and Chronology Terminology

## T.1. Purpose

This addendum establishes the normative terminology used by the K temporal API for calendar systems and civil dates.

The terms **chronology**, **calendar**, **civil date**, and **timeline** describe distinct concepts and MUST NOT be used interchangeably in normative API documentation.

---

# T.2. Timeline

The **timeline** is the ordered set of chronological instants represented by `Instant`.

It answers the question:

> When did this point in chronological time occur?

The timeline is independent of:

- calendar system;

- time zone;

- locale;

- language;

- date formatting.

`Instant` is the representation of a point on this timeline.

---

# T.3. Chronology

A **chronology** is a mathematical and civil-date system defining how dates are named, structured, validated, and manipulated.

A chronology defines, as applicable:

- eras;

- years;

- year numbering;

- months;

- month numbering;

- days;

- leap-year rules;

- leap-month rules;

- month lengths;

- calendar epochs;

- valid date ranges;

- date arithmetic;

- conversion between its civil dates and a continuous day-based representation.

Examples include:

```text
Gregorian
Julian
Hebrew
Islamic
Japanese
Buddhist
```

The chronology is therefore the **rule system governing civil dates**.

A chronology does not define:

- the global timeline itself;

- UTC;

- time zones;

- UTC offsets;

- daylight-saving transitions;

- locale;

- textual formatting.

---

# T.4. Calendar

The term **calendar** refers to the civil representation of dates according to a chronology.

In informal language, "Gregorian calendar" and "Hebrew calendar" are acceptable expressions.

In the normative K API, however, the abstraction representing the rules of such a system SHOULD be called `Chronology`.

This distinction avoids ambiguity between:

```text
the rules of a calendar system
```

and:

```text
one date expressed using those rules.
```

Thus:

```text
Chronology
    defines the rules

CalendarDate
    is one value interpreted by those rules
```

---

# T.5. `Chronology`

The generalized API type formerly called `Calendar` SHOULD therefore be named:

```k
Chronology
```

Its conceptual interface is:

```k
interface Chronology {
    id(): ChronologyId

    date(year, month, day): CalendarDate

    isValid(year, month, day): bool

    daysInMonth(year, month): int
    monthsInYear(year): int

    isLeapYear(year): bool

    plus(date, period): CalendarDate
    minus(date, period): CalendarDate

    toEpochDay(date): EpochDay
    fromEpochDay(day): CalendarDate
}
```

The exact K syntax is implementation-dependent; the semantic role of the type is normative.

---

# T.6. Chronology Identity

Each chronology MUST have a stable identity.

Examples:

```text
Gregorian
Julian
Hebrew
Islamic
Japanese
Buddhist
```

A chronology identity identifies the rules used to interpret civil dates.

It MUST NOT be inferred from:

- locale;

- language;

- time zone;

- operating-system settings.

---

# T.7. Calendar Date

A **calendar date**, represented by `CalendarDate`, is a civil date interpreted according to a specific `Chronology`.

Conceptually:

```text
CalendarDate =
    Chronology
    +
    chronology-specific date fields
```

The date fields may include:

```text
Era
YearOfEra
Month
Day
```

or other fields required by the chronology.

The API MUST NOT assume that every chronology can be represented by:

```text
year + month + day
```

alone.

---

# T.8. Gregorian `LocalDate`

`LocalDate` is the K convenience type for a civil date in the default proleptic Gregorian chronology.

Semantically:

```text
LocalDate ≈ Gregorian CalendarDate
```

`LocalDate` is retained because Gregorian dates are overwhelmingly common and deserve a compact, convenient API.

It MUST NOT be reinterpreted according to another chronology.

---

# T.9. `CalendarDateTime`

`CalendarDateTime` represents a local civil date and time according to a particular chronology.

Conceptually:

```text
CalendarDateTime =
    CalendarDate
    +
    LocalTime
```

It does not contain:

- an `Instant`;

- a `ZoneOffset`;

- a `TimeZone`.

Therefore it does not identify a unique point on the global timeline.

---

# T.10. Chronology Conversion

Two dates expressed using different chronologies MAY be converted through a chronology-neutral day representation.

Conceptually:

```text
Chronology A
     │
     ▼
CalendarDate A
     │
     ▼
EpochDay
     │
     ▼
CalendarDate B
     ▲
     │
Chronology B
```

For example:

```k
let julian = Julian.date(2026, 8, 24)

let gregorian =
    julian.toChronology(Gregorian)
```

The conversion preserves the represented civil day.

It does not require a time zone.

It does not create an `Instant`.

---

# T.11. `EpochDay`

The generalized API SHOULD use the term `EpochDay` for a chronology-neutral integral count of civil days from the K-defined epoch.

`EpochDay` is not an `Instant`.

It represents a whole chronological day rather than a precise point within a day.

Its primary purpose is to provide a common mathematical domain between chronologies:

```text
CalendarDate
      │
      ▼
   EpochDay
      │
      ▼
CalendarDate
```

The exact numerical epoch and range of `EpochDay` are defined by the core temporal specification.

---

# T.12. `EpochDay` and Time Zones

`EpochDay` does not inherently contain a time zone.

Consequently:

```text
CalendarDate -> EpochDay
```

is a chronology operation.

It is not a time-zone operation.

Conversely, converting an `Instant` to a local `CalendarDate` requires a time zone because the time zone determines which local civil day contains the instant.

The conceptual operation is:

```text
Instant
   │
   │ TimeZone
   ▼
Local civil date/time
   │
   │ Chronology
   ▼
CalendarDate
```

---

# T.13. Chronology vs Time Zone

A chronology answers:

> How is this civil date named and structured?

A time zone answers:

> Which local civil date/time corresponds to this instant at this location and according to these zone rules?

These are independent dimensions.

For example:

```text
Chronology = Japanese
TimeZone   = Europe/Paris
```

is entirely valid.

Likewise:

```text
Chronology = Islamic
TimeZone   = Asia/Tokyo
```

is valid.

---

# T.14. Chronology vs Locale

A chronology and a locale serve different purposes.

A chronology defines **semantic date interpretation**.

A locale defines **cultural presentation**.

For example:

```text
Chronology = Japanese
Locale     = fr-FR
```

means:

> Interpret the date using the Japanese chronology and present it according to French localization conventions.

A locale MUST NOT silently change the chronology of an existing `CalendarDate`.

---

# T.15. Chronology vs Formatting

Formatting is a presentation operation.

A formatter MAY choose a chronology for displaying a temporal value, provided that this choice is explicit or determined by the formatter's documented configuration.

For example:

```k
formatter
    .locale(Locale("fr-FR"))
    .chronology(Japanese)
```

means:

> Display the temporal value using French localization conventions and Japanese calendar fields.

This operation does not alter the underlying `Instant`.

---

# T.16. Chronology vs `Instant`

An `Instant` does not belong to a chronology.

The same `Instant` may be represented simultaneously in different chronologies.

For example:

```text
                    same Instant
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
      Gregorian        Julian        Japanese
       date/time       date/time       date/time
```

The chronology determines only the civil representation.

It does not determine the chronological identity.

---

# T.17. Chronology vs `Duration`

`Duration` is chronology-independent.

The following operation:

```k
instant + Duration.hours(24)
```

has exactly the same meaning regardless of:

- chronology;

- locale;

- time zone.

By contrast:

```k
calendarDate + Period.days(1)
```

is chronology-sensitive.

The chronology determines what constitutes the next civil day.

---

# T.18. Chronology-Sensitive Arithmetic

Arithmetic involving calendar fields MUST be defined by the chronology.

Examples include:

```text
+1 month
+1 year
+1 calendar day
next month
last day of month
day-of-year
week-based year
```

The implementation MUST NOT reduce such operations to fixed `Duration` values.

For example:

```text
one month ≠ a fixed number of seconds
one year ≠ a fixed number of seconds
```

unless a particular chronology explicitly defines such a relationship for a specific operation.

---

# T.19. Chronology-Specific Structure

A chronology MAY define structures that do not exist in the Gregorian calendar.

Examples include:

- multiple eras;

- leap months;

- non-Gregorian month lengths;

- non-Gregorian year boundaries;

- non-Gregorian year numbering;

- calendar-specific cycles.

The generalized API MUST accommodate these features without forcing them into Gregorian concepts.

---

# T.20. Historical Chronologies

A chronology MAY be:

1. **proleptic** — its rules are extended indefinitely;

2. **bounded** — its rules apply only to a specified range;

3. **reformed** — its rules change at one or more historical boundaries.

A historical calendar reform is a chronology rule.

It is distinct from a time-zone transition.

For example:

```text
Chronology reform
    changes interpretation of civil dates

Time-zone transition
    changes offset between local civil time and the timeline
```

The two MUST NOT be conflated.

---

# T.21. Terminology Summary

The normative terminology is therefore:

| Term               | Meaning                                                                 |
| ------------------ | ----------------------------------------------------------------------- |
| `Instant`          | A precise point on the global timeline                                  |
| `Duration`         | An elapsed amount of chronological time                                 |
| `Chronology`       | Rules defining a system of civil dates                                  |
| `CalendarDate`     | A date expressed according to a chronology                              |
| `CalendarDateTime` | A date and local time expressed according to a chronology               |
| `EpochDay`         | A chronology-neutral integral civil-day value                           |
| `LocalDate`        | Gregorian convenience form of `CalendarDate`                            |
| `LocalDateTime`    | Gregorian convenience form of `CalendarDateTime`                        |
| `TimeZone`         | A zone identity plus a versioned set of offset rules                    |
| `ZonedDateTime`    | An instant represented as civil date/time in a time zone and chronology |
| `Locale`           | Cultural and linguistic presentation conventions                        |

---

# T.22. Terminological Rule

In normative K API specifications:

- **Chronology** SHOULD be used for the rule system.

- **CalendarDate** SHOULD be used for a date value.

- **Calendar** MAY be used as an informal human-facing term for a chronology.

- **Calendar system** SHOULD be avoided as an API type name when `Chronology` is sufficient.

- **Chronological** SHOULD describe properties of the global timeline, not calendar rules.

- **Civil** SHOULD describe human/calendar date and time representations.

In particular, the specification SHOULD avoid phrases such as:

```text
"the Calendar represents the timeline"
"the Calendar converts an Instant"
"Chronology is a date"
"Calendar arithmetic uses Duration"
```

and instead use:

```text
"the Chronology defines the civil-date rules"
"a CalendarDate represents a date according to a Chronology"
"a TimeZone maps local civil time to the timeline"
"Duration represents elapsed chronological time"
"Period represents calendar-relative amounts"
```

---

# T.23. Final Conceptual Model

The terminology can be summarized by the following model:

```text
                         GLOBAL TIMELINE
                               │
                            Instant
                               │
                               │
                         TimeZone/Rules
                               │
                               ▼
                      Local Civil Date/Time
                               │
                               │
                          Chronology
                               │
                               ▼
                         CalendarDate
```

The three principal semantic dimensions are therefore:

```text
Timeline      → When?
Time Zone     → Where / which local clock?
Chronology    → How is the civil date represented?
```

Locale is a fourth, presentation-oriented dimension:

```text
Locale        → How is it presented to a human?
```

These dimensions MUST remain independent in the K temporal API.







# Addendum — Time Sources and Time Scales

## 1. Purpose

The K Time API distinguishes three fundamentally different concepts:

1. **Chronological time scales** — mathematical or physical conventions used to express positions on the global timeline.

2. **Time sources and clocks** — mechanisms that provide observations or measurements of time.

3. **Civil time** — human-oriented representations such as local date/time, time zones and calendars.

These concepts MUST NOT be conflated.

In particular:

- `SystemClock` is a source of current civil/absolute time.

- `MonotonicClock` is a source of elapsed-time measurements.

- TAI and GPS are chronological time scales.

- UTC is both a globally defined chronological scale and the basis of civil time.

- A monotonic clock is not a time scale.

- A time scale does not imply the existence of a particular physical clock capable of realizing it.

The API MUST preserve these distinctions.

---

# 2. Global Chronological Timeline

K defines an abstract global timeline of physical instants.

```text
Timeline
    |
    +-- Instant
```

`Instant` identifies a unique point on this timeline.

An `Instant` is independent of:

- timezone;

- locale;

- calendar;

- formatting;

- operating system;

- clock implementation;

- time synchronization protocol;

- particular time scale used to display or exchange it.

The same `Instant` may therefore be expressed using different time scales.

Conceptually:

```text
                    same physical instant
                            |
          +-----------------+------------------+
          |                 |                  |
         TAI               UTC                GPS
```

The representations differ, but they identify the same point on the underlying timeline.

---

# 3. Time Scales

## 3.1 Definition

A `TimeScale` defines a convention for assigning temporal coordinates to instants.

A time scale specifies, among other things:

- its relationship to the global chronological timeline;

- its epoch, if applicable;

- its unit;

- its continuity properties;

- its relationship to leap seconds;

- conversion rules to and from other supported scales.

A time scale is not itself a clock.

Conceptually:

```k
interface TimeScale {
    id(): TimeScaleId

    instantFromValue(value: TimeValue): Instant
    valueFromInstant(instant: Instant): TimeValue

    epoch(): Instant
}
```

The exact representation of `TimeValue` is implementation-defined, but its semantics MUST be defined by the selected `TimeScale`.

---

# 4. TAI

## 4.1 Definition

TAI (International Atomic Time) is a continuous atomic time scale.

For the purposes of K:

- TAI MUST be treated as continuous.

- TAI MUST NOT contain leap seconds.

- Equal increments of TAI represent equal elapsed durations.

- A TAI coordinate identifies an `Instant`.

- TAI is independent of timezone and civil calendar.

TAI therefore provides a particularly useful conceptual reference for representing elapsed chronological time.

The API MAY expose:

```k
TimeScale.tai
```

or an equivalent implementation-specific singleton.

---

## 4.2 TAI and Duration

For two instants:

```k
duration = instant2 - instant1
```

the duration is independent of whether either instant is displayed as TAI, UTC or GPS.

If the interval crosses a UTC leap second, that leap second contributes to the elapsed duration.

For example, an interval containing:

```text
23:59:59 UTC
23:59:60 UTC
00:00:00 UTC
```

contains two elapsed SI seconds.

This MUST NOT depend on the underlying operating system representation.

---

# 5. UTC

## 5.1 Definition

UTC (Coordinated Universal Time) is a globally defined atomic time scale adjusted with leap seconds to remain close to UT1.

For K:

- UTC is an absolute time scale.

- UTC permits leap-second representations.

- A UTC leap second is a distinct position on the global timeline.

- UTC is the reference scale used by civil time and time-zone rules.

- UTC is not equivalent to POSIX time.

Thus:

```text
UTC 23:59:59
UTC 23:59:60
UTC 00:00:00
```

represent three consecutive UTC labels for three distinct chronological positions.

---

# 6. GPS Time

## 6.1 Definition

GPS time is an atomic time scale used by the Global Positioning System.

For K:

- GPS time is continuous.

- GPS time does not contain leap seconds.

- GPS time has a defined epoch.

- GPS time is related to TAI by a constant offset.

- GPS time is related to UTC through the currently applicable UTC–TAI relationship.

The API MAY expose:

```k
TimeScale.gps
```

GPS time MUST NOT be interpreted as "UTC time in the GPS timezone".

It is a distinct atomic time scale.

---

# 7. Relationship Between TAI, GPS and UTC

The relationships between these scales MUST be represented explicitly rather than encoded as arbitrary fixed constants in application code.

Conceptually:

```text
                         TAI
                          |
                    constant offset
                          |
                         GPS

                          |
                    leap-second
                     relationship
                          |
                          v
                         UTC
                          |
                   timezone rules
                          |
                          v
                   LocalDateTime
```

The TAI–GPS relationship is fixed by their respective definitions.

The TAI–UTC relationship changes when leap seconds are introduced.

Consequently:

```text
TAI <-> GPS
```

can be described by a fixed relationship, whereas:

```text
TAI <-> UTC
```

requires knowledge of the applicable leap-second history.

The implementation MUST NOT assume that the current TAI–UTC offset applies to historical instants.

---

# 8. Time Scale Conversion

K SHOULD provide explicit conversion facilities.

For example:

```k
instant.toTimeScale(TimeScale.tai)
instant.toTimeScale(TimeScale.utc)
instant.toTimeScale(TimeScale.gps)
```

or equivalently:

```k
TimeScale.tai.valueOf(instant)
TimeScale.utc.valueOf(instant)
TimeScale.gps.valueOf(instant)
```

The important semantic property is that conversion changes the **representation of the same instant**, not the instant itself.

For example:

```text
instant
   |
   +-- TAI representation
   |
   +-- UTC representation
   |
   +-- GPS representation
```

must never produce three different instants merely because three different scales were requested.

---

# 9. Leap Seconds

Leap seconds are a property of the relationship between UTC and continuous atomic time scales.

They MUST NOT be modeled as irregularities in `Duration`.

They MUST NOT be silently discarded by `Instant`.

They MUST NOT be implemented as timezone transitions.

The following concepts are therefore independent:

```text
Leap second
    -> UTC / atomic-time relationship

DST transition
    -> TimeZone / ZoneRules

Calendar reform
    -> Chronology
```

A leap second is a global chronological phenomenon.

A timezone transition is a local civil-time phenomenon.

A calendar reform is a civil-date-system phenomenon.

---

# 10. POSIX Time

POSIX timestamps are an interoperability representation and MUST NOT define the semantics of `Instant`.

POSIX time traditionally represents:

```text
seconds since 1970-01-01T00:00:00Z
```

without representing UTC leap seconds as distinct timestamp values.

Consequently, POSIX time cannot in general provide a bijective mapping to K `Instant` values if K explicitly represents leap seconds.

The API MUST therefore make this limitation explicit.

For example:

```k
PosixTimestamp
```

may be provided as an interoperability type.

Conversion:

```k
Instant -> PosixTimestamp
PosixTimestamp -> Instant
```

MAY be lossy or ambiguous around leap seconds, depending on the defined interoperability policy.

Such behavior MUST be explicitly specified.

`PosixTimestamp` MUST NOT replace `Instant` as the fundamental absolute-time type.

---

# 11. Time Sources

A `Clock` is an abstraction over a source of temporal observations.

```k
interface Clock {
    now(): Instant
}
```

However, not every clock has the same semantics.

K therefore defines distinct clock categories.

---

# 12. SystemClock

`SystemClock` provides the system's best available estimate of current absolute time.

Conceptually:

```k
interface SystemClock : Clock {
    now(): Instant
}
```

Its value is associated with an absolute time scale, normally UTC.

The implementation MAY obtain this value from:

- the operating system wall clock;

- a hardware real-time clock;

- NTP synchronization;

- PTP synchronization;

- another system time service;

- a combination of these mechanisms.

These implementation choices MUST NOT change the semantic contract of the API.

---

## 12.1 System Clock Is Not Monotonic

A `SystemClock` MUST NOT be assumed to be monotonic.

Its value MAY:

- move forward;

- move backward;

- remain unchanged;

- jump by a significant amount.

For example, synchronization with an external time source may cause:

```text
12:00:00.100
12:00:00.101
11:59:59.900
```

This is valid behavior for a system wall clock.

Applications requiring elapsed-time measurement MUST NOT use `SystemClock`.

---

# 13. MonotonicClock

A `MonotonicClock` measures elapsed time using a monotonic reference.

```k
interface MonotonicClock {
    now(): MonotonicInstant
}
```

or, equivalently, it may expose an elapsed-duration API.

The essential property is:

```text
t2 >= t1
```

for observations made by the same monotonic clock.

A monotonic clock MUST NOT move backwards.

---

## 13.1 Monotonic Time Is Not Absolute Time

A monotonic clock does not identify a globally meaningful instant.

Its origin may be:

- system boot;

- process start;

- an implementation-defined arbitrary point;

- a hardware counter origin.

Therefore:

```k
MonotonicInstant
```

MUST NOT be implicitly convertible to:

```k
Instant
```

without an explicitly defined correlation mechanism.

Likewise, a monotonic timestamp MUST NOT be serialized as though it were UTC, TAI or GPS time.

---

# 14. Monotonic Time and Duration

The primary purpose of a monotonic clock is measuring elapsed time.

For example:

```k
let start = monotonicClock.now()

operation()

let elapsed = monotonicClock.now() - start
```

This measurement MUST NOT be affected by changes to:

- system wall time;

- timezone;

- DST;

- calendar;

- UTC leap-second announcements;

- NTP clock corrections.

The implementation SHOULD use the strongest monotonic facility available on the host platform.

---

# 15. SystemClock and MonotonicClock Are Orthogonal

A system clock and a monotonic clock answer different questions.

```text
SystemClock:
    "What absolute time is it?"

MonotonicClock:
    "How much time has elapsed?"
```

They MUST NOT be treated as interchangeable.

A typical implementation may therefore expose:

```k
SystemClock.system()
MonotonicClock.system()
```

as separate objects.

---

# 16. Correlation Between System and Monotonic Clocks

A runtime MAY internally correlate a monotonic clock with an absolute clock.

For example:

```text
                  SystemClock
                      |
                      | sampled at T0
                      |
                      v
                correlation point
                      ^
                      |
                      | monotonic value M0
                      |
                MonotonicClock
```

However, such a correlation is inherently subject to uncertainty and clock adjustments.

K MUST NOT imply that:

```k
Instant == f(MonotonicInstant)
```

is permanently valid.

A system clock may be adjusted while the monotonic clock continues uninterrupted.

If K exposes correlation explicitly, it SHOULD represent it as a sampled observation with an uncertainty bound rather than as an eternal mathematical identity.

---

# 17. Clock Accuracy and Synchronization

Clock semantics and clock accuracy are separate concepts.

A `SystemClock` may have perfectly valid semantics while having poor synchronization accuracy.

For example:

```text
SystemClock:
    absolute = UTC
    resolution = 1 ns
    synchronization error = ±50 ms
```

The API SHOULD therefore distinguish:

- resolution;

- precision;

- accuracy;

- synchronization status;

- estimated uncertainty;

- source information.

These properties MUST NOT redefine the meaning of `Instant`.

If exposed, they SHOULD be represented through a dedicated observation type rather than by changing the semantics of `Instant`.

For example:

```k
interface TimeSource {
    observation(): TimeObservation
}

struct TimeObservation {
    instant: Instant
    uncertainty: Duration
}
```

The exact API is implementation-dependent, but the semantic distinction is normative.

---

# 18. Hardware and External Time Sources

K MAY expose additional time sources, including:

- hardware RTC;

- NTP;

- PTP;

- GNSS/GPS receivers;

- atomic-clock-backed sources;

- platform-specific precision clocks.

These MUST be modeled as sources of observations, not as new kinds of `Instant`.

For example:

```text
GNSS receiver
      |
      v
GPS / UTC observation
      |
      v
Instant
```

The source identifies how an observation was obtained.

The time scale identifies how the observed time is expressed.

These are separate dimensions.

---

# 19. Source vs Scale

The distinction is normative:

```text
             Time Source              Time Scale
                 |                        |
          "where/how measured"      "how represented"
                 |                        |
          SystemClock              UTC / TAI / GPS
          PTP source               ...
          GNSS receiver
          hardware RTC
```

Examples:

### System clock synchronized by NTP

```text
Source: SystemClock
Scale: UTC
```

### GNSS receiver providing GPS time

```text
Source: GNSS receiver
Scale: GPS
```

### Atomic reference

```text
Source: atomic clock
Scale: TAI
```

### Monotonic hardware counter

```text
Source: hardware monotonic counter
Scale: monotonic elapsed-time domain
```

The last case is not an absolute time scale.

---

# 20. Clock Resolution

A clock MAY expose its nominal resolution:

```k
clock.resolution(): Duration
```

Resolution describes the smallest representable increment of the clock.

It MUST NOT be interpreted as accuracy.

For example:

```text
resolution = 1 ns
accuracy   = ±10 ms
```

is entirely valid.

Likewise, a clock with millisecond resolution may be synchronized more accurately than another clock with nanosecond resolution.

---

# 21. Clock Identity and Stability

Two independently obtained clocks MUST NOT be assumed to be directly comparable unless the API explicitly defines their relationship.

For example:

```k
clockA.now()
clockB.now()
```

does not imply that subtraction between their values is meaningful.

Two `MonotonicClock` instances SHOULD be explicitly documented as belonging to the same monotonic domain if their values can be compared.

Likewise, two system clocks may have different synchronization characteristics even though both produce `Instant`.

---

# 22. Suspend and Sleep

The monotonic-clock contract MUST explicitly define behavior across system suspend.

K SHOULD distinguish between at least:

```text
Monotonic excluding suspended time
Monotonic including suspended time
```

if the host platform provides both semantics.

For example, an implementation may provide:

```k
MonotonicClock.activeTime()
MonotonicClock.elapsedTime()
```

where:

- `activeTime` advances only while the relevant execution environment is active;

- `elapsedTime` advances according to elapsed physical time, including suspend.

The standard MUST define these as distinct semantics rather than relying on platform-specific terminology such as `CLOCK_MONOTONIC` or `CLOCK_BOOTTIME`.

---

# 23. Process and Thread Clocks

Process CPU time and thread CPU time are not instances of wall-clock or monotonic elapsed time.

If exposed, they SHOULD use distinct abstractions:

```k
ProcessCpuClock
ThreadCpuClock
```

Their values represent consumed CPU execution time, not elapsed physical time.

They MUST NOT be implicitly convertible to `Instant`.

---

# 24. Recommended Conceptual Type Hierarchy

The resulting conceptual model is:

```text
                         Time
                          |
          +---------------+----------------+
          |                                |
     Absolute time                    Measured time
          |                                |
       Instant                    +---------+---------+
          |                       |                   |
      TimeScale              MonotonicClock     CPU clocks
          |
    +-----+------+
    |     |      |
   TAI   UTC    GPS
```

And separately:

```text
                       TimeSource
                           |
            +--------------+---------------+
            |              |               |
       SystemClock    GNSS source      PTP/NTP source
            |
          UTC
```

The source and scale dimensions intersect, but neither subsumes the other.

---

# 25. Interaction with Time Zones

The conversion from an absolute instant to civil local time proceeds conceptually as:

```text
Instant
   |
   | select time zone
   v
TimeZone / ZoneRules
   |
   v
Local civil date/time
   |
   | select Chronology
   v
CalendarDateTime
```

Time scale conversion is orthogonal:

```text
                    Instant
                       |
        +--------------+--------------+
        |              |              |
       TAI            UTC            GPS
        |
        +------------------------------+
                       |
                  TimeZone
                       |
                local date/time
```

A timezone does not convert TAI to UTC, GPS to UTC, etc.

A timezone converts an absolute instant into local civil time using UTC-based timezone rules.

---

# 26. Interaction with Chronology

Chronology is also independent from time scale.

For example, the same instant may be expressed as:

```text
TAI coordinate
UTC coordinate
GPS coordinate
Gregorian local date/time
Julian calendar date/time
Japanese calendar date/time
```

These are different representations of the same underlying temporal position.

The chronology determines how a civil date is structured.

The time scale determines how an absolute instant is coordinated.

Neither changes the instant.

---

# 27. Serialization

Serialized absolute timestamps SHOULD identify their time scale explicitly whenever ambiguity is possible.

Examples of semantically distinct representations include:

```text
UTC:
    2026-09-06T08:30:00Z

TAI:
    explicit TAI representation

GPS:
    GPS week + time-of-week
```

The serialization format MUST NOT cause an implementation-specific interpretation of an otherwise ambiguous numeric value.

In particular, a bare integer such as:

```text
1788683400
```

MUST NOT be assumed by the semantic API to mean POSIX, TAI or GPS time without an explicitly specified type or context.

---

# 28. Summary of Normative Distinctions

| Concept          | Meaning                        | Monotonic | Absolute | Leap seconds                 |
| ---------------- | ------------------------------ | --------- | -------- | ---------------------------- |
| `Instant`        | Point on global timeline       | N/A       | Yes      | Representable                |
| `Duration`       | Elapsed chronological time     | N/A       | No       | Counted                      |
| TAI              | Atomic time scale              | Yes       | Yes      | No                           |
| GPS              | Atomic time scale              | Yes       | Yes      | No                           |
| UTC              | Civil/atomic global time scale | Yes       | Yes      | Yes                          |
| `SystemClock`    | Current absolute-time source   | No        | Yes      | Depends on represented scale |
| `MonotonicClock` | Elapsed-time source            | Yes       | No       | N/A                          |
| CPU clock        | CPU consumption measurement    | Yes       | No       | N/A                          |
| `TimeZone`       | Local offset rules             | N/A       | N/A      | No                           |
| `Chronology`     | Civil-date rules               | N/A       | N/A      | No                           |

The table describes semantic properties, not implementation mechanisms.

---

# 29. Core Design Principle

The K Time API SHOULD therefore be understood as three orthogonal layers:

```text
                ┌───────────────────────────┐
                │       Time Sources        │
                │                           │
                │ SystemClock               │
                │ MonotonicClock            │
                │ NTP / PTP / GNSS / RTC    │
                └─────────────┬─────────────┘
                              │ observations
                              v
                ┌───────────────────────────┐
                │    Absolute Timeline      │
                │                           │
                │         Instant            │
                └─────────────┬─────────────┘
                              │
                    represented using
                              │
                ┌─────────────v─────────────┐
                │       Time Scales         │
                │                           │
                │ UTC / TAI / GPS           │
                └─────────────┬─────────────┘
                              │
                              v
                ┌───────────────────────────┐
                │      Civil Time           │
                │                           │
                │ TimeZone + Chronology     │
                │ LocalDateTime             │
                └───────────────────────────┘
```

This separation is fundamental to the K Time API.

In particular, **TAI/GPS/UTC describe temporal scales, while System/Monotonic describe sources or measurement domains**. They must not be represented as alternative implementations of the same abstraction.

The API may therefore evolve to expose richer physical time-source metadata without changing the semantic definition of `Instant`, `Duration`, `TimeZone`, or `Chronology`.
