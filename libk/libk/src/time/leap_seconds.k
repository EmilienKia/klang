/*
 * K Language standard library — Leap seconds and POSIX leap second policy
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

// ── Private FFI declarations for leap second runtime ─────────────────────────
@ffi::Extern("C") private __k_time_leap_second_is_leap_second(epoch_second: long) : int;
@ffi::Extern("C") private __k_time_leap_second_tai_offset(epoch_second: long) : long;
@ffi::Extern("C") private __k_time_leap_second_count() : int;
@ffi::Extern("C") private __k_time_leap_second_load(file_path: const byte*) : int;
@ffi::Extern("C") private __k_time_leap_second_is_leap_date(year: long, month: int, day: int) : int;

/**
 * Policy defining how a leap second (second label 60) is converted
 * to or from a POSIX timestamp.
 */
public final class PosixLeapSecondPolicy {
private:
    _mode : int;

public:
    PosixLeapSecondPolicy() {
        _mode = 0;
    }

    PosixLeapSecondPolicy(mode: int) {
        _mode = mode;
    }

    static reject() : PosixLeapSecondPolicy& {
        return _rejectPolicyInstance;
    }

    static foldToPreviousSecond() : PosixLeapSecondPolicy& {
        return _foldPrevPolicyInstance;
    }

    static foldToFollowingSecond() : PosixLeapSecondPolicy& {
        return _foldNextPolicyInstance;
    }

    const mode() : int {
        return _mode;
    }

    const operator==(other: const PosixLeapSecondPolicy&) : bool {
        return _mode == other._mode;
    }

    const operator!=(other: const PosixLeapSecondPolicy&) : bool {
        return _mode != other._mode;
    }
}

private _rejectPolicyInstance   : PosixLeapSecondPolicy(0);
private _foldPrevPolicyInstance : PosixLeapSecondPolicy(1);
private _foldNextPolicyInstance : PosixLeapSecondPolicy(2);

/**
 * LeapSecondTable — provides access to historical and configured leap seconds.
 *
 * Validates civil :60 seconds against historical leap-second records.
 * Throws LeapSecondException(504) when a :60 second occurs on a non-leap date.
 */
public final class LeapSecondTable {
public:
    /**
     * Determine whether the given epoch second corresponds to a leap second event.
     */
    static isLeapSecond(epochSecond: long) : bool {
        return __k_time_leap_second_is_leap_second(epochSecond) != 0;
    }

    /**
     * Return the TAI - UTC difference in seconds at epochSecond.
     */
    static taiOffset(epochSecond: long) : long {
        return __k_time_leap_second_tai_offset(epochSecond);
    }

    /**
     * Return the number of historical/loaded leap second records.
     */
    static count() : int {
        return __k_time_leap_second_count();
    }

    /**
     * Check whether (year, month, day) had a scheduled leap second at 23:59:60.
     */
    static isLeapDate(year: long, month: int, day: int) : bool {
        return __k_time_leap_second_is_leap_date(year, month, day) != 0;
    }

    /**
     * Validate a civil date and time when second == 60.
     * Throws LeapSecondException(504) if second == 60 but the date or time is not
     * a valid leap second event (23:59:60 on a declared leap second date).
     */
    static validateLeapSecond(year: long, month: int, day: int,
                              hour: int, minute: int, second: int)
        throws(LeapSecondException, InvalidTemporalValueException) {
        if (second == 60) {
            if (hour != 23 || minute != 59 || !isLeapDate(year, month, day)) {
                throw LeapSecondException(504);
            }
        } else if (second < 0 || second > 60) {
            throw InvalidTemporalValueException(501);
        }
    }

    /**
     * Validate a LocalDateTime for leap second compliance.
     * Throws LeapSecondException(504) if its time has second == 60 but its date is not
     * a declared leap second date.
     */
    static validate(dateTime: const LocalDateTime&) throws(LeapSecondException) {
        if (dateTime.time().second() == 60) {
            if (dateTime.time().hour() != 23 || dateTime.time().minute() != 59 ||
                !isLeapDate(dateTime.date().year(), dateTime.date().month(), dateTime.date().day())) {
                throw LeapSecondException(504);
            }
        }
    }

    /**
     * Load an external leapseconds file.
     * Returns 0 on success, non-zero on error.
     */
    static load(filePath: const String&) : int {
        u8 : unsigned byte[]! = filePath.toUtf8();
        return __k_time_leap_second_load((const byte*) &u8[0]);
    }
}

} // namespace time
