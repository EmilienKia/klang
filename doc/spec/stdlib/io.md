# I/O Streams

**Module:** `k`  
**Namespace:** `io`  
**Source:** `libk/libk/src/io/`

---

## Overview

The `k::io` namespace provides Java-inspired stream abstractions for
element-oriented I/O.  The `InputStream<T>` / `OutputStream<T>` interfaces and
the array and filter streams are templated over the element type `T`; the
file, buffered, data and print streams are specialised for `byte`.  All types
are part of the base standard library module `k` and are auto-imported — no
explicit `import` statement is needed.

For interruptible, cancellable file I/O built on io_uring, see
[Asynchronous I/O](io-async.md), which adds `ByteBuffer`, `Path`, `FileChannel`
and the `AsyncFileInputStream` / `AsyncFileOutputStream` adapters.
For interruptible TCP networking (`NetworkAddress`, `SocketChannel`,
`ServerSocket`), see [Asynchronous Network I/O](io-network.md).

The hierarchy:

```
InputStream<T> (interface)
├── ArrayInputStream<T>
├── FileInputStream          (InputStream<byte>)
└── FilterInputStream<T>
    ├── BufferedInputStream   (FilterInputStream<byte>)
    └── DataInputStream       (FilterInputStream<byte>, also implements DataInput)
└── TransformInputStream<I,O>
    ├── OneToOneTransformInputStream<I,O>
    ├── OneToManyTransformInputStream<I,O>
    ├── ManyToOneTransformInputStream<I,O>
    └── ManyToManyTransformInputStream<I,O>

OutputStream<T> (interface)
├── ArrayOutputStream<T>
├── FileOutputStream         (OutputStream<byte>)
└── FilterOutputStream<T>
    ├── BufferedOutputStream  (FilterOutputStream<byte>)
    ├── DataOutputStream      (FilterOutputStream<byte>, also implements DataOutput)
    └── PrintStream           (FilterOutputStream<byte>)
└── TransformOutputStream<I,O>
    ├── OneToOneTransformOutputStream<I,O>
    ├── OneToManyTransformOutputStream<I,O>
    ├── ManyToOneTransformOutputStream<I,O>
    └── ManyToManyTransformOutputStream<I,O>

DataInput  (interface)
DataOutput (interface)
```

---

## Interfaces

### InputStream

Abstract interface for reading a sequence of `T` values.

```k
template<typename T>
interface InputStream {
    read() : Optional<T>;
    read(buf: T[], off: unsigned int, len: unsigned int) : Expected<unsigned int, int>;
    read(buf: T[]) : Expected<unsigned int, int>;
    skip(n: unsigned long) : unsigned long;
    available() : Expected<unsigned int, int>;
    close();
}
```

| Method | Description |
|--------|-------------|
| `read() : Optional<T>` | Read a single value; empty `Optional` at end of stream. |
| `read(buf, off, len) : Expected<unsigned int, int>` | Read up to `len` values into `buf` starting at `off`. Returns the number of values read (`0` = end of stream while still open), or an error code when the stream is closed/broken. |
| `read(buf) : Expected<unsigned int, int>` | Read up to `buf.size` values into `buf`. |
| `skip(n) : unsigned long` | Skip up to `n` values. Returns the actual number skipped. |
| `available() : Expected<unsigned int, int>` | Number of values readable without blocking, or an error code. |
| `close()` | Close the stream and release resources. |

---

### OutputStream

Abstract interface for writing a sequence of `T` values.

```k
template<typename T>
interface OutputStream {
    write(b: T) : OutputStream<T>&;
    write(buf: const T[], off: unsigned int, len: unsigned int) : OutputStream<T>&;
    write(buf: const T[]) : OutputStream<T>&;
    flush() : OutputStream<T>&;
    close() : OutputStream<T>&;
}
```

All mutating methods return `OutputStream<T>&` for fluent chaining.

| Method | Description |
|--------|-------------|
| `write(b)` | Write a single value `b`. |
| `write(buf, off, len)` | Write `len` values from `buf` starting at `off`. |
| `write(buf)` | Write all `buf.size` values. |
| `flush()` | Flush any buffered output. |
| `close()` | Flush and close the stream. |

---

### TransformInputStream / TransformOutputStream

Decorator bases for stream chains that transform and optionally filter values.
The input-side variants consume values from an `InputStream<I>` and expose
`InputStream<O>`, while the output-side variants consume values written as `I`
and forward transformed values to an `OutputStream<O>`.

```k
template<typename I, typename O>
class TransformInputStream : public InputStream<O> {
    TransformInputStream(input: InputStream<I>*);
    TransformInputStream();
}

template<typename I, typename O>
class TransformOutputStream : public OutputStream<I> {
    TransformOutputStream(output: OutputStream<O>*);
    TransformOutputStream();
}

template<typename I, typename O>
abstract class OneToOneTransformInputStream : public TransformInputStream<I, O> {
    transform(in: const I&) : Optional<O>;
}

template<typename I, typename O>
abstract class OneToManyTransformInputStream : public TransformInputStream<I, O> {
    transform(in: const I&) : Vector<O>;
}

template<typename I, typename O>
abstract class ManyToOneTransformInputStream : public TransformInputStream<I, O> {
    transform(in: const Vector<I>&) : Optional<O>;
}

template<typename I, typename O>
abstract class ManyToManyTransformInputStream : public TransformInputStream<I, O> {
    transform(in: const Vector<I>&) : Vector<O>;
}

template<typename I, typename O>
abstract class OneToOneTransformOutputStream : public TransformOutputStream<I, O> {
    transform(in: const I&) : Optional<O>;
}

template<typename I, typename O>
abstract class OneToManyTransformOutputStream : public TransformOutputStream<I, O> {
    transform(in: const I&) : Vector<O>;
}

template<typename I, typename O>
abstract class ManyToOneTransformOutputStream : public TransformOutputStream<I, O> {
    transform(in: const Vector<I>&) : Optional<O>;
}

template<typename I, typename O>
abstract class ManyToManyTransformOutputStream : public TransformOutputStream<I, O> {
    transform(in: const Vector<I>&) : Vector<O>;
}
```

The one-to-many and many-to-many variants keep a small intermediate buffer so
they can preserve output ordering while consuming or producing multiple values
per transformation step.

---

### DataInput

Interface for reading primitive types from a byte stream.
All multi-byte values use the **platform native** byte order.

```k
interface DataInput {
    readByte() : byte;
    readChar() : char;
    readShort() : short;
    readUnsignedShort() : unsigned short;
    readInt() : int;
    readUnsignedInt() : unsigned int;
    readLong() : long;
    readUnsignedLong() : unsigned long;
    readFloat() : float;
    readDouble() : double;
    readBool() : bool;
    readFully(buf: byte[], off: int, len: int);
    readFully(buf: byte[]);
}
```

---

### DataOutput

Interface for writing primitive types to a byte stream.
All multi-byte values use the **platform native** byte order.
All mutating methods return `DataOutput&` for fluent chaining.

```k
interface DataOutput {
    writeByte(v: byte) : DataOutput&;
    writeChar(v: char) : DataOutput&;
    writeShort(v: short) : DataOutput&;
    writeUnsignedShort(v: unsigned short) : DataOutput&;
    writeInt(v: int) : DataOutput&;
    writeUnsignedInt(v: unsigned int) : DataOutput&;
    writeLong(v: long) : DataOutput&;
    writeUnsignedLong(v: unsigned long) : DataOutput&;
    writeFloat(v: float) : DataOutput&;
    writeDouble(v: double) : DataOutput&;
    writeBool(v: bool) : DataOutput&;
}
```

---

## Classes

### ArrayInputStream

Reads `T` values from an in-memory array.

```k
template<typename T>
class ArrayInputStream : public InputStream<T> {
    ArrayInputStream();
    ArrayInputStream(buf: T[]*, size: int);
}
```

The constructor copies the first `size` entries of `buf` into independent
internal storage.  `read()` returns an empty `Optional<T>` once all values
have been consumed.

---

### ArrayOutputStream

Writes `T` values to a growable in-memory array.

```k
template<typename T>
class ArrayOutputStream : public OutputStream<T> {
    ArrayOutputStream();
    ArrayOutputStream(capacity: int);

    size() : int;
    toArray() : T[]!;
    reset();
    writeTo(out: OutputStream<T>&);
}
```

| Method | Description |
|--------|-------------|
| `size() : int` | Return the number of values written. |
| `toArray() : T[]!` | Return a copy of the written data (caller takes ownership). |
| `reset()` | Reset the value count to zero (buffer retained). |
| `writeTo(out)` | Write entire content to another output stream. |

---

### FileInputStream

Reads bytes from a file via a C `FILE*` handle.  Implements `InputStream<byte>`.

```k
class FileInputStream : public InputStream<byte> {
    FileInputStream(path: const char[]);
    FileInputStream(fp: CFile*);

    isOpen() : bool;
    getFD() : FileDescriptor;
}
```

| Constructor / Method | Description |
|----------------------|-------------|
| `FileInputStream(path)` | Open the file at `path` for reading (`"rb"`). |
| `FileInputStream(fp)` | Wrap an existing `CFile*` handle. The stream does **not** take ownership — the caller must keep the handle valid. Used internally to wrap libc `stdin`. |
| `isOpen() : bool` | Return `true` if the underlying file was opened successfully. |
| `getFD() : FileDescriptor` | Return a `FileDescriptor` for the OS-level file descriptor. |

---

### FileOutputStream

Writes bytes to a file via a C `FILE*` handle.  Implements `OutputStream<byte>`.

```k
class FileOutputStream : public OutputStream<byte> {
    FileOutputStream(path: const char[]);
    FileOutputStream(path: const char[], append: bool);
    FileOutputStream(fp: CFile*);

    isOpen() : bool;
    getFD() : FileDescriptor;
}
```

| Constructor / Method | Description |
|----------------------|-------------|
| `FileOutputStream(path)` | Open the file at `path` for writing (`"wb"`). |
| `FileOutputStream(path, append)` | Open the file for writing; if `append` is `true`, use append mode (`"ab"`). |
| `FileOutputStream(fp)` | Wrap an existing `CFile*` handle. The stream does **not** take ownership — the caller must keep the handle valid. Used internally to wrap libc `stdout`/`stderr`. |
| `isOpen() : bool` | Return `true` if the underlying file was opened successfully. |
| `getFD() : FileDescriptor` | Return a `FileDescriptor` for the OS-level file descriptor. |

---

### FilterInputStream

Wraps an `InputStream<T>` and delegates all calls.  Subclasses override
specific methods to add behaviour (buffering, data decoding, etc.).

The wrapped stream is held by pointer — the caller retains ownership.

```k
template<typename T>
class FilterInputStream : public InputStream<T> {
    FilterInputStream(input: InputStream<T>*);
}
```

---

### FilterOutputStream

Wraps an `OutputStream<T>` and delegates all calls.  `close()` calls
`flush()` before closing the underlying stream.

```k
template<typename T>
class FilterOutputStream : public OutputStream<T> {
    FilterOutputStream(output: OutputStream<T>*);
}
```

---

### BufferedInputStream

Adds an internal buffer to reduce the number of read calls to the
underlying stream.  Default buffer size is 8192 bytes.

```k
class BufferedInputStream : public FilterInputStream<byte> {
    BufferedInputStream(input: InputStream<byte>*);
    BufferedInputStream(input: InputStream<byte>*, size: int);
}
```

---

### BufferedOutputStream

Adds an internal buffer to reduce the number of write calls to the
underlying stream.  Default buffer size is 8192 bytes.
`flush()` and `close()` force remaining buffered data through.

```k
class BufferedOutputStream : public FilterOutputStream<byte> {
    BufferedOutputStream(output: OutputStream<byte>*);
    BufferedOutputStream(output: OutputStream<byte>*, size: int);
}
```

---

### DataInputStream

Reads primitive types from an underlying `InputStream` using the platform
native byte order.  Extends `FilterInputStream<byte>` and implements `DataInput`.

```k
class DataInputStream : public FilterInputStream<byte>, public DataInput {
    DataInputStream(input: InputStream<byte>+);
}
```

> **Note:** Virtual dispatch through a `DataInput&` reference is not yet
> supported (secondary base limitation).  Use direct calls on the
> `DataInputStream` object.

---

### DataOutputStream

Writes primitive types to an underlying `OutputStream` using the platform
native byte order.  Extends `FilterOutputStream<byte>` and implements `DataOutput`.

```k
class DataOutputStream : public FilterOutputStream<byte>, public DataOutput {
    DataOutputStream(output: OutputStream<byte>+);

    size() : int;
}
```

| Method | Description |
|--------|-------------|
| `size() : int` | Return total bytes written through this stream. |

> **Note:** Virtual dispatch through a `DataOutput&` reference is not yet
> supported (secondary base limitation).  Use direct calls on the
> `DataOutputStream` object.

---

### PrintStream

Adds convenient `print` / `println` methods to an `OutputStream`.  Converts
primitive values to their textual representation and writes them to the
underlying stream.  All `print`/`println` methods return `PrintStream&` for
fluent chaining.

Extends `FilterOutputStream<byte>`.  The wrapped stream is held by pointer
(non-owning).

```k
class PrintStream : public FilterOutputStream<byte> {
    PrintStream(output: OutputStream<byte>*);

    // ── print (no trailing newline) ──────────────────────
    print(v: bool)           : PrintStream&;
    print(v: char)           : PrintStream&;
    print(v: byte)           : PrintStream&;
    print(v: short)          : PrintStream&;
    print(v: int)            : PrintStream&;
    print(v: long)           : PrintStream&;
    print(v: unsigned short) : PrintStream&;
    print(v: unsigned int)   : PrintStream&;
    print(v: unsigned long)  : PrintStream&;
    print(v: float)          : PrintStream&;
    print(v: double)         : PrintStream&;
    print(v: const char[])   : PrintStream&;

    // ── println (value + newline) ────────────────────────
    println()                 : PrintStream&;
    println(v: bool)          : PrintStream&;
    println(v: char)          : PrintStream&;
    println(v: byte)          : PrintStream&;
    println(v: short)         : PrintStream&;
    println(v: int)           : PrintStream&;
    println(v: long)          : PrintStream&;
    println(v: unsigned short): PrintStream&;
    println(v: unsigned int)  : PrintStream&;
    println(v: unsigned long) : PrintStream&;
    println(v: float)         : PrintStream&;
    println(v: double)        : PrintStream&;
    println(v: const char[])  : PrintStream&;
}
```

| Method | Description |
|--------|-------------|
| `print(v)` | Write the textual representation of `v` (boolean → `"true"`/`"false"`, char → single byte, numbers → decimal string, `const char[]` → characters without trailing null). |
| `println()` | Write a newline (`\n`). |
| `println(v)` | `print(v)` followed by a newline. |

---

## Standard I/O Streams

**Source:** `libk/libk/src/io/stdio.k`

Three global references provide access to the process standard streams,
connected to the underlying libc `stdin`, `stdout`, and `stderr` handles.
They are available as soon as global initialisation completes (before
`main()` is called).

| Reference | Type | Backed by | Description |
|-----------|------|-----------|-------------|
| `k::io::stdin` | `InputStream<byte>&` | `FileInputStream` wrapping libc `stdin` | Standard input stream. |
| `k::io::stdout` | `PrintStream&` | `PrintStream` wrapping a `FileOutputStream` on libc `stdout` | Standard output stream. |
| `k::io::stderr` | `PrintStream&` | `PrintStream` wrapping a `FileOutputStream` on libc `stderr` | Standard error stream. |

The backing objects (`FileInputStream`, `FileOutputStream`, `PrintStream`
instances) are **private** to the library.  Only the three references above
are public.  The compiler's global init-order resolver ensures that
dependent objects are constructed in the correct order.

### Usage

```k
module myapp;

main() : int {
    // Write to standard output
    k::io::stdout.println("Hello, world!");
    k::io::stdout.print("value = ").println(42);

    // Write to standard error
    k::io::stderr.println("An error occurred");

    // Read a byte from standard input
    b : Optional<byte> = k::io::stdin.read();
    return 0;
}
```

---

## Examples

### Data stream round-trip

Round-trip: write primitives with `DataOutputStream`, read them back
with `DataInputStream`.  All multi-byte values use the platform native
byte order.  Output methods return `self` for fluent chaining:

```k
module example;

test() : int {
    baos : k::io::ArrayOutputStream<byte>;
    dos : k::io::DataOutputStream(&baos);
    dos.writeInt(42)
       .writeLong(1234567890)
       .writeBool(true)
       .flush();

    arr : byte[]* = baos.toArray();
    bais : k::io::ArrayInputStream<byte>(arr, baos.size());
    dis : k::io::DataInputStream(&bais);

    v1 : int = dis.readInt();       // 42
    v2 : long = dis.readLong();     // 1234567890
    v3 : bool = dis.readBool();     // true
    return v1;
}
```

### Standard I/O echo

Read bytes from standard input and echo them to standard output and
standard error:

```k
module echo;

main() : int {
    k::io::stdout.print("Enter text: ");
    k::io::stdout.flush();

    b : Optional<byte> = k::io::stdin.read();
    while (b.hasValue()) {
        k::io::stdout.write(b.get());
        k::io::stderr.write(b.get());
        b = k::io::stdin.read();
    }

    k::io::stdout.println();
    return 0;
}
```







