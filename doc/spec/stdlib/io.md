# I/O Streams

**Module:** `k`  
**Namespace:** `io`  
**Source:** `libk/libk/src/io/`

---

## Overview

The `k::io` namespace provides Java-inspired stream abstractions for
byte-oriented I/O.  All types are part of the base standard library module `k`
and are auto-imported — no explicit `import` statement is needed.

The hierarchy:

```
InputStream (interface)
├── ByteArrayInputStream
├── FileInputStream
└── FilterInputStream
    ├── BufferedInputStream
    └── DataInputStream (also implements DataInput)

OutputStream (interface)
├── ByteArrayOutputStream
├── FileOutputStream
└── FilterOutputStream
    ├── BufferedOutputStream
    ├── DataOutputStream (also implements DataOutput)
    └── PrintStream

DataInput  (interface)
DataOutput (interface)
```

---

## Interfaces

### InputStream

Abstract interface for reading bytes.

```k
interface InputStream {
    read() : int;
    read(buf: byte[], off: int, len: int) : int;
    read(buf: byte[]) : int;
    skip(n: long) : long;
    available() : int;
    close();
}
```

| Method | Description |
|--------|-------------|
| `read() : int` | Read a single byte (0–255), or `-1` on end of stream. |
| `read(buf, off, len) : int` | Read up to `len` bytes into `buf` starting at `off`. Returns bytes read or `-1` on EOF. |
| `read(buf) : int` | Read up to `buf.size` bytes into `buf`. Returns bytes read or `-1` on EOF. |
| `skip(n) : long` | Skip up to `n` bytes. Returns actual bytes skipped. |
| `available() : int` | Return bytes that can be read without blocking. |
| `close()` | Close the stream and release resources. |

---

### OutputStream

Abstract interface for writing bytes.

```k
interface OutputStream {
    write(b: int) : OutputStream&;
    write(buf: const byte[], off: int, len: int) : OutputStream&;
    write(buf: const byte[]) : OutputStream&;
    flush() : OutputStream&;
    close() : OutputStream&;
}
```

All mutating methods return `OutputStream&` for fluent chaining.

| Method | Description |
|--------|-------------|
| `write(b)` | Write the low 8 bits of `b`. |
| `write(buf, off, len)` | Write `len` bytes from `buf` starting at `off`. |
| `write(buf)` | Write all `buf.size` bytes from `buf`. |
| `flush()` | Flush any buffered output. |
| `close()` | Flush and close the stream. |

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

### ByteArrayInputStream

Reads bytes from an in-memory byte array.

```k
class ByteArrayInputStream : public InputStream {
    ByteArrayInputStream(buf: byte[]!, size: int);
}
```

The constructor takes ownership of the byte array `buf`.
`size` is the number of valid bytes. Reading returns `-1` when all bytes
are consumed.

---

### ByteArrayOutputStream

Writes bytes to a growable in-memory byte array.

```k
class ByteArrayOutputStream : public OutputStream {
    ByteArrayOutputStream();
    ByteArrayOutputStream(capacity: int);

    size() : int;
    toByteArray() : byte[]!;
    reset();
    writeTo(out: OutputStream&);
}
```

| Method | Description |
|--------|-------------|
| `size() : int` | Return the number of bytes written. |
| `toByteArray() : byte[]!` | Return a copy of the written data. |
| `reset()` | Reset the byte count to zero (buffer retained). |
| `writeTo(out)` | Write entire content to another output stream. |

---

### FileInputStream

Reads bytes from a file via a C `FILE*` handle.  Implements `InputStream`.

```k
class FileInputStream : public InputStream {
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

Writes bytes to a file via a C `FILE*` handle.  Implements `OutputStream`.

```k
class FileOutputStream : public OutputStream {
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

Wraps an `InputStream` and delegates all calls.  Subclasses override
specific methods to add behaviour (buffering, data decoding, etc.).

The wrapped stream is held by pointer — the caller retains ownership.

```k
class FilterInputStream : public InputStream {
    FilterInputStream(input: InputStream*);
}
```

---

### FilterOutputStream

Wraps an `OutputStream` and delegates all calls.  `close()` calls
`flush()` before closing the underlying stream.

```k
class FilterOutputStream : public OutputStream {
    FilterOutputStream(output: OutputStream*);
}
```

---

### BufferedInputStream

Adds an internal buffer to reduce the number of read calls to the
underlying stream.  Default buffer size is 8192 bytes.

```k
class BufferedInputStream : public FilterInputStream {
    BufferedInputStream(input: InputStream*);
    BufferedInputStream(input: InputStream*, size: int);
}
```

---

### BufferedOutputStream

Adds an internal buffer to reduce the number of write calls to the
underlying stream.  Default buffer size is 8192 bytes.
`flush()` and `close()` force remaining buffered data through.

```k
class BufferedOutputStream : public FilterOutputStream {
    BufferedOutputStream(output: OutputStream*);
    BufferedOutputStream(output: OutputStream*, size: int);
}
```

---

### DataInputStream

Reads primitive types from an underlying `InputStream` using the platform
native byte order.  Extends `FilterInputStream` and implements `DataInput`.

```k
class DataInputStream : public FilterInputStream, public DataInput {
    DataInputStream(input: InputStream*);
}
```

> **Note:** Virtual dispatch through a `DataInput&` reference is not yet
> supported (secondary base limitation).  Use direct calls on the
> `DataInputStream` object.

---

### DataOutputStream

Writes primitive types to an underlying `OutputStream` using the platform
native byte order.  Extends `FilterOutputStream` and implements `DataOutput`.

```k
class DataOutputStream : public FilterOutputStream, public DataOutput {
    DataOutputStream(output: OutputStream*);

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

Extends `FilterOutputStream`.  The wrapped stream is held by pointer
(non-owning).

```k
class PrintStream : public FilterOutputStream {
    PrintStream(output: OutputStream*);

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
| `k::io::stdin` | `InputStream&` | `FileInputStream` wrapping libc `stdin` | Standard input stream. |
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
    b : int = k::io::stdin.read();
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
    baos : k::io::ByteArrayOutputStream;
    dos : k::io::DataOutputStream(&baos);
    dos.writeInt(42)
       .writeLong(1234567890)
       .writeBool(true)
       .flush();

    arr : byte[]* = baos.toByteArray();
    bais : k::io::ByteArrayInputStream(arr, baos.size());
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

    b : int = k::io::stdin.read();
    while (b != -1) {
        k::io::stdout.write(b);
        k::io::stderr.write(b);
        b = k::io::stdin.read();
    }

    k::io::stdout.println();
    return 0;
}
```








