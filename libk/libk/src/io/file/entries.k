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

namespace io::file {

/**
 * FileSystemEntryType — enumerate kinds of filesystem entries.
 */
public enum FileSystemEntryType {
    NotFound;
    File;
    Directory;
    Symlink;
    Other;
}

/**
 * Common base class for filesystem entries.
 */
public abstract class FileSystemEntry : public Object {
protected:
    _path : Path;
    _fs   : FileSystem*;

public:
    FileSystemEntry(path: const Path&, fs: FileSystem&) : _path(path), _fs(&fs) {}

    FileSystemEntry(other: const FileSystemEntry&) : _path(other._path), _fs(other._fs) {}

    const path() : const Path& {
        return _path;
    }

    const fileSystem() : FileSystem& {
        return *_fs;
    }

    const name() : String {
        return _path.fileName();
    }

    abstract const type() : FileSystemEntryType;

    const exists() : bool {
        return _fs->exists(_path);
    }

    remove() : bool {
        return _fs->remove(_path);
    }

    rename(dst: const Path&) : bool {
        return _fs->rename(_path, dst);
    }
}

/**
 * Directory entry in a file system.
 */
public class Directory : public FileSystemEntry {
public:
    Directory(path: const Path&, fs: FileSystem&) : FileSystemEntry(path, fs) {}

    Directory(other: const Directory&) : FileSystemEntry(other) {}

    override const type() : FileSystemEntryType {
        return FileSystemEntryType::Directory;
    }

    create(recursive: bool) : bool {
        if (recursive) {
            return _fs->createDirectories(_path);
        }
        return _fs->createDirectory(_path);
    }

    create() : bool {
        return create(false);
    }

    entries() : Vector<AnyFileSystemEntry>! {
        return _fs->entries(_path);
    }

    walk() : Vector<AnyFileSystemEntry>! {
        return _fs->walk(_path);
    }
}

/**
 * Regular file entry in a file system.
 */
public class File : public FileSystemEntry {
public:
    File(path: const Path&, fs: FileSystem&) : FileSystemEntry(path, fs) {}

    File(other: const File&) : FileSystemEntry(other) {}

    override const type() : FileSystemEntryType {
        return FileSystemEntryType::File;
    }

    const size() : long {
        return _fs->size(_path);
    }

    openInput() : FileInputStream! {
        return new FileInputStream(_path);
    }

    openOutput(append: bool) : FileOutputStream! {
        return new FileOutputStream(_path, append);
    }

    openOutput() : FileOutputStream! {
        return openOutput(false);
    }

    openChannel(options: int) : FileChannel! {
        return FileChannel::open(_path, options);
    }

    openChannel() : FileChannel! {
        return FileChannel::open(_path);
    }
}

/**
 * Symbolic link entry in a file system.
 */
public class Symlink : public FileSystemEntry {
public:
    Symlink(path: const Path&, fs: FileSystem&) : FileSystemEntry(path, fs) {}

    Symlink(other: const Symlink&) : FileSystemEntry(other) {}

    override const type() : FileSystemEntryType {
        return FileSystemEntryType::Symlink;
    }

    target() : Path! {
        return _fs->resolveSymlink(_path);
    }

    resolvedTarget() : Path! {
        curr : Path! = target();
        visited : int = 0;
        while (curr != null && _fs->entryType(*curr) == FileSystemEntryType::Symlink && visited < 32) {
            nextP : Path! = _fs->resolveSymlink(*curr);
            delete curr;
            curr = nextP;
            ++visited;
        }
        return curr;
    }
}

/**
 * Polymorphic union holding any specialized filesystem entry.
 */
public union AnyFileSystemEntry : FileSystemEntry {
    directory : Directory;
    file      : File;
    symlink   : Symlink;
}

} // namespace io::file
