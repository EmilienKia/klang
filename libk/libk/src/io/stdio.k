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

/**
 * Provide access to the process standard I/O streams.
 *
 * This namespace exposes public references bound to the standard C streams:
 * `stdin`, `stdout`, and `stderr`.
 *
 * The underlying stream objects are private library globals created from raw
 * C `FILE*` handles obtained via FFI.
 */

namespace io {

// ── Private backing objects: file streams wrapping libc FILE* ────────────
private _stdinStream  : FileInputStream(__k_io_file_get_stdin());
private _stdoutStream : FileOutputStream(__k_io_file_get_stdout());
private _stderrStream : FileOutputStream(__k_io_file_get_stderr());

// ── Private backing objects: PrintStream wrappers ────────────────────────
private _stdoutPrint : PrintStream(&_stdoutStream);
private _stderrPrint : PrintStream(&_stderrStream);

/**
 * Read bytes from the process standard input stream.
 */
public stdin  : InputStream& = _stdinStream;
/**
 * Write formatted text to the process standard output stream.
 */
public stdout : PrintStream& = _stdoutPrint;
/**
 * Write formatted text to the process standard error stream.
 */
public stderr : PrintStream& = _stderrPrint;

} // namespace io
