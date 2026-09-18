# Guide to Files and File Systems in K (`k::io::file`)

The K standard library provides a structured, modern, and type-safe file system
subsystem under the `k::io::file` namespace.

Rather than conflating file paths, disk operations, directories, and data streams
into a few monolithic classes, K separates filesystem operations into three
distinct and cohesive tiers:

1. **Location representation** (`Path`): An immutable, structured representation of
   relative or absolute paths.
2. **File systems and providers** (`FileSystem`, `FileSystemProvider`):
   Façades and abstractions for filesystem-level operations (traversal, copying,
   deletion, symlink resolution) and virtualization (host local vs sandboxed sub-trees).
3. **Specialized entries and data I/O** (`FileSystemEntry`, `File`, `Directory`,
   `Symlink`, `AnyFileSystemEntry`, streams, and channels): Typed domain objects
   and stream/channel handles for reading and writing data.

This guide explains the architecture, provides a decision matrix to help choose
the right type for every task, and illustrates common workflows with complete K
examples.

---

## 1. The Three-Tier Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Tier 1: Location Representation                                         │
│   Path (immutable string segments, root, extension, URI conversion)     │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ attached to / operated on by
┌────────────────────────────────────▼────────────────────────────────────┐
│ Tier 2: File System & Virtualization                                    │
│   FileSystem (façade: copyTree, removeTree, createDirectories, etc.)    │
│   ├── UnixLocalFileSystem (host local filesystem via FileSystem::local) │
│   └── DirectoryFileSystem (sandboxed chroot-like subtree filesystem)    │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ produces / binds
┌────────────────────────────────────▼────────────────────────────────────┐
│ Tier 3: Specialized Entries & Data I/O                                  │
│   AnyFileSystemEntry (polymorphic union)                                │
│   ├── File       ──► openInput(), openOutput(), openChannel()           │
│   ├── Directory  ──► entries(), walk(), create()                        │
│   └── Symlink    ──► target(), resolvedTarget()                         │
│                                                                         │
│   Data I/O Handles:                                                     │
│   - Synchronous:  FileInputStream, FileOutputStream                     │
│   - Asynchronous: FileChannel, AsyncFileInputStream, AsyncFileOutputStream│
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Which Class for Which Task?

Use this decision table to select the appropriate type:

| What do you want to do? | Recommended type(s) | Key operations |
|-------------------------|---------------------|----------------|
| **Represent, parse, or transform a path** | `Path` | `Path::of("/a/b/c.txt")`, `parent()`, `resolve()`, `withExtension()` |
| **Convert between paths and URIs** | `Path`, `Uri` | `path.toUri()`, `Path::fromUri(uri)`, `uri.toPath()` |
| **Query file existence or metadata** | `FileSystem` | `FileSystem::local().exists(p)`, `size(p)`, `entryType(p)` |
| **Delete, copy, or move files / trees** | `FileSystem` | `fs.copyTree(src, dst, true)`, `fs.removeTree(p)`, `fs.move(src, dst)` |
| **Sandbox filesystem operations to a folder** | `DirectoryFileSystem` | `DirectoryFileSystem::of(rootPath)` |
| **Inspect a typed filesystem node** | `AnyFileSystemEntry` | `fs.entry(path)`, `entry->exists()`, `entry.index()` |
| **List or walk directory contents** | `Directory` | `dir.entries()`, `dir.walk()` |
| **Read/write files synchronously** | `FileInputStream`, `FileOutputStream` | `new FileInputStream(p)`, `stream->read(buf)` |
| **Seek, force, or do async/cancellable file I/O** | `FileChannel` | `FileChannel::open(p, OPEN_READ)`, `channel->read(buf, pos)` |
| **Stream file data asynchronously** | `AsyncFileInputStream`, `AsyncFileOutputStream` | `new AsyncFileInputStream(p)` |

---

## 3. Tier 1: Paths and Location Representation (`Path`)

A `Path` represents an immutable location in a file system. Internally, it
stores an array of normalized `String` components, a boolean flag indicating
whether the path is absolute or relative, and an optional pointer to an
attached `FileSystem`.

### 3.1 Construction and Parsing

```k
using k::io::file::Path;

// From string literal or String
p1 : Path("/etc/config.json");
p2 : Path("docs/readme.md");

// Using static factories (allocating on the heap)
p3 : Path! = Path::of("src/main.k");

// Empty relative path
pEmpty : Path();
```

Paths automatically split components around directory separators (`/`) and
discard redundant empty separators.

### 3.2 Inspecting Components

`Path` provides methods to extract parts of a file name and path hierarchy:

```k
p : Path("/var/log/system.backup.tar.gz");

p.isAbsolute();           // true
p.isRelative();           // false
p.segmentCount();         // 4 ("var", "log", "system.backup.tar.gz")
p.segment(0u);            // "var"
p.fileName();             // "system.backup.tar.gz"
p.extension();            // "gz"
p.hasExtension();         // true
p.stem();                 // "system.backup.tar"
```

### 3.3 Fluent Navigation and Transformations

Because `Path` is immutable, every modification produces a new `Path` instance:

```k
base : Path("/srv/data");

// Navigation
child : Path! = base.resolve("app/config.json"); // "/srv/data/app/config.json"
parent : Path! = child->parent();                // "/srv/data/app"
root : Path! = child->root();                    // "/"

// Transformations
renamed : Path! = child->withFileName("settings.xml"); // "/srv/data/app/settings.xml"
bak : Path! = child->withExtension("bak");             // "/srv/data/app/config.bak"

// Normalization & Relativization
messy : Path("/srv/data/../data/app/./config.json");
clean : Path! = messy.normalize(); // "/srv/data/app/config.json"

target : Path("/srv/data/app/logs/today.txt");
rel : Path! = target.relative(base); // "app/logs/today.txt"
```

### 3.4 URI Interoperability

K supports converting between `Path` and standard RFC 3986 `file:` URIs:

```k
using k::io::Uri;
using k::io::file::Path;

p : Path("/home/user/document.pdf");
uri : Uri! = p.toUri(); // "file:///home/user/document.pdf"

// Reverse conversion
fromUri : Path! = Path::fromUri(uri);
// Or directly via Uri member method:
fromUri2 : Path! = uri->toPath();
```

---

## 4. Tier 2: FileSystem and FileSystemProvider

The `FileSystem` class acts as the central façade for filesystem-level operations.
Low-level operations are delegated to a `FileSystemProvider`.

### 4.1 Host Local FileSystem

To access the host machine's filesystem, use the singleton accessor:

```k
using k::io::file::FileSystem;
using k::io::file::Path;

fs : FileSystem& = FileSystem::local();

p : Path("/tmp/scratch");
if (!fs.exists(p)) {
    fs.createDirectories(p);
}
```

### 4.2 Sandboxed Virtualization: `DirectoryFileSystem`

When executing untrusted tasks or isolating sub-components (such as package
managers, builds, or web servers), you can restrict all operations to a specific
directory using `DirectoryFileSystem`:

```k
using k::io::file::DirectoryFileSystem;
using k::io::file::Path;

sandboxRoot : Path("/var/sandboxes/tenant-42");
vfs : DirectoryFileSystem! = DirectoryFileSystem::of(sandboxRoot);

// A path like "/app/data" in vfs is safely mapped to
// "/var/sandboxes/tenant-42/app/data" on the real host.
// Any attempt to traverse above the sandbox (e.g. "/../etc/passwd")
// is clamped and cannot escape sandboxRoot.
```

### 4.3 High-Level Operations Façade

`FileSystem` provides atomic and recursive file manipulation methods:

- **Directory creation**: `createDirectory(path)` (single level) and `createDirectories(path)` (creates intermediate directories, like `mkdir -p`).
- **Copying**: `copy(src, dst, overwrite)` copies a single file. `copyTree(src, dst, overwrite)` recursively copies entire directories and files.
- **Moving/Renaming**: `move(src, dst)` renames or moves a path.
- **Deletion**: `remove(path)` deletes a file or empty directory. `removeIfExists(path)` returns false if missing. `removeTree(path)` recursively removes directory trees (like `rm -rf`).
- **Symlinks**: `createSymbolicLink(link, target)` creates a symlink. `resolveSymlink(link)` reads the destination.

---

## 5. Tier 3: Specialized Entries (`AnyFileSystemEntry`)

Rather than querying multiple flags on a path, K allows attaching paths to a
filesystem to obtain specialized entry objects:

```k
using k::io::file::FileSystem;
using k::io::file::Path;
using k::io::file::AnyFileSystemEntry;
using k::io::file::FileSystemEntryType;

fs : FileSystem& = FileSystem::local();
entry : AnyFileSystemEntry = fs.entry(Path("/etc/hosts"));
```

### 5.1 The `AnyFileSystemEntry` Polymorphic Union

`AnyFileSystemEntry` is a typed union deriving from `FileSystemEntry`. Common
properties can be called directly via `->`:

```k
name : String = entry->name();
exists : bool = entry->exists();
path : const Path& = entry->path();
```

To access type-specific operations, branch on `entry.index()` or inspect types:

```k
using k::io::file::Directory;
using k::io::file::File;
using k::io::file::Symlink;

if (entry.index() == 0u) {
    // Directory
    dir : Directory* = entry.as<Directory>();
    // ...
} else if (entry.index() == 1u) {
    // Regular File
    file : File* = entry.as<File>();
    sz : long = file->size();
} else if (entry.index() == 2u) {
    // Symbolic Link
    sym : Symlink* = entry.as<Symlink>();
    target : Path! = sym->target();
}
```

### 5.2 Traversing Directories

`Directory` offers two listing operations returning `Vector<AnyFileSystemEntry>`:

- `entries()`: Lists direct children of the directory (shallow enumeration).
- `walk()`: Performs a depth-first traversal of all nested files, directories,
  and symlinks.

```k
dirEntry : AnyFileSystemEntry = fs.entry(Path("/var/log"));
if (dirEntry.index() == 0u) {
    dir : Directory* = dirEntry.as<Directory>();
    allFiles : Vector<AnyFileSystemEntry>! = dir->walk();

    for (i : unsigned int = 0u; i < allFiles->size(); ++i) {
        sub : const AnyFileSystemEntry& = allFiles->get(i);
        // Process each sub-entry...
    }
}
```

---

## 6. File I/O: Streams and Channels

K provides two I/O paradigms depending on concurrency and performance needs:

### 6.1 Synchronous Streams (`FileInputStream` / `FileOutputStream`)

Backed by standard C runtime file handles, synchronous streams are simple,
linear, and ideal for command-line utilities, quick file loading, or scripts:

```k
using k::io::file::FileInputStream;
using k::io::file::FileOutputStream;
using k::io::file::Path;

// Writing
out : FileOutputStream! = new FileOutputStream(Path("/tmp/hello.txt"));
out->write("Hello, K!\n");
out->flush();
out->close();
delete out;

// Reading
in : FileInputStream! = new FileInputStream(Path("/tmp/hello.txt"));
buf : byte[256];
n : int = in->read(buf);
in->close();
delete in;
```

A `File` entry also provides direct factory shortcuts:
```k
fileEntry : File* = ...;
inStream : FileInputStream! = fileEntry->openInput();
outStream : FileOutputStream! = fileEntry->openOutput(false); // overwrite
```

### 6.2 Asynchronous Channels (`FileChannel`)

For high-throughput, multi-threaded, or seekable file processing, use `FileChannel`.
`FileChannel` uses the native asynchronous I/O substrate (such as Linux `io_uring`):

- **Seekable & Thread-Safe**: Supports concurrent reads/writes at explicit file
  offsets without mutating shared state.
- **Interruptible**: Blocked transfers can be cancelled immediately via
  `Thread::interrupt()`, throwing `ThreadInterruptionException`.
- **Deadline-Aware**: Read and write operations accept timeouts.
- **Immediate Resource Release**: Closing a channel from another thread safely
  aborts pending kernel operations.

Open flags can be combined bitwise:

| Flag | Meaning |
|------|---------|
| `OPEN_READ` | Open for reading |
| `OPEN_WRITE` | Open for writing |
| `OPEN_CREATE` | Create file if it does not exist |
| `OPEN_TRUNCATE` | Truncate file to zero length |
| `OPEN_APPEND` | Writes append to end |
| `OPEN_EXCLUSIVE` | Fail if file already exists |

Opening and using a channel:

```k
using k::io::ByteBuffer;
using k::io::file::FileChannel;
using k::io::file::OPEN_READ;
using k::io::file::OPEN_WRITE;
using k::io::file::OPEN_CREATE;
using k::io::file::Path;

chan : FileChannel! = FileChannel::open(Path("/tmp/data.bin"), OPEN_READ | OPEN_WRITE | OPEN_CREATE);

// Positional write (does not alter channel position)
buf : ByteBuffer! = ByteBuffer::allocate(1024u);
// ... populate buf ...
chan->write(buf, 0L); // write at offset 0

// Synchronize changes to storage
chan->force(true);

chan->close();
delete chan;
```

### 6.3 Asynchronous Streams (`AsyncFileInputStream` / `AsyncFileOutputStream`)

When stream semantics (`InputStream<byte>`, `OutputStream<byte>`) are desired
alongside asynchronous cancellation, wrap channels in `AsyncFileInputStream` or
`AsyncFileOutputStream`.

---

## 7. Common Recipes and Workflows

### Recipe 1: Safe File Path Extension Normalization

```k
module demo;

using k::io::file::Path;

cleanImagePath(inputPath: const String&) : Path! {
    p : Path(inputPath);
    normalized : Path! = p.normalize();
    if (!normalized->hasExtension() || normalized->extension() == "jpeg") {
        return normalized->withExtension("jpg");
    }
    return normalized;
}
```

### Recipe 2: Recursive File Tree Search

Find all files with a given extension in a directory hierarchy:

```k
module demo;

using k::io::file::FileSystem;
using k::io::file::Path;
using k::io::file::AnyFileSystemEntry;
using k::io::file::Directory;

findFiles(rootPath: const Path&, ext: const String&) : void {
    fs : FileSystem& = FileSystem::local();
    entry : AnyFileSystemEntry = fs.entry(rootPath);

    if (entry.index() == 0u) {
        dir : Directory* = entry.as<Directory>();
        all : Vector<AnyFileSystemEntry>! = dir->walk();

        for (i : unsigned int = 0u; i < all->size(); ++i) {
            item : const AnyFileSystemEntry& = all->get(i);
            if (item.index() == 1u) { // Regular file
                if (item->path().extension() == ext) {
                    // Found matching file: item->path().nativePath()
                }
            }
        }
    }
}
```

### Recipe 3: Deep Directory Copy with Sandboxing

Replicate an entire directory hierarchy securely inside a tenant directory:

```k
module demo;

using k::io::file::DirectoryFileSystem;
using k::io::file::Path;

backupTenant(tenantFolder: const Path&) : bool {
    vfs : DirectoryFileSystem! = DirectoryFileSystem::of(tenantFolder);

    src : Path("/data");
    dst : Path("/backup");

    return vfs->copyTree(src, dst, true);
}
```

---

## 8. Best Practices and Pitfalls

1. **Remember that `Path` is immutable**:
   Calling `path.withExtension("bak")` or `path.resolve("sub")` does not alter
   `path`. Always capture or return the resulting `Path!`.

2. **Clean up heap allocations**:
   Objects allocated with `new` (`FileInputStream`, `FileOutputStream`,
   `FileChannel`, `ByteBuffer`) must be cleaned up via `delete` when finished
   to prevent resource leaks. Always call `close()` before deleting.

3. **Use `createDirectories` for nested folders**:
   `createDirectory(path)` fails if any parent directory is missing. Use
   `createDirectories(path)` when intermediate folders may need creation.

4. **Prefer `FileChannel` for concurrent I/O**:
   Standard `FileInputStream`/`FileOutputStream` handles share single native file
   offsets. For multi-threaded access to the same file, use `FileChannel` with
   positional parameters (`read(buf, offset)`).

5. **Beware of symbolic link loops**:
   When traversing filesystems with symlinks, circular links can cause infinite
   recursion unless tracked or resolved via `fs.resolveSymlink(path)`.
