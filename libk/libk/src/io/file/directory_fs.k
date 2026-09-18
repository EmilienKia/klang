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
 * DirectoryFileSystemProvider — provides a file system rooted in a local sub-directory (sandbox/chroot).
 */
public class DirectoryFileSystemProvider : public FileSystemProvider {
private:
    _rootPath : Path;
    _fs       : FileSystem*;

    const _toRealPath(virtualPath: const Path&) : Path! {
        n : unsigned int = virtualPath.segmentCount();
        depth : int = 0;
        i : unsigned int = 0u;
        while (i < n) {
            seg : String = virtualPath.segment(i);
            if (seg == String("..")) {
                if (depth <= 0) {
                    return null;
                }
                --depth;
            } else if (seg != String(".")) {
                ++depth;
            }
            ++i;
        }

        norm : Path! = virtualPath.normalize();
        relNorm : Path! = new Path(norm->segments(), false, null);
        delete norm;
        realP : Path! = _rootPath.resolve(*relNorm);
        delete relNorm;
        return realP;
    }

    const _toVirtualPath(realPath: const Path&) : Path! {
        relP : Path! = realPath.relative(_rootPath);
        pStr : String = relP->toPathString();
        sb : StringBuilder("/");
        sb.append(pStr);
        delete relP;
        vStr : String = sb.toString();
        vPath : Path! = new Path(vStr);
        if (_fs != null) {
            attached : Path! = vPath->attachedTo(*_fs);
            delete vPath;
            return attached;
        }
        return vPath;
    }

public:
    DirectoryFileSystemProvider(root: const Path&) : _rootPath(root), _fs(null) {
        absRoot : Path! = root.absolute();
        _rootPath = *absRoot;
        delete absRoot;
    }

    setFileSystem(fs: FileSystem&) {
        _fs = &fs;
    }

    override const scheme() : String {
        return String("file");
    }

    override const separator() : String {
        return String("/");
    }

    const rootPath() : const Path& {
        return _rootPath;
    }

    override const exists(path: const Path&) : bool {
        realP : Path! = _toRealPath(path);
        if (realP == null) return false;
        res : bool = realP->exists();
        delete realP;
        return res;
    }

    override const entryType(path: const Path&) : FileSystemEntryType {
        realP : Path! = _toRealPath(path);
        if (realP == null) return FileSystemEntryType::NotFound;
        t : FileSystemEntryType = realP->entryType();
        delete realP;
        return t;
    }

    override const size(path: const Path&) : long {
        realP : Path! = _toRealPath(path);
        if (realP == null) return -1L;
        s : long = realP->size();
        delete realP;
        return s;
    }

    override createDirectory(path: const Path&) : bool {
        realP : Path! = _toRealPath(path);
        if (realP == null) return false;
        res : bool = realP->createDirectory();
        delete realP;
        return res;
    }

    override createDirectories(path: const Path&) : bool {
        realP : Path! = _toRealPath(path);
        if (realP == null) return false;
        res : bool = FileSystem::local().createDirectories(*realP);
        delete realP;
        return res;
    }

    override remove(path: const Path&) : bool {
        realP : Path! = _toRealPath(path);
        if (realP == null) return false;
        res : bool = realP->remove();
        delete realP;
        return res;
    }

    override rename(src: const Path&, dst: const Path&) : bool {
        rSrc : Path! = _toRealPath(src);
        rDst : Path! = _toRealPath(dst);
        if (rSrc == null || rDst == null) {
            if (rSrc != null) delete rSrc;
            if (rDst != null) delete rDst;
            return false;
        }
        res : bool = FileSystem::local().rename(*rSrc, *rDst);
        delete rSrc;
        delete rDst;
        return res;
    }

    override copyFile(src: const Path&, dst: const Path&, overwrite: bool) : bool {
        rSrc : Path! = _toRealPath(src);
        rDst : Path! = _toRealPath(dst);
        if (rSrc == null || rDst == null) {
            if (rSrc != null) delete rSrc;
            if (rDst != null) delete rDst;
            return false;
        }
        res : bool = FileSystem::local().copy(*rSrc, *rDst, overwrite);
        delete rSrc;
        delete rDst;
        return res;
    }

    override move(src: const Path&, dst: const Path&) : bool {
        rSrc : Path! = _toRealPath(src);
        rDst : Path! = _toRealPath(dst);
        if (rSrc == null || rDst == null) {
            if (rSrc != null) delete rSrc;
            if (rDst != null) delete rDst;
            return false;
        }
        res : bool = FileSystem::local().move(*rSrc, *rDst);
        delete rSrc;
        delete rDst;
        return res;
    }

    override readSymbolicLink(path: const Path&) : Path! {
        realP : Path! = _toRealPath(path);
        if (realP == null) return null;
        rawTarget : Path! = FileSystem::local().resolveSymlink(*realP);
        delete realP;
        if (rawTarget == null) return null;
        if (rawTarget->isAbsolute()) {
            vTarget : Path! = _toVirtualPath(*rawTarget);
            delete rawTarget;
            return vTarget;
        }
        return rawTarget;
    }

    override createSymbolicLink(link: const Path&, target: const Path&) : bool {
        rLink : Path! = _toRealPath(link);
        if (rLink == null) return false;
        rTarget : Path!;
        if (target.isAbsolute()) {
            rTarget = _toRealPath(target);
        } else {
            rTarget = new Path(target);
        }
        if (rTarget == null) {
            delete rLink;
            return false;
        }
        res : bool = FileSystem::local().provider().createSymbolicLink(*rLink, *rTarget);
        delete rLink;
        delete rTarget;
        return res;
    }

    override readDirectory(path: const Path&) : Vector<Path>! {
        realP : Path! = _toRealPath(path);
        if (realP == null) return null;
        rawChildren : Vector<Path>! = FileSystem::local().provider().readDirectory(*realP);
        delete realP;
        if (rawChildren == null) return null;
        res : Vector<Path>! = new Vector<Path>();
        n : unsigned int = rawChildren->size();
        i : unsigned int = 0u;
        while (i < n) {
            childReal : const Path& = rawChildren->get(i);
            fName : String = childReal.fileName();
            vChild : Path! = path.resolve(fName);
            if (_fs != null) {
                att : Path! = vChild->attachedTo(*_fs);
                res->append(*att);
                delete att;
            } else {
                res->append(*vChild);
            }
            delete vChild;
            ++i;
        }
        delete rawChildren;
        return res;
    }

    override openNativeFile(path: const Path&, mode: const char*) : CFile* {
        realP : Path! = _toRealPath(path);
        if (realP == null) return null;
        fp : CFile* = FileSystem::local().provider().openNativeFile(*realP, mode);
        delete realP;
        return fp;
    }
}

/**
 * DirectoryFileSystem — file system representing a sub-directory of the local file system.
 */
public class DirectoryFileSystem : public FileSystem {
private:
    _provider : DirectoryFileSystemProvider;

public:
    DirectoryFileSystem(rootPath: const Path&) : _provider(rootPath) {
        _provider.setFileSystem(this);
    }

    override provider() : FileSystemProvider& {
        return _provider;
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
        att : Path! = r.attachedTo(this);
        roots->append(*att);
        delete att;
        return roots;
    }

    static of(root: const Path&) : DirectoryFileSystem! {
        return new DirectoryFileSystem(root);
    }
}

} // namespace io::file
