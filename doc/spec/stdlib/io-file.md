# File System Infrastructure — `k::io::file`

**Module:** `k`  
**Namespace:** `io::file`  
**Source:** `libk/libk/src/io/file/`

---

## 1. Overview

> For a user-oriented tutorial, decision matrix, and common recipes, see the [File System Guide](../../guides/file-system.md).

The `k::io::file` namespace provides modern, structured filesystem abstractions
structured into three cohesive tiers:

1. **Location representation**: `Path` represents an immutable relative or
   absolute path divided into distinct string segments.
2. **File system and operations**: `FileSystem` and `FileSystemProvider` manage
   filesystem traversal, copying, moving, directory recursion, and symbolic link resolution.
3. **Specialized filesystem entries and I/O**: `FileSystemEntry`, `File`,
   `Directory`, `Symlink`, and the polymorphic union `AnyFileSystemEntry`.

---

## 2. Location Representation: `Path`

`Path` is an immutable location representation in a file system. Internally,
it holds an array of `String` components (`String[]!`), an `isAbsolute` flag,
and an optional pointer to an attached `FileSystem`.

### 2.1 Construction

| Method | Description |
|--------|-------------|
| `Path()` | Construct an empty relative path. |
| `Path(path: const char[])` | Construct by parsing a path string. |
| `Path(path: const String&)` | Construct by parsing a `String`. |
| `Path(segs: const String[], isAbs: bool)` | Construct from pre-split segments. |
| `Path(segs: const String[], isAbs: bool, fs: FileSystem*)` | Construct from segments with attached `FileSystem`. |
| `static of(path: const char[]) : Path!` | Factory constructing a heap-allocated `Path`. |
| `static of(path: const String&) : Path!` | Factory from `String`. |
| `static of(path: const String&, fs: FileSystem*) : Path!` | Factory with attached `FileSystem`. |
| `static of(segs: const String[], isAbs: bool) : Path!` | Factory from segments. |
| `static fromUri(uri: const Uri&) : Path!` | Construct a `Path` from a `file:` URI. |

### 2.2 Inspection & Extraction

| Method | Description |
|--------|-------------|
| `isAbsolute() : bool` | Return `true` if path begins at the filesystem root. |
| `isRelative() : bool` | Return `true` if path is relative. |
| `isEmpty() : bool` | Return `true` if path has no components and is relative. |
| `segmentCount() : unsigned int` | Number of segments. |
| `segment(index: unsigned int) : String` | Get segment at given index. |
| `segments() : const String[]?` | View of all segments. |
| `fileName() : String` | Last segment of the path, or empty if none. |
| `extension() : String` | File extension without the leading dot. |
| `hasExtension() : bool` | Whether the file name contains an extension. |
| `stem()` / `nameWithoutExtension() : String` | File name without its extension. |
| `parent() : Path!` | Parent path, or `null` if none. |
| `root() : Path!` | Root component (`/`), or `null` if relative. |
| `fileSystem() : FileSystem*` | Return attached `FileSystem`, or `null`. |

### 2.3 Fluent Transformations

| Method | Description |
|--------|-------------|
| `withFileName(name: const String&) : Path!` | Replace the last component. |
| `withExtension(ext: const String&) : Path!` | Replace or add the extension. |
| `prefix(pref: const Path&) : Path!` | Prepend segments from `pref`. |
| `last() : Path!` | Relative path consisting only of the last component. |
| `resolve(child: const Path&) : Path!` | Resolve `child` against this path. |
| `resolve(child: const String&) : Path!` | Resolve string against this path. |
| `normalize() : Path!` | Eliminate `.` and redundant `..` segments. |
| `absolute() : Path!` | Resolve relative path against working directory. |
| `relative(base: const Path&) : Path!` | Compute relative path from `base` to `this`. |
| `attachedTo(fs: FileSystem&) : Path!` | Clone path attached to `fs`. |
| `toUri() : Uri!` | Convert to a `file:` scheme URI. |

---

## 3. FileSystem & FileSystemProvider

### 3.1 `FileSystemProvider`

Abstract service-provider interface managing low-level operations:

```k
public abstract class FileSystemProvider : public Object {
    abstract const scheme() : String;
    abstract const separator() : String;
    abstract const exists(path: const Path&) : bool;
    abstract const entryType(path: const Path&) : FileSystemEntryType;
    abstract const size(path: const Path&) : long;
    abstract createDirectory(path: const Path&) : bool;
    abstract createDirectories(path: const Path&) : bool;
    abstract remove(path: const Path&) : bool;
    abstract rename(src: const Path&, dst: const Path&) : bool;
    abstract copyFile(src: const Path&, dst: const Path&, overwrite: bool) : bool;
    abstract move(src: const Path&, dst: const Path&) : bool;
    abstract readSymbolicLink(path: const Path&) : Path!;
    abstract createSymbolicLink(link: const Path&, target: const Path&) : bool;
    abstract readDirectory(path: const Path&) : Vector<Path>!;
    abstract openNativeFile(path: const Path&, mode: const char*) : ::k::io::file::CFile*;
}
```

### 3.2 `FileSystem`

High-level interface and façade for filesystem interactions:

- `static local() : FileSystem&` returns the singleton instance representing the host local filesystem.
- `copy(src, dst, overwrite)` and `copyTree(src, dst, overwrite)` copy files or directory trees recursively.
- `move(src, dst)` moves or renames files and directories.
- `remove(path)`, `removeIfExists(path)`, `removeTree(path)` delete files and directory hierarchies.
- `createDirectory(path)` and `createDirectories(path)` create single or nested directories.
- `resolveSymlink(path)` reads the destination of a symbolic link.
- `entry(path) : AnyFileSystemEntry` produces a typed filesystem entry variant.

### 3.3 Concrete Implementations

- **`UnixLocalFileSystemProvider` / `UnixLocalFileSystem`**:
  Private implementation backing `FileSystem::local()`, interacting directly with POSIX syscalls (`stat`, `lstat`, `opendir`, `readdir`, `readlink`, `symlink`).
- **`DirectoryFileSystemProvider` / `DirectoryFileSystem`**:
  Sandboxed filesystem provider rooted at a given local directory. Re-bases virtual absolute paths under the root directory and prevents path traversal escaping (`..`). Factory: `DirectoryFileSystem::of(root: const Path&)`.

---

## 4. Specialized Entries: `AnyFileSystemEntry`

### 4.1 `FileSystemEntryType`

```k
public enum FileSystemEntryType {
    NotFound;
    File;
    Directory;
    Symlink;
    Other;
}
```

### 4.2 Hierarchy

```
FileSystemEntry (abstract class)
├── Directory
├── File
└── Symlink
```

- **`FileSystemEntry`**: Common base providing `path() : const Path&`, `fileSystem() : FileSystem&`, `name() : String`, `exists() : bool`, `remove() : bool`, `rename(dst) : bool`.
- **`Directory`**: Offers `create(recursive: bool)`, `entries() : Vector<AnyFileSystemEntry>!`, and `walk() : Vector<AnyFileSystemEntry>!`.
- **`File`**: Offers `size() : long`, `openInput() : FileInputStream!`, `openOutput(append: bool) : FileOutputStream!`.
- **`Symlink`**: Offers `target() : Path!` and `resolvedTarget() : Path!`.

### 4.3 `AnyFileSystemEntry`

A polymorphic union representing any concrete filesystem entry:

```k
public union AnyFileSystemEntry : FileSystemEntry {
    directory : Directory;
    file      : File;
    symlink   : Symlink;
}
```

Supports direct polymorphic member calls via `->` (e.g. `entry->exists()`, `entry->name()`), discriminant inspection via `entry.index()`, and pattern extraction.

---

## 5. URI Mapping

- `Path::fromUri(uri: const Uri&) : Path!` converts a `file:` URI into a local `Path`.
- `Path.toUri() : Uri!` encodes a `Path` into a RFC 3986 `file:` URI.
- `Uri.toPath() : file::Path!` converts a `file:` URI directly into a `Path`.

---

## 6. File Streams and Channels

All file streaming and channel facilities reside in `k::io::file`:

### 6.1 Synchronous File Streams

- **`CFile`**: Opaque wrapper type representing a native C `FILE*` handle.
- **`FileDescriptor`**: Wrapper for a platform integer file descriptor (`getFd() : int`, `valid() : bool`).
- **`FileInputStream`**: Synchronous file-backed input stream (`InputStream<byte>`).
  - `FileInputStream(path: const Path&)`
  - `FileInputStream(path: const char[])`
  - `FileInputStream(fp: CFile*)`
- **`FileOutputStream`**: Synchronous file-backed output stream (`OutputStream<byte>`).
  - `FileOutputStream(path: const Path&, append: bool = false)`
  - `FileOutputStream(path: const char[], append: bool = false)`
  - `FileOutputStream(fp: CFile*)`

### 6.2 Asynchronous File Channels and Streams

- **`FileChannel`**: Seekable, interruptible channel over a regular file backed by the asynchronous substrate.
  - Open constants: `OPEN_READ`, `OPEN_WRITE`, `OPEN_APPEND`, `OPEN_CREATE`, `OPEN_TRUNCATE`, `OPEN_EXCLUSIVE`.
  - Factories: `FileChannel::open(path: const Path&, options: int)`, `FileChannel::open(path: const Path&)`.
  - Transfers: `read(dst)`, `read(dst, pos, timeout)`, `write(src)`, `write(src, pos, timeout)`, `readFully(dst, pos)`, `writeFully(src, pos)`, `position()`, `size()`, `truncate()`, `force()`.
- **`AsyncFileInputStream`**: Interruptible `InputStream<byte>` backed by a `FileChannel`.
- **`AsyncFileOutputStream`**: Interruptible `OutputStream<byte>` backed by a `FileChannel`.
