/*
 * K Language standard library — ::k::Module / Application::loadModule() tests
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
 * Tests for ::k::Application::loadModule() and ::k::Module.
 *
 * Each test JIT-compiles a small K program whose `main` calls `loadModule()`
 * and returns a distinct non-zero code for every failed check.
 *
 * Fixture libraries (built by CMake in LIBK_MODULE_FIXTURES_DIR, see
 * libk/libk/tests/modules):
 *  - libmodreg.so       K library owning a global plugin registry;
 *  - libmodplugin_a.so  K library, depends on modreg; its global object registers itself;
 *  - libmodplugin_b.so  same as plugin_a, with another plugin id;
 *  - libmodmissing_user.so  C library whose DT_NEEDED dependency has been deleted;
 *  - libmodundef.so     C library with an unresolved symbol;
 *  - not_a_library.txt  a plain text file.
 *
 * The dynamic loader state is process-wide, so tests that observe the plugin
 * registry live in a single test case.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_MODULE_FIXTURES_DIR
#error "LIBK_MODULE_FIXTURES_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

namespace fs = std::filesystem;

const std::string FIXTURES = LIBK_MODULE_FIXTURES_DIR;

// Values of ::k::LoadModuleError.
constexpr int NOT_FOUND          = 0;
constexpr int ACCESS_DENIED      = 1;
constexpr int INVALID_PATH       = 2;
constexpr int NOT_A_LIBRARY      = 3;
constexpr int MISSING_DEPENDENCY = 4;
constexpr int UNRESOLVED_SYMBOL  = 5;
constexpr int INVALID_MANIFEST = 7;
constexpr int INCOMPATIBLE_MANIFEST = 8;

/// Compile a K program that may import the fixture `modreg` library, then JIT it.
/// libmodreg.so is loaded into the process (as the test executable itself would
/// do through a DT_NEEDED) so that the JIT can resolve its symbols.
std::unique_ptr<k::model::gen::jit> jit_module(std::string_view src) {
    static bool reg_loaded = false;
    if (!ensure_libk_loaded()) return nullptr;
    if (!reg_loaded) {
        std::string reg = FIXTURES + "/libmodreg.so";
        if (!dlopen(reg.c_str(), RTLD_NOW | RTLD_GLOBAL)) {
            FAIL("Cannot load " << reg << ": " << dlerror());
        }
        reg_loaded = true;
    }

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    resolver->add_search_dir(FIXTURES);
    comp->set_file_resolver(resolver);
    try {
        comp->parse_source("", src, true, false);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (std::exception& ex) {
        std::cerr << "Unexpected error: " << ex.what() << std::endl;
        return nullptr;
    }
}

int run_main(k::model::gen::jit& jit) {
    static const char* argv[] = {"prog", nullptr};
    auto main = jit.lookup_main_entry_symbol<int (*)(int, char**)>();
    REQUIRE(main != nullptr);
    return main(1, const_cast<char**>(argv));
}

/// Run a K program that loads `path` and returns 99 on success or the error code.
int load_error_of(const std::string& path) {
    auto jit = jit_module(
        "module __module_error__;\n"
        "main() : int {\n"
        "    r : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + path + "\"));\n"
        "    if (r.hasResult()) { return 99; }\n"
        "    return (int) r.getError();\n"
        "}\n");
    REQUIRE(jit);
    return run_main(*jit);
}

} // anonymous namespace

TEST_CASE("Application::applicationModule — registers the process image with shared ownership",
          "[libk][module]") {
    auto jit = jit_module(
        "module __module_application__;\n"
        "saved_app_module : Shared<ApplicationModule>;\n"
        "main() : int {\n"
        "    app_module : Shared<ApplicationModule> = applicationModule();\n"
        "    saved_app_module = app_module;\n"
        "    if (!app_module) { return 1; }\n"
        "    if (app_module.get()->kind() != ModuleKind::APPLICATION) { return 2; }\n"
        "    if (!app_module.get()->isProcessPinned()) { return 3; }\n"
        "    if (app_module.get()->residency() != ModuleResidency::PROCESS_PINNED) { return 14; }\n"
        "    owners_before_snapshot : int = app_module.useCount();\n"
        "    snapshot : Vector<Shared<Module>> = modules();\n"
        "    if (snapshot.size() < 1u) { return 4; }\n"
        "    if (snapshot.get(0) != Shared<Module>(app_module)) { return 5; }\n"
        "    if (app_module.useCount() <= owners_before_snapshot) { return 9; }\n"
        "    owners_with_snapshot : int = app_module.useCount();\n"
        "    snapshot.clear();\n"
        "    if (modules().size() < 1u) { return 6; }\n"
        "    if (app_module.useCount() >= owners_with_snapshot) { return 10; }\n"
        "    if (linkedModules().size() < 1u) { return 12; }\n"
        "    if (linkedUnits().size() < 1u) { return 13; }\n"
        "    if (app_module.get()->linkedModules().size() != linkedModules().size()) { return 15; }\n"
        "    if (app_module.get()->linkedUnits().size() != linkedUnits().size()) { return 16; }\n"
        "    if (!lookupModule(app_module.get()->path())) { return 7; }\n"
        "    if (lookupModule(String(\"" + FIXTURES + "/not_loaded.so\"))) { return 8; }\n"
        "    return 0;\n"
        "}\n"
        "check_saved_startup_snapshot() : int {\n"
        "    if (!saved_app_module) { return 1; }\n"
        "    if (saved_app_module.get()->linkedModules().size() < 1u) { return 2; }\n"
        "    if (saved_app_module.get()->linkedUnits().size() < 1u) { return 3; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
    auto check_snapshot = jit->lookup_symbol<int(*)()>("check_saved_startup_snapshot");
    REQUIRE(check_snapshot);
    CHECK(check_snapshot() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Plugin chain: dependencies loaded, global objects constructed and registered
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application::loadModule — plugins register themselves in their dependency",
          "[libk][module]") {
    // A symbolic link and a dotted path both designate plugin_a.
    const fs::path tmp = fs::temp_directory_path() / ("klang-module-test-" + std::to_string(getpid()));
    fs::create_directories(tmp);
    fs::create_symlink(fs::path(FIXTURES) / "libmodplugin_a.so", tmp / "link_to_a.so");

    auto jit = jit_module(
        "module __module_chain__;\n"
        "import modreg;\n"
        "main() : int {\n"
        "    if (modreg::registeredCount() != 0) { return 1; }\n"
        "\n"
        "    ra : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + FIXTURES + "/libmodplugin_a.so\"));\n"
        "    if (!ra.hasResult()) { return 2; }\n"
        "    ma : Shared<Module> = ra.getResult();\n"
        "    if (modreg::registeredCount() != 1) { return 3; }\n"
        "    if (modreg::registeredId(0) != 1) { return 4; }\n"
        "\n"
        "    // Same file through other paths: same module, no new registration.\n"
        "    owners_before_alias : int = ma.useCount();\n"
        "    rl : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + (tmp / "link_to_a.so").string() + "\"));\n"
        "    if (!rl.hasResult()) { return 5; }\n"
        "    if (rl.getResult() != ma) { return 6; }\n"
        "    if (ma.useCount() != owners_before_alias + 1) { return 19; }\n"
        "    rd : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + FIXTURES + "/./libmodplugin_a.so\"));\n"
        "    if (!rd.hasResult()) { return 7; }\n"
        "    if (rd.getResult() != ma) { return 8; }\n"
        "    owners_before_lookup : int = ma.useCount();\n"
        "    lookup : Shared<Module> = lookupModule(String(\"" + FIXTURES + "/libmodplugin_a.so\"));\n"
        "    if (lookup != ma) { return 16; }\n"
        "    if (ma.useCount() != owners_before_lookup + 1) { return 20; }\n"
        "    if (modreg::registeredCount() != 1) { return 9; }\n"
        "\n"
        "    // Second plugin: chained after the first one.\n"
        "    rb : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + FIXTURES + "/libmodplugin_b.so\"));\n"
        "    if (!rb.hasResult()) { return 10; }\n"
        "    if (rb.getResult() == ma) { return 11; }\n"
        "    if (modreg::registeredCount() != 2) { return 12; }\n"
        "    if (modreg::registeredId(0) != 1) { return 13; }\n"
        "    if (modreg::registeredId(1) != 2) { return 14; }\n"
        "\n"
        "    // The module reports the absolute real path of the library.\n"
        "    if (ma.get()->path() != String(\"" + fs::canonical(fs::path(FIXTURES) / "libmodplugin_a.so").string() + "\")) { return 15; }\n"
        "    if (ma.get()->isProcessPinned()) { return 21; }\n"
        "    if (ma.get()->residency() != ModuleResidency::POTENTIALLY_RELEASABLE) { return 22; }\n"
        "    snapshot : Vector<Shared<Module>> = modules();\n"
        "    if (snapshot.size() < 3u) { return 17; }\n"
        "    snapshot.clear();\n"
        "    if (!lookupModule(String(\"" + FIXTURES + "/libmodplugin_b.so\"))) { return 18; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);

    fs::remove_all(tmp);
}

// ═════════════════════════════════════════════════════════════════════════════
// Libraries already loaded implicitly by the process
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application::loadModule — implicitly loaded library gets a dedicated Module",
          "[libk][module]") {
    const std::string libk = fs::canonical(fs::path(LIBK_LIB_DIR) / "libk.so").string();
    const std::string reg  = fs::canonical(fs::path(FIXTURES) / "libmodreg.so").string();

    auto jit = jit_module(
        "module __module_implicit__;\n"
        "main() : int {\n"
        "    r1 : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + libk + "\"));\n"
        "    if (!r1.hasResult()) { return 1; }\n"
        "    m1 : Shared<Module> = r1.getResult();\n"
        "    if (m1.get()->path() != String(\"" + libk + "\")) { return 2; }\n"
        "    if (m1.get()->kind() != ModuleKind::LIBRARY) { return 7; }\n"
        "    if (!m1.get()->isProcessPinned()) { return 8; }\n"
        "    units : Vector<UnitView> = m1.get()->units();\n"
        "    if (units.size() < 1u) { return 21; }\n"
        "    unit : UnitView = units.get(0);\n"
        "    if (unit.owner() != m1) { return 22; }\n"
        "    if (unit.getDescriptor().getRootNamespace() == null) { return 23; }\n"
        "    r2 : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + libk + "\"));\n"
        "    if (!r2.hasResult()) { return 3; }\n"
        "    if (r2.getResult() != m1) { return 4; }\n"
        "\n"
        "    r3 : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + reg + "\"));\n"
        "    if (!r3.hasResult()) { return 5; }\n"
        "    if (r3.getResult() == m1) { return 6; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("Application::loadModule — dependency manifest is not attributed to native image",
          "[libk][module][manifest]") {
    auto jit = jit_module(
        "module __module_native_dependency__;\n"
        "main() : int {\n"
        "    r : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" +
        FIXTURES + "/libmodnative_dep.so\"));\n"
        "    if (!r.hasResult()) { return 1; }\n"
        "    native : Shared<Module> = r.getResult();\n"
        "    if (native.get()->kind() != ModuleKind::NATIVE_LIBRARY) { return 2; }\n"
        "    if (native.get()->units().size() != 0u) { return 3; }\n"
        "    registry : Shared<Module> = lookupModule(String(\"" + FIXTURES +
        "/libmodreg.so\"));\n"
        "    if (!registry || registry.get()->kind() != ModuleKind::LIBRARY) { return 4; }\n"
        "    if (registry.get()->units().size() == 0u) { return 5; }\n"
        "    direct : Vector<Shared<Module>> = native.get()->dependencies();\n"
        "    if (direct.size() != 1u) { return 6; }\n"
        "    if (direct.get(0) != registry) { return 7; }\n"
        "    if (direct.get(0).get()->path() != registry.get()->path()) { return 8; }\n"
        "    if (native.get()->isProcessPinned()) { return 9; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("Module views retain namespace owners and reject empty descriptor access",
          "[libk][module][rtti]") {
    const std::string reg = fs::canonical(fs::path(FIXTURES) / "libmodreg.so").string();
    auto jit = jit_module(
        "module __module_namespace_view__;\n"
        "main() : int {\n"
        "    module_handle : Shared<Module> = loadModule(String(\"" + reg + "\")).getResult();\n"
        "    units : Vector<UnitView> = module_handle.get()->units();\n"
        "    if (units.size() == 0u) { return 1; }\n"
        "    unit : UnitView = units.get(0);\n"
        "    if (unit.isNull()) { return 2; }\n"
        "    root : NamespaceView = unit.rootNamespace();\n"
        "    children : Vector<NamespaceView> = root.children();\n"
        "    if (children.size() == 0u) { return 3; }\n"
        "    nested : Vector<NamespaceView> = children.get(0).children();\n"
        "    if (nested.size() == 0u) { return 4; }\n"
        "    leaf : NamespaceView = nested.get(0);\n"
        "    if (leaf.isNull()) { return 5; }\n"
        "    unit = UnitView();\n"
        "    root = NamespaceView();\n"
        "    children.clear();\n"
        "    nested.clear();\n"
        "    module_handle = Shared<Module>();\n"
        "    if (!leaf.owner()) { return 6; }\n"
        "    if (leaf.getDescriptor().getFullName() == null) { return 7; }\n"
        "    empty_unit : UnitView;\n"
        "    if (!empty_unit.isNull()) { return 8; }\n"
        "    try { empty_unit.getDescriptor(); return 9; }\n"
        "    catch (e: NullDereferenceError&) {}\n"
        "    empty_namespace : NamespaceView;\n"
        "    if (!empty_namespace.isNull()) { return 10; }\n"
        "    try { empty_namespace.getDescriptor(); return 11; }\n"
        "    catch (e: NullDereferenceError&) {}\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Errors
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application::loadModule — missing file", "[libk][module][error]") {
    CHECK(load_error_of(FIXTURES + "/does_not_exist.so") == NOT_FOUND);
    CHECK(load_error_of("") == NOT_FOUND);
}

TEST_CASE("Application::loadModule — invalid path", "[libk][module][error]") {
    CHECK(load_error_of("/" + std::string(5000, 'a')) == INVALID_PATH);
}

TEST_CASE("Application::loadModule — not a shared library", "[libk][module][error]") {
    CHECK(load_error_of(FIXTURES + "/not_a_library.txt") == NOT_A_LIBRARY);
    CHECK(load_error_of(FIXTURES) == NOT_A_LIBRARY); // a directory
}

TEST_CASE("Application::loadModule — missing dependency", "[libk][module][error]") {
    CHECK(load_error_of(FIXTURES + "/libmodmissing_user.so") == MISSING_DEPENDENCY);
}

TEST_CASE("Application::loadModule — unresolved symbol", "[libk][module][error]") {
    CHECK(load_error_of(FIXTURES + "/libmodundef.so") == UNRESOLVED_SYMBOL);
}

TEST_CASE("Application::loadModule — malformed manifest", "[libk][module][manifest][error]") {
    CHECK(load_error_of(FIXTURES + "/libmodmalformed_manifest.so") == INVALID_MANIFEST);
}

TEST_CASE("Application::loadModule — malformed dependency manifest",
          "[libk][module][manifest][error]") {
    CHECK(load_error_of(FIXTURES + "/libmodmalformed_dependency.so") == INVALID_MANIFEST);
}

TEST_CASE("Application::loadModule — incompatible manifest", "[libk][module][manifest][error]") {
    CHECK(load_error_of(FIXTURES + "/libmodincompatible_manifest.so") == INCOMPATIBLE_MANIFEST);
}

TEST_CASE("Application::loadModule — access denied", "[libk][module][error]") {
    if (geteuid() == 0) {
        SKIP("permissions are not enforced for root");
    }
    const fs::path tmp = fs::temp_directory_path() / ("klang-module-denied-" + std::to_string(getpid()));
    fs::create_directories(tmp);
    const fs::path lib = tmp / "libdenied.so";
    fs::copy_file(fs::path(FIXTURES) / "libmodplugin_a.so", lib);
    chmod(lib.c_str(), 0);

    CHECK(load_error_of(lib.string()) == ACCESS_DENIED);

    chmod(lib.c_str(), 0600);
    fs::remove_all(tmp);
}

// ═════════════════════════════════════════════════════════════════════════════
// Concurrency
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application::loadModule — concurrent loads of one file share one Module",
          "[libk][module][thread]") {
    auto jit = jit_module(
        "module __module_threads__;\n"
        "\n"
        "class LoadTask : public Runnable {\n"
        "    private _app : Application*;\n"
        "    public _module : Shared<Module>;\n"
        "\n"
        "    LoadTask(app : Application*) {\n"
        "        _app = app;\n"
        "        _module = null;\n"
        "    }\n"
        "\n"
        "    override run() {\n"
        "        r : Expected<Shared<Module>, LoadModuleError> = _app->loadModule(String(\"" + FIXTURES + "/libmodplugin_b.so\"));\n"
        "        if (r.hasResult()) {\n"
        "            _module = r.getResult();\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "main() : int {\n"
        "    app : Application* = &this;\n"
        "    t0 : LoadTask(app);\n"
        "    t1 : LoadTask(app);\n"
        "    t2 : LoadTask(app);\n"
        "    t3 : LoadTask(app);\n"
        "    r0 : Runnable* = &t0;\n"
        "    r1 : Runnable* = &t1;\n"
        "    r2 : Runnable* = &t2;\n"
        "    r3 : Runnable* = &t3;\n"
        "    th0 : Thread! = new Thread(r0);\n"
        "    th1 : Thread! = new Thread(r1);\n"
        "    th2 : Thread! = new Thread(r2);\n"
        "    th3 : Thread! = new Thread(r3);\n"
        "    th0.start();\n"
        "    th1.start();\n"
        "    th2.start();\n"
        "    th3.start();\n"
        "    th0.join();\n"
        "    th1.join();\n"
        "    th2.join();\n"
        "    th3.join();\n"
        "    if (!t0._module) { return 1; }\n"
        "    if (t1._module != t0._module) { return 2; }\n"
        "    if (t2._module != t0._module) { return 3; }\n"
        "    if (t3._module != t0._module) { return 4; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("Application startup rejects malformed manifests in already-loaded images",
          "[libk][module][manifest][startup]") {
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const std::string malformed = FIXTURES + "/libmodmalformed_manifest.so";
        if (!dlopen(malformed.c_str(), RTLD_NOW | RTLD_GLOBAL)) _exit(2);
        auto jit = jit_module(
            "module __module_startup_manifest_error__;\n"
            "main() : int { return 0; }\n");
        if (!jit) _exit(4);
        _exit(run_main(*jit) != 0 ? 0 : 3);
    }

    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}
