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

// ═════════════════════════════════════════════════════════════════════════════
// Standard I/O streams — stdin, stdout, stderr
//
// Provides three public global references connected to the standard C streams:
//   - k::io::stdin  : InputStream&   (reads from libc stdin)
//   - k::io::stdout : PrintStream&   (writes to libc stdout)
//   - k::io::stderr : PrintStream&   (writes to libc stderr)
//
// The underlying objects are private to the library and constructed from raw
// CFile* handles obtained via FFI.  The compiler's global init-order resolver
// ensures correct construction order (streams first, then print wrappers,
// then references).
// ═════════════════════════════════════════════════════════════════════════════

namespace io {

// ── Private backing objects: file streams wrapping libc FILE* ────────────
private _stdinStream  : FileInputStream(__k_io_file_get_stdin());
private _stdoutStream : FileOutputStream(__k_io_file_get_stdout());
private _stderrStream : FileOutputStream(__k_io_file_get_stderr());

// ── Private backing objects: PrintStream wrappers ────────────────────────
private _stdoutPrint : PrintStream(&_stdoutStream);
private _stderrPrint : PrintStream(&_stderrStream);

// ── Public references: standard I/O entry points ─────────────────────────
public stdin  : InputStream& = _stdinStream;
public stdout : PrintStream& = _stdoutPrint;
public stderr : PrintStream& = _stderrPrint;

} // namespace io
