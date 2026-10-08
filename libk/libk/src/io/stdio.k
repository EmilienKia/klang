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

private _activeApplication() : ::k::Application& {
    application : OptionalRef<::k::Application> = ::k::Application::getApplication();
    if (!application.hasValue()) {
        throw NoActiveApplicationError();
    }
    return application.get();
}

/** Borrow the active application's standard input byte stream. */
public stdinBytes() : InputStream<byte>& {
    application : ::k::Application& = _activeApplication();
    return application.stdinBytes();
}

/** Borrow the active application's standard input byte stream. */
public stdin() : InputStream<byte>& {
    application : ::k::Application& = _activeApplication();
    return application.stdin();
}

/** Borrow the active application's UTF-8 buffered standard input reader. */
public stdinReader() : BufferedReader& {
    application : ::k::Application& = _activeApplication();
    return application.stdinReader();
}

/** Borrow the active application's standard output byte stream. */
public stdoutBytes() : OutputStream<byte>& {
    application : ::k::Application& = _activeApplication();
    return application.stdoutBytes();
}

/** Borrow the active application's standard error byte stream. */
public stderrBytes() : OutputStream<byte>& {
    application : ::k::Application& = _activeApplication();
    return application.stderrBytes();
}

/** Borrow the active application's UTF-8 standard output printer. */
public stdout() : TextPrinter& {
    application : ::k::Application& = _activeApplication();
    return application.stdout();
}

/** Borrow the active application's UTF-8 standard error printer. */
public stderr() : TextPrinter& {
    application : ::k::Application& = _activeApplication();
    return application.stderr();
}

/** Borrow the active application's legacy byte-oriented standard output printer. */
public stdoutPrint() : PrintStream& {
    application : ::k::Application& = _activeApplication();
    return application.stdoutPrint();
}

/** Borrow the active application's legacy byte-oriented standard error printer. */
public stderrPrint() : PrintStream& {
    application : ::k::Application& = _activeApplication();
    return application.stderrPrint();
}

} // namespace io
