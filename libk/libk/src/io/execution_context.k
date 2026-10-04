/*
 * K Language standard library — base module
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

namespace io {

@ffi::Extern("C") private __k_io_execution_context_current() : NativeIoOperation*;
@ffi::Extern("C") private __k_io_execution_context_is_cancellation_requested() : int;
@ffi::Extern("C") private __k_io_is_reaper() : int;

/**
 * ExecutionContext — base context available to executing callbacks.
 *
 * Allows callbacks executing on worker threads to observe cooperative
 * cancellation requests without modifying thread interrupt state.
 */
public class ExecutionContext {
public:
    ExecutionContext() -> default;
    ~ExecutionContext() {}

    /**
     * Check if cancellation or interruption was requested on this context.
     */
    const isCancellationRequested() : bool {
        return false;
    }

    /**
     * Return true if cancellation was requested on the current thread's context TLS.
     */
    static isCancellationRequestedCurrent() : bool {
        return __k_io_execution_context_is_cancellation_requested() != 0;
    }

    /**
     * Return the ExecutionContext currently active on the calling thread.
     */
    static current() : ExecutionContext! {
        return IoOperation::current();
    }
}

} // namespace io
