/*
 * K Language compiler
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
/**
 * [instantiation-diamond-static] End-to-end STATIC-LINK diamond of a libk
 * template instantiation.
 *
 *                 libk: Optional<T> / Expected<R,E>
 *                /                              \
 *   liba (Optional<int>)                 libb (Optional<int>)   <- static .a
 *                \                              /
 *                  mainc (Optional<int>)  <- links both .a archives
 *
 * Two libraries A and B are compiled to STATIC archives (.a); each instantiates
 * the same libk template (k is auto-imported). An executable C links against
 * both archives and also instantiates the template. Because every consumer of a
 * libk template re-emits that template's RTTI / vtable / reflection descriptors,
 * a static link pulls multiple copies into one image. The compiler now emits
 * those template-related symbols with merge-friendly linkage:
 *   - vtable / RTTI globals (_KTV / _KTRI)      -> linkonce_odr + COMDAT;
 *   - reflection function descriptors (_KTRF)   -> module-local (private).
 * so the linker resolves them to a single definition instead of failing with
 * "multiple definition" errors.
 *
 * This is the static-archive counterpart of the shared-library diamond covered
 * by [import][e2e][instantiation-diamond-shared] in test-import.cpp.
 */
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include "helpers.hpp"
namespace fs = std::filesystem;
namespace {
// Write @p contents into <dir>/<name>.
void write_file(const fs::path& dir, const std::string& name, const std::string& contents) {
    std::ofstream ofs(dir / name);
    ofs << contents;
}
// Compile @p src into a static library <dir>/lib<module>.a (+ lib<module>.kdi),
// then symlink <module>.kdi -> lib<module>.kdi so the import resolver finds it
// by module name.  Returns the archive path.
//
// @p extra_args are inserted before the source path (e.g. {"-I", dir} so a
// dependent library can resolve a previously-built library's KDI).
fs::path build_static_lib(const fs::path& klangc, const fs::path& dir,
                          const std::string& module, const std::string& src,
                          const std::vector<std::string>& extra_args = {}) {
    write_file(dir, module + ".k", src);
    fs::path archive = dir / ("lib" + module + ".a");
    std::vector<std::string> args = {"--static-lib", "-o", archive.string()};
    for (const auto& a : extra_args) args.push_back(a);
    args.push_back((dir / (module + ".k")).string());
    auto res = k::tools::run_process(klangc.string(), args);
    INFO("klangc (" << module << ") stdout: " << res.out);
    INFO("klangc (" << module << ") stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(fs::exists(archive));
    fs::path kdi = dir / ("lib" + module + ".kdi");
    REQUIRE(fs::exists(kdi));
    std::error_code ec;
    fs::remove(dir / (module + ".kdi"), ec);
    fs::create_symlink("lib" + module + ".kdi", dir / (module + ".kdi"), ec);
    REQUIRE(!ec);
    return archive;
}
} // namespace

TEST_CASE("klangc: static-link diamond of a libk template instantiation",
          "[klangc][instantiation-diamond-static]") {
    auto klangc = find_klangc();
    // Unique scratch directory for all artifacts.
    fs::path dir = fs::temp_directory_path() /
                   ("klang_static_diamond_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE(!ec);
    // Libraries A and B both instantiate ::k::Optional<int> (k auto-imported).
    build_static_lib(klangc, dir, "klangc_static_diamond_01", R"K(
        module klangc_static_diamond_01;
        makeA() : int {
            v : int = 40;
            z : int = 0;
            o : Optional<int>;
            o.set(v);
            return o.getOr(z);
        }
    )K");
    build_static_lib(klangc, dir, "klangc_static_diamond_02", R"K(
        module klangc_static_diamond_02;
        makeB() : int {
            v : int = 2;
            z : int = 0;
            o : Optional<int>;
            o.set(v);
            return o.getOr(z);
        }
    )K");
    // Executable C imports both and also instantiates ::k::Optional<int>.
    write_file(dir, "mainc.k", R"K(
        module klangc_static_diamond_03;
        import klangc_static_diamond_01;
        import klangc_static_diamond_02;
        main() : int {
            z : int = 0;
            o : Optional<int>;
            return makeA() + makeB() + o.getOr(z);
        }
    )K");
    // Link the executable against both STATIC archives (only .a exist in -L dir,
    // so the auto-added -lliba / -llibb resolve to the archives -> static link).
    fs::path exe = dir / "mainc";
    auto link = k::tools::run_process(
        klangc.string(),
        {"-o", exe.string(), (dir / "mainc.k").string(),
         "-I", dir.string(), "-L", dir.string()});
    INFO("link stdout: " << link.out);
    INFO("link stderr: " << link.err);
    // The key assertion: the static link must succeed (no "multiple definition"
    // on the re-emitted template RTTI / vtable / reflection descriptors).
    REQUIRE(link.exit_code == 0);
    REQUIRE(fs::exists(exe));
    // Run it: 40 + 2 + 0 = 42 (libk.so is needed at load time).
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto run = k::tools::run_process(exe.string(), {});
    INFO("run stdout: " << run.out);
    INFO("run stderr: " << run.err);
    REQUIRE(run.exit_code == 42);
    fs::remove_all(dir, ec);
}
// ---------------------------------------------------------------------------
// [instantiation-diamond-static][user-template] STATIC-LINK diamond of a
// USER-DECLARED (non-libk) struct template.
//
//                 boxlib: Box<T>            (user library -- NOT libk)
//                /                  \
//   boxa (Box<int>)            boxb (Box<int>)     <- static .a
//                \                  /
//                  boxmain (Box<int>)  <- links both .a archives
//
// Same scenario as the libk static diamond above, but the template lives in a
// plain user library. The COMDAT/linkonce_odr dedup is module-agnostic (it keys
// off the template's origin module + the is_instantiation()/is_template() flags,
// never on libk), so boxa, boxb and the executable each synthesise their own
// ::boxlib::Box<int> under the same origin-absolute name with merge-friendly
// linkage, and the static link resolves them to a single definition.
//
// Expected: 40 + 2 + 0 = 42.
// ---------------------------------------------------------------------------
TEST_CASE("klangc: static-link diamond of a user-declared template instantiation",
          "[klangc][instantiation-diamond-static][user-template]") {
    auto klangc = find_klangc();
    // Unique scratch directory for all artifacts.
    fs::path dir = fs::temp_directory_path() /
                   ("klang_static_diamond_user_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE(!ec);
    // The template-defining library (the template's true origin). It is never
    // instantiated here -- each consumer re-synthesises ::boxlib::Box<int> itself.
    build_static_lib(klangc, dir, "klangc_static_diamond_04", R"K(
        module klangc_static_diamond_04;
        template<typename T>
        struct Box {
            val : T;
        }
        boxlibTag() : int { return 0; }
    )K");
    // Libraries A and B both import boxlib and instantiate ::boxlib::Box<int>.
    // -I dir lets them resolve boxlib.kdi.
    build_static_lib(klangc, dir, "klangc_static_diamond_05", R"K(
        module klangc_static_diamond_05;
        import klangc_static_diamond_04;
        makeBoxA() : int {
            b : klangc_static_diamond_04::Box<int>;
            b.val = 40;
            return b.val;
        }
    )K", {"-I", dir.string()});
    build_static_lib(klangc, dir, "klangc_static_diamond_06", R"K(
        module klangc_static_diamond_06;
        import klangc_static_diamond_04;
        makeBoxB() : int {
            b : klangc_static_diamond_04::Box<int>;
            b.val = 2;
            return b.val;
        }
    )K", {"-I", dir.string()});
    // Executable imports boxlib + both A and B and also instantiates Box<int>.
    write_file(dir, "boxmain.k", R"K(
        module klangc_static_diamond_07;
        import klangc_static_diamond_04;
        import klangc_static_diamond_05;
        import klangc_static_diamond_06;
        main() : int {
            b : klangc_static_diamond_04::Box<int>;
            b.val = 0;
            return makeBoxA() + makeBoxB() + b.val;
        }
    )K");
    // Link the executable against the STATIC archives (only .a exist in -L dir,
    // so the auto-added -lboxa / -lboxb / -lboxlib resolve to the archives).
    fs::path exe = dir / "boxmain";
    auto link = k::tools::run_process(
        klangc.string(),
        {"-o", exe.string(), (dir / "boxmain.k").string(),
         "-I", dir.string(), "-L", dir.string()});
    INFO("link stdout: " << link.out);
    INFO("link stderr: " << link.err);
    // The key assertion: the static link must succeed (no "multiple definition"
    // on the re-emitted ::boxlib::Box<int> instantiation symbols).
    REQUIRE(link.exit_code == 0);
    REQUIRE(fs::exists(exe));
    // Run it: 40 + 2 + 0 = 42 (libk.so is needed at load time).
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto run = k::tools::run_process(exe.string(), {});
    INFO("run stdout: " << run.out);
    INFO("run stderr: " << run.err);
    REQUIRE(run.exit_code == 42);
    fs::remove_all(dir, ec);
}
