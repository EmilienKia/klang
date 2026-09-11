/*
 * K Language standard library — Time scales, time sources, and observations
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

namespace time {

/**
 * TimeScaleId — stable identifier for a time scale convention.
 *
 * Standard IDs include "UTC", "TAI", "GPS", and "POSIX".
 */
public final class TimeScaleId {
private:
    _value : String;

public:
    TimeScaleId() {
        _value = String("UTC");
    }

    TimeScaleId(value: const String&) {
        _value = value;
    }

    static of(value: const String&) : TimeScaleId throws(InvalidTemporalValueException) {
        if (value.empty()) {
            throw InvalidTemporalValueException(501);
        }
        i : unsigned int = 0u;
        sz : unsigned int = value.size();
        while (i < sz) {
            c : char = value.at(i);
            isAlpha : bool = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            isDigit : bool = (c >= '0' && c <= '9');
            if (!isAlpha && !isDigit && c != '-' && c != '_') {
                throw InvalidTemporalValueException(501);
            }
            ++i;
        }
        return TimeScaleId(value);
    }

    static utc() : TimeScaleId {
        return TimeScaleId(String("UTC"));
    }

    static tai() : TimeScaleId {
        return TimeScaleId(String("TAI"));
    }

    static gps() : TimeScaleId {
        return TimeScaleId(String("GPS"));
    }

    static posix() : TimeScaleId {
        return TimeScaleId(String("POSIX"));
    }

    const value() : String {
        return _value;
    }

    const operator==(other: const TimeScaleId&) : bool {
        return _value == other._value;
    }

    const operator!=(other: const TimeScaleId&) : bool {
        return _value != other._value;
    }
}

/**
 * TimeScaleValue — explicit scale-coordinate value representing whole seconds
 * and nanosecond adjustment within a specific time scale.
 *
 * Invariant: 0 <= nanoAdjustment < 1_000_000_000.
 */
public final struct TimeScaleValue {
private:
    _wholeSeconds   : long;
    _nanoAdjustment : int;

public:
    TimeScaleValue() {
        _wholeSeconds = 0L;
        _nanoAdjustment = 0;
    }

    TimeScaleValue(wholeSeconds: long, nanos: int) throws(TemporalArithmeticException) {
        norm : NormalizedSecondNanos = NormalizedSecondNanos(wholeSeconds, nanos);
        _wholeSeconds = norm.seconds;
        _nanoAdjustment = norm.nanos;
    }

    static of(wholeSeconds: long, nanos: int) : TimeScaleValue throws(TemporalArithmeticException) {
        return TimeScaleValue(wholeSeconds, nanos);
    }

    static ofSeconds(wholeSeconds: long) : TimeScaleValue {
        try {
            return TimeScaleValue(wholeSeconds, 0);
        } catch (e: TemporalArithmeticException&) {
            return TimeScaleValue();
        }
    }

    const wholeSeconds() : long {
        return _wholeSeconds;
    }

    const nanoAdjustment() : int {
        return _nanoAdjustment;
    }

    const operator==(other: const TimeScaleValue&) : bool {
        return _wholeSeconds == other._wholeSeconds && _nanoAdjustment == other._nanoAdjustment;
    }

    const operator!=(other: const TimeScaleValue&) : bool {
        return _wholeSeconds != other._wholeSeconds || _nanoAdjustment != other._nanoAdjustment;
    }
}

/**
 * TimeScale — contract defining conventions for assigning temporal coordinates
 * to Instants on the global chronological timeline.
 */
public interface TimeScale {
    id() : TimeScaleId;
    epoch() : time::Instant;
    valueFromInstant(instant: const time::Instant&) : TimeScaleValue throws(TimeScaleDataUnavailableException);
    instantFromValue(value: const TimeScaleValue&) : time::Instant throws(TimeScaleDataUnavailableException, InvalidTemporalValueException);
}

private _utcScaleInstance : UtcTimeScale;
private _taiScaleInstance : TaiTimeScale;
private _gpsScaleInstance : GpsTimeScale;

/**
 * UtcTimeScale — Coordinated Universal Time scale adjusted with leap seconds.
 *
 * Reference epoch: 1970-01-01T00:00:00Z.
 */
public final class UtcTimeScale : public TimeScale {
public:
    UtcTimeScale() {}

    static instance() : UtcTimeScale& {
        return _utcScaleInstance;
    }

    override id() : TimeScaleId {
        return TimeScaleId::utc();
    }

    override epoch() : time::Instant {
        return time::Instant::epoch();
    }

    override valueFromInstant(instant: const time::Instant&) : TimeScaleValue throws(TimeScaleDataUnavailableException) {
        if (LeapSecondTable::count() == 0) {
            throw TimeScaleDataUnavailableException(513);
        }
        posixSec : long = LeapSecondTable::continuousToPosixFolded(instant.epochSeconds());
        try {
            return TimeScaleValue(posixSec, instant.nanoAdjustment());
        } catch (e: TemporalArithmeticException&) {
            throw TimeScaleDataUnavailableException(513);
        }
    }

    override instantFromValue(value: const TimeScaleValue&) : time::Instant
        throws(TimeScaleDataUnavailableException, InvalidTemporalValueException) {
        if (LeapSecondTable::count() == 0) {
            throw TimeScaleDataUnavailableException(513);
        }
        if (value.nanoAdjustment() < 0 || value.nanoAdjustment() >= 1000000000) {
            throw InvalidTemporalValueException(501);
        }
        contSec : long = LeapSecondTable::posixToContinuous(value.wholeSeconds());
        try {
            return time::Instant::ofEpochSecond(contSec, value.nanoAdjustment());
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }
}

/**
 * TaiTimeScale — International Atomic Time scale (continuous atomic scale).
 *
 * TAI is ahead of UTC by historical leap seconds (10s in 1972, 37s in 2017).
 * TAI = instant + 10s (where continuous Instant is anchored to 1970-01-01 UTC).
 */
public final class TaiTimeScale : public TimeScale {
public:
    TaiTimeScale() {}

    static instance() : TaiTimeScale& {
        return _taiScaleInstance;
    }

    override id() : TimeScaleId {
        return TimeScaleId::tai();
    }

    override epoch() : time::Instant {
        return time::Instant::epoch();
    }

    override valueFromInstant(instant: const time::Instant&) : TimeScaleValue throws(TimeScaleDataUnavailableException) {
        if (LeapSecondTable::count() == 0) {
            throw TimeScaleDataUnavailableException(513);
        }
        try {
            taiSec : long = addChecked(instant.epochSeconds(), 10L);
            return TimeScaleValue(taiSec, instant.nanoAdjustment());
        } catch (e: TemporalArithmeticException&) {
            throw TimeScaleDataUnavailableException(513);
        }
    }

    override instantFromValue(value: const TimeScaleValue&) : time::Instant
        throws(TimeScaleDataUnavailableException, InvalidTemporalValueException) {
        if (LeapSecondTable::count() == 0) {
            throw TimeScaleDataUnavailableException(513);
        }
        if (value.nanoAdjustment() < 0 || value.nanoAdjustment() >= 1000000000) {
            throw InvalidTemporalValueException(501);
        }
        try {
            instSec : long = subtractChecked(value.wholeSeconds(), 10L);
            return time::Instant::ofEpochSecond(instSec, value.nanoAdjustment());
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }
}

/**
 * GpsTimeScale — Global Positioning System time scale.
 *
 * Epoch: 1980-01-06T00:00:00 UTC.
 * Fixed relation to TAI: TAI = GPS + 19 seconds (GPS = TAI - 19 seconds).
 */
public final class GpsTimeScale : public TimeScale {
public:
    GpsTimeScale() {}

    static instance() : GpsTimeScale& {
        return _gpsScaleInstance;
    }

    override id() : TimeScaleId {
        return TimeScaleId::gps();
    }

    override epoch() : time::Instant {
        return time::Instant(315964809L, 0);
    }

    override valueFromInstant(instant: const time::Instant&) : TimeScaleValue throws(TimeScaleDataUnavailableException) {
        taiVal : TimeScaleValue = TaiTimeScale::instance().valueFromInstant(instant);
        try {
            gpsSec : long = subtractChecked(taiVal.wholeSeconds(), 19L);
            return TimeScaleValue(gpsSec, taiVal.nanoAdjustment());
        } catch (e: TemporalArithmeticException&) {
            throw TimeScaleDataUnavailableException(513);
        }
    }

    override instantFromValue(value: const TimeScaleValue&) : time::Instant
        throws(TimeScaleDataUnavailableException, InvalidTemporalValueException) {
        if (value.nanoAdjustment() < 0 || value.nanoAdjustment() >= 1000000000) {
            throw InvalidTemporalValueException(501);
        }
        try {
            taiSec : long = addChecked(value.wholeSeconds(), 19L);
            taiVal : TimeScaleValue = TimeScaleValue(taiSec, value.nanoAdjustment());
            return TaiTimeScale::instance().instantFromValue(taiVal);
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }
}

/**
 * TimeScales — utility facade for standard time scales.
 */
public final class TimeScales {
public:
    static utc() : TimeScale& {
        return UtcTimeScale::instance();
    }

    static tai() : TimeScale& {
        return TaiTimeScale::instance();
    }

    static gps() : TimeScale& {
        return GpsTimeScale::instance();
    }
}

/**
 * TimeSourceId — identifier for a time measurement source.
 */
public final class TimeSourceId {
private:
    _value : String;

public:
    TimeSourceId() {
        _value = String("SystemClock");
    }

    TimeSourceId(value: const String&) {
        _value = value;
    }

    static of(value: const String&) : TimeSourceId throws(InvalidTemporalValueException) {
        if (value.empty()) {
            throw InvalidTemporalValueException(501);
        }
        i : unsigned int = 0u;
        sz : unsigned int = value.size();
        while (i < sz) {
            c : char = value.at(i);
            isAlpha : bool = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            isDigit : bool = (c >= '0' && c <= '9');
            if (!isAlpha && !isDigit && c != '-' && c != '_' && c != '.' && c != '/') {
                throw InvalidTemporalValueException(501);
            }
            ++i;
        }
        return TimeSourceId(value);
    }

    static systemClock() : TimeSourceId {
        return TimeSourceId(String("SystemClock"));
    }

    static ntp() : TimeSourceId {
        return TimeSourceId(String("NTP"));
    }

    static ptp() : TimeSourceId {
        return TimeSourceId(String("PTP"));
    }

    static gnss() : TimeSourceId {
        return TimeSourceId(String("GNSS"));
    }

    static rtc() : TimeSourceId {
        return TimeSourceId(String("RTC"));
    }

    const value() : String {
        return _value;
    }

    const operator==(other: const TimeSourceId&) : bool {
        return _value == other._value;
    }

    const operator!=(other: const TimeSourceId&) : bool {
        return _value != other._value;
    }
}

/**
 * TimeObservation — immutable record of a time measurement.
 *
 * Encapsulates the observed Instant, the source identity, the declared TimeScale,
 * the clock resolution, and optional measurement uncertainty.
 */
public final class TimeObservation {
private:
    _instant        : time::Instant;
    _sourceId       : TimeSourceId;
    _scale          : TimeScale*;
    _resolution     : time::Duration;
    _hasUncertainty : bool;
    _uncertainty    : time::Duration;

public:
    TimeObservation() {
        _instant = time::Instant();
        _sourceId = TimeSourceId();
        _scale = &UtcTimeScale::instance();
        _resolution = time::Duration();
        _hasUncertainty = false;
        _uncertainty = time::Duration();
    }

    TimeObservation(instant: const time::Instant&, sourceId: const TimeSourceId&, scale: TimeScale&, resolution: const time::Duration&) {
        _instant = instant;
        _sourceId = sourceId;
        _scale = &scale;
        _resolution = resolution;
        _hasUncertainty = false;
        _uncertainty = time::Duration();
    }

    TimeObservation(instant: const time::Instant&, sourceId: const TimeSourceId&, scale: TimeScale&, resolution: const time::Duration&, uncertainty: const time::Duration&) {
        _instant = instant;
        _sourceId = sourceId;
        _scale = &scale;
        _resolution = resolution;
        _hasUncertainty = true;
        _uncertainty = uncertainty;
    }

    TimeObservation(instant: const time::Instant&, sourceId: const TimeSourceId&, scale: TimeScale&, resolution: const time::Duration&, uncertainty: const Optional<time::Duration>&) {
        _instant = instant;
        _sourceId = sourceId;
        _scale = &scale;
        _resolution = resolution;
        _hasUncertainty = uncertainty.hasValue();
        if (_hasUncertainty) {
            _uncertainty = uncertainty.get();
        } else {
            _uncertainty = time::Duration();
        }
    }

    TimeObservation(other: const TimeObservation&) {
        _instant = other._instant;
        _sourceId = other._sourceId;
        _scale = other._scale;
        _resolution = other._resolution;
        _hasUncertainty = other._hasUncertainty;
        _uncertainty = other._uncertainty;
    }

    const instant() : time::Instant {
        return _instant;
    }

    const sourceId() : TimeSourceId {
        return _sourceId;
    }

    const scale() : TimeScale& {
        return *_scale;
    }

    const resolution() : time::Duration {
        return _resolution;
    }

    const hasUncertainty() : bool {
        return _hasUncertainty;
    }

    const uncertainty() : Optional<time::Duration> {
        if (_hasUncertainty) {
            return Optional<time::Duration>(_uncertainty);
        }
        return Optional<time::Duration>();
    }

    const operator==(other: const TimeObservation&) : bool {
        if (_instant != other._instant) return false;
        if (_sourceId != other._sourceId) return false;
        if (_scale->id() != other.scale().id()) return false;
        if (_resolution != other._resolution) return false;
        if (_hasUncertainty != other._hasUncertainty) return false;
        if (_hasUncertainty && _uncertainty != other._uncertainty) return false;
        return true;
    }

    const operator!=(other: const TimeObservation&) : bool {
        return !(this == other);
    }
}

/**
 * TimeSource — contract for entities producing time observations.
 */
public interface TimeSource {
    id() : TimeSourceId;
    scale() : TimeScale&;
    observation() : TimeObservation throws(TimeDataUnavailableException, TimeScaleDataUnavailableException);
}

} // namespace time
