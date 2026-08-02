/*
 * K Language standard library — Selector (Phase 6)
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

public enum InterestOps {
    READ = 1;
    WRITE = 4;
    CONNECT = 8;
    ACCEPT = 16;
}

public class Selector {
public:
    static open() : Selector! {
        return new Selector();
    }

    select() : int {
        throw IOException(0);
    }

    select(timeout: const Duration&) : int {
        throw IOException(0);
    }

    selectNow() : int {
        throw IOException(0);
    }

    wakeup() : Selector& {
        return this;
    }

    close() : void {
    }
}

} // namespace io
