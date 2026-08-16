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

/**
 * NetworkAddress — immutable TCP endpoint address.
 *
 * Phase 5 currently supports IPv4 numeric hosts and "localhost".
 */
public class NetworkAddress {
    _host : char[]!;
    _port : int;

public:
    NetworkAddress(host: const char[], port: int) : _host(null), _port(port) {
        n : int = host.size;
        if (n > 0 && host[n - 1] == (char) 0) {
            --n;
        }
        _host = new char[n + 1];
        i : int = 0;
        while (i < n) {
            _host[i] = host[i];
            ++i;
        }
        _host[n] = (char) 0;
    }

    static of(host: const char[], port: int) : NetworkAddress! {
        return new NetworkAddress(host, port);
    }

    static loopback(port: int) : NetworkAddress! {
        return new NetworkAddress("127.0.0.1", port);
    }

    static any(port: int) : NetworkAddress! {
        return new NetworkAddress("*", port);
    }

    const host() : const char[]? {
        return _host;
    }

    const port() : int {
        return _port;
    }

    const nativeHost() : const char* {
        return &_host[0];
    }
}

} // namespace io

