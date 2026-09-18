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
 * Service-provider class for file systems.
 *
 * A provider implements low-level operations for a specific scheme or backing store.
 */
public abstract class FileSystemProvider : public Object {
public:
    abstract const scheme() : String;
    abstract const separator() : String;

    // Metadata
    abstract const exists(path: const Path&) : bool;
    abstract const entryType(path: const Path&) : FileSystemEntryType;
    abstract const size(path: const Path&) : long;

    // Manipulation
    abstract createDirectory(path: const Path&) : bool;
    abstract createDirectories(path: const Path&) : bool;
    abstract remove(path: const Path&) : bool;
    abstract rename(src: const Path&, dst: const Path&) : bool;
    abstract copyFile(src: const Path&, dst: const Path&, overwrite: bool) : bool;
    abstract move(src: const Path&, dst: const Path&) : bool;

    // Symbolic links
    abstract readSymbolicLink(path: const Path&) : Path!;
    abstract createSymbolicLink(link: const Path&, target: const Path&) : bool;

    // Directory reading
    abstract readDirectory(path: const Path&) : Vector<Path>!;

    // Native I/O handle
    abstract openNativeFile(path: const Path&, mode: const char*) : ::k::io::CFile*;
}

/**
 * FileSystem — interface to a file system and factory for path-related operations.
 */
public abstract class FileSystem : public Object {
private:
    _walkInternal(path: const Path&, out: Vector<AnyFileSystemEntry>&) {
        children : Vector<Path>! = provider().readDirectory(path);
        if (children == null) {
            return;
        }
        n : unsigned int = children->size();
        i : unsigned int = 0u;
        while (i < n) {
            child : const Path& = children->get(i);
            ent : AnyFileSystemEntry = entry(child);
            out.append(ent);
            if (entryType(child) == FileSystemEntryType::Directory) {
                _walkInternal(child, out);
            }
            ++i;
        }
        delete children;
    }

public:
    abstract provider() : FileSystemProvider&;
    abstract const separator() : String;
    abstract const isOpen() : bool;
    abstract const isReadOnly() : bool;
    abstract const rootDirectories() : Vector<Path>!;

    // Paths
    getPath(first: const String&) : Path! {
        p : Path! = new Path(first);
        res : Path! = p->attachedTo(this);
        delete p;
        return res;
    }

    getPath(first: const char[]) : Path! {
        p : Path! = new Path(first);
        res : Path! = p->attachedTo(this);
        delete p;
        return res;
    }

    // Metadata & inspection
    const exists(path: const Path&) : bool {
        return provider().exists(path);
    }

    const entryType(path: const Path&) : FileSystemEntryType {
        return provider().entryType(path);
    }

    const size(path: const Path&) : long {
        return provider().size(path);
    }

    // Basic manipulations
    createDirectory(path: const Path&) : bool {
        return provider().createDirectory(path);
    }

    createDirectories(path: const Path&) : bool {
        return provider().createDirectories(path);
    }

    remove(path: const Path&) : bool {
        return provider().remove(path);
    }

    removeIfExists(path: const Path&) : bool {
        if (!exists(path)) {
            return false;
        }
        return remove(path);
    }

    rename(src: const Path&, dst: const Path&) : bool {
        return provider().rename(src, dst);
    }

    copy(src: const Path&, dst: const Path&, overwrite: bool) : bool {
        return provider().copyFile(src, dst, overwrite);
    }

    copy(src: const Path&, dst: const Path&) : bool {
        return copy(src, dst, false);
    }

    move(src: const Path&, dst: const Path&) : bool {
        return provider().move(src, dst);
    }

    resolveSymlink(path: const Path&) : Path! {
        return provider().readSymbolicLink(path);
    }

    // Recursive operations
    removeTree(path: const Path&) : bool {
        t : FileSystemEntryType = entryType(path);
        if (t == FileSystemEntryType::NotFound) {
            return false;
        }
        if (t == FileSystemEntryType::Directory) {
            children : Vector<Path>! = provider().readDirectory(path);
            if (children != null) {
                n : unsigned int = children->size();
                i : unsigned int = 0u;
                while (i < n) {
                    child : const Path& = children->get(i);
                    removeTree(child);
                    ++i;
                }
                delete children;
            }
        }
        return remove(path);
    }

    copyTree(src: const Path&, dst: const Path&, overwrite: bool) : bool {
        t : FileSystemEntryType = entryType(src);
        if (t == FileSystemEntryType::NotFound) {
            return false;
        }
        if (t == FileSystemEntryType::Directory) {
            if (!exists(dst)) {
                if (!createDirectory(dst)) {
                    return false;
                }
            }
            children : Vector<Path>! = provider().readDirectory(src);
            if (children != null) {
                n : unsigned int = children->size();
                i : unsigned int = 0u;
                while (i < n) {
                    child : const Path& = children->get(i);
                    fName : String = child.fileName();
                    dstChild : Path! = dst.resolve(fName);
                    copyTree(child, *dstChild, overwrite);
                    delete dstChild;
                    ++i;
                }
                delete children;
            }
            return true;
        }
        return copy(src, dst, overwrite);
    }

    copyTree(src: const Path&, dst: const Path&) : bool {
        return copyTree(src, dst, false);
    }

    // Specialized entries
    entry(path: const Path&) : AnyFileSystemEntry {
        t : FileSystemEntryType = entryType(path);
        res : AnyFileSystemEntry;
        if (t == FileSystemEntryType::Directory) {
            res.directory = Directory(path, this);
        } else if (t == FileSystemEntryType::Symlink) {
            res.symlink = Symlink(path, this);
        } else {
            res.file = File(path, this);
        }
        return res;
    }

    entries(path: const Path&) : Vector<AnyFileSystemEntry>! {
        res : Vector<AnyFileSystemEntry>! = new Vector<AnyFileSystemEntry>();
        children : Vector<Path>! = provider().readDirectory(path);
        if (children != null) {
            n : unsigned int = children->size();
            i : unsigned int = 0u;
            while (i < n) {
                child : const Path& = children->get(i);
                ent : AnyFileSystemEntry = entry(child);
                res->append(ent);
                ++i;
            }
            delete children;
        }
        return res;
    }

    walk(path: const Path&) : Vector<AnyFileSystemEntry>! {
        res : Vector<AnyFileSystemEntry>! = new Vector<AnyFileSystemEntry>();
        _walkInternal(path, *res);
        return res;
    }

    // Static accessor for the local file system
    static local() : FileSystem& {
        return UnixLocalFileSystem::instance();
    }
}

} // namespace io::file
