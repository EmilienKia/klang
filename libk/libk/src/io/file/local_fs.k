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
 * UnixLocalFileSystemProvider — private provider backing the local UNIX file system.
 */
private class UnixLocalFileSystemProvider : public FileSystemProvider {
    @ffi::Extern("C") private static __k_io_fs_lstat_type(path: const char*) : int;
    @ffi::Extern("C") private static __k_io_fs_stat_size(path: const char*) : long;
    @ffi::Extern("C") private static __k_io_fs_mkdir(path: const char*, mode: int) : int;
    @ffi::Extern("C") private static __k_io_fs_rmdir(path: const char*) : int;
    @ffi::Extern("C") private static __k_io_fs_unlink(path: const char*) : int;
    @ffi::Extern("C") private static __k_io_fs_rename(src: const char*, dst: const char*) : int;
    @ffi::Extern("C") private static __k_io_fs_copyfile(src: const char*, dst: const char*, overwrite: int) : int;
    @ffi::Extern("C") private static __k_io_fs_readlink(path: const char*, target_buf: char*, max: int) : int;
    @ffi::Extern("C") private static __k_io_fs_symlink(target: const char*, link: const char*) : int;
    @ffi::Extern("C") private static __k_io_fs_opendir(path: const char*) : byte*;
    @ffi::Extern("C") private static __k_io_fs_readdir(dir: byte*, name_buf: char*, max: int, out_type: int*) : int;
    @ffi::Extern("C") private static __k_io_fs_closedir(dir: byte*) : int;

public:
    UnixLocalFileSystemProvider() {}

    override const scheme() : String {
        return String("file");
    }

    override const separator() : String {
        return String("/");
    }

    override const exists(path: const Path&) : bool {
        raw : const char* = path.nativePath();
        return __k_io_fs_lstat_type(raw) >= 0;
    }

    override const entryType(path: const Path&) : FileSystemEntryType {
        raw : const char* = path.nativePath();
        t : int = __k_io_fs_lstat_type(raw);
        if (t == 0) return FileSystemEntryType::File;
        if (t == 1) return FileSystemEntryType::Directory;
        if (t == 2) return FileSystemEntryType::Symlink;
        if (t == 3) return FileSystemEntryType::Other;
        return FileSystemEntryType::NotFound;
    }

    override const size(path: const Path&) : long {
        raw : const char* = path.nativePath();
        return __k_io_fs_stat_size(raw);
    }

    override createDirectory(path: const Path&) : bool {
        raw : const char* = path.nativePath();
        return __k_io_fs_mkdir(raw, 0755) == 0;
    }

    override createDirectories(path: const Path&) : bool {
        if (exists(path)) {
            return entryType(path) == FileSystemEntryType::Directory;
        }
        parentP : Path! = path.parent();
        if (parentP != null) {
            if (!exists(*parentP)) {
                if (!createDirectories(*parentP)) {
                    delete parentP;
                    return false;
                }
            }
            delete parentP;
        }
        return createDirectory(path);
    }

    override remove(path: const Path&) : bool {
        raw : const char* = path.nativePath();
        t : int = __k_io_fs_lstat_type(raw);
        if (t < 0) return false;
        if (t == 1) {
            return __k_io_fs_rmdir(raw) == 0;
        }
        return __k_io_fs_unlink(raw) == 0;
    }

    override rename(src: const Path&, dst: const Path&) : bool {
        sRaw : const char* = src.nativePath();
        dRaw : const char* = dst.nativePath();
        return __k_io_fs_rename(sRaw, dRaw) == 0;
    }

    override copyFile(src: const Path&, dst: const Path&, overwrite: bool) : bool {
        sRaw : const char* = src.nativePath();
        dRaw : const char* = dst.nativePath();
        return __k_io_fs_copyfile(sRaw, dRaw, overwrite ? 1 : 0) == 0;
    }

    override move(src: const Path&, dst: const Path&) : bool {
        if (rename(src, dst)) {
            return true;
        }
        if (copyFile(src, dst, true)) {
            return remove(src);
        }
        return false;
    }

    override readSymbolicLink(path: const Path&) : Path! {
        raw : const char* = path.nativePath();
        buf : char[]! = new char[4096];
        n : int = __k_io_fs_readlink(raw, &buf[0], 4096);
        if (n < 0) {
            delete buf;
            return null;
        }
        p : Path! = new Path(buf);
        delete buf;
        return p;
    }

    override createSymbolicLink(link: const Path&, target: const Path&) : bool {
        tRaw : const char* = target.nativePath();
        lRaw : const char* = link.nativePath();
        return __k_io_fs_symlink(tRaw, lRaw) == 0;
    }

    override readDirectory(path: const Path&) : Vector<Path>! {
        raw : const char* = path.nativePath();
        d : byte* = __k_io_fs_opendir(raw);
        if (d == null) {
            return null;
        }
        res : Vector<Path>! = new Vector<Path>();
        nameBuf : char[]! = new char[1024];
        dummyType : int = 0;
        while (__k_io_fs_readdir(d, &nameBuf[0], 1024, &dummyType) > 0) {
            subName : String(nameBuf);
            child : Path! = path.resolve(subName);
            res->append(*child);
            delete child;
        }
        delete nameBuf;
        __k_io_fs_closedir(d);
        return res;
    }

    override openNativeFile(path: const Path&, mode: const char*) : CFile* {
        raw : const char* = path.nativePath();
        return __k_io_file_fopen(raw, mode);
    }
}

/**
 * Concrete FileSystem representing the local UNIX file system.
 */
private class UnixLocalFileSystem : public FileSystem {
private:
    _prov : UnixLocalFileSystemProvider;
    static _instance : UnixLocalFileSystem* = null;

public:
    UnixLocalFileSystem() {}

    override provider() : FileSystemProvider& {
        return _prov;
    }

    override const separator() : String {
        return String("/");
    }

    override const isOpen() : bool {
        return true;
    }

    override const isReadOnly() : bool {
        return false;
    }

    override const rootDirectories() : Vector<Path>! {
        roots : Vector<Path>! = new Vector<Path>();
        r : Path("/");
        roots->append(r);
        return roots;
    }

    static instance() : UnixLocalFileSystem& {
        if (_instance == null) {
            _instance = new UnixLocalFileSystem();
        }
        return *_instance;
    }
}

} // namespace io::file
