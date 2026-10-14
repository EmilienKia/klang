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
 *  - libvisitor_fixture.so  K RTTI traversal fixture with declared root function probes;
 *  - libmodmissing_user.so  C library whose DT_NEEDED dependency has been deleted;
 *  - libmodundef.so     C library with an unresolved symbol;
 *  - not_a_library.txt  a plain text file.
 *
 * The dynamic loader state is process-wide, so tests that observe the plugin
 * registry live in a single test case.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include <kdi.hpp>
#include <functional>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <vector>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_MODULE_FIXTURES_DIR
#error "LIBK_MODULE_FIXTURES_DIR must be defined — check CMakeLists.txt"
#endif

extern "C" int klang_module_symbol_application_fixture(void) {
    return 17;
}

#if defined(__ELF__)
asm(".globl klang_module_symbol_zero_fixture\n"
    ".type klang_module_symbol_zero_fixture, @object\n"
    ".set klang_module_symbol_zero_fixture, 0\n");
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
        "    // Earlier cases can leave these plugin images resident and registered.\n"
        "    expectedCount : int = modreg::registeredCount();\n"
        "    aIndex : int = -1;\n"
        "    bIndex : int = -1;\n"
        "    i : int = 0;\n"
        "    while (i < expectedCount) {\n"
        "        id : int = modreg::registeredId(i);\n"
        "        if (id == 1) {\n"
        "            if (aIndex >= 0) { return 1; }\n"
        "            aIndex = i;\n"
        "        }\n"
        "        if (id == 2) {\n"
        "            if (bIndex >= 0) { return 1; }\n"
        "            bIndex = i;\n"
        "        }\n"
        "        ++i;\n"
        "    }\n"
        "    aWasResident : bool = aIndex >= 0;\n"
        "\n"
        "    ra : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + FIXTURES + "/libmodplugin_a.so\"));\n"
        "    if (!ra.hasResult()) { return 2; }\n"
        "    ma : Shared<Module> = ra.getResult();\n"
        "    if (aIndex < 0) { aIndex = expectedCount; ++expectedCount; }\n"
        "    if (modreg::registeredCount() != expectedCount) { return 3; }\n"
        "    if (modreg::registeredId(aIndex) != 1) { return 4; }\n"
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
        "    if (modreg::registeredCount() != expectedCount) { return 9; }\n"
        "\n"
        "    // A new second plugin appends once; a resident one does not register again.\n"
        "    rb : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" + FIXTURES + "/libmodplugin_b.so\"));\n"
        "    if (!rb.hasResult()) { return 10; }\n"
        "    if (rb.getResult() == ma) { return 11; }\n"
        "    if (bIndex < 0) { bIndex = expectedCount; ++expectedCount; }\n"
        "    if (modreg::registeredCount() != expectedCount) { return 12; }\n"
        "    if (modreg::registeredId(aIndex) != 1) { return 13; }\n"
        "    if (modreg::registeredId(bIndex) != 2) { return 14; }\n"
        "\n"
        "    // The module reports the absolute real path of the library.\n"
        "    if (ma.get()->path() != String(\"" + fs::canonical(fs::path(FIXTURES) / "libmodplugin_a.so").string() + "\")) { return 15; }\n"
        "    if (ma.get()->isProcessPinned() != aWasResident) { return 21; }\n"
        "    if (aWasResident) {\n"
        "        if (ma.get()->residency() != ModuleResidency::PROCESS_PINNED) { return 22; }\n"
        "    } else {\n"
        "        if (ma.get()->residency() != ModuleResidency::POTENTIALLY_RELEASABLE) { return 22; }\n"
        "    }\n"
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

TEST_CASE("Application singleton — dynamically loaded module constructors share the active instance",
          "[libk][module][application]") {
    struct ObservationGate {
        int descriptors[2] = {-1, -1};
        ~ObservationGate() {
            for (int descriptor : descriptors) {
                if (descriptor >= 0) close(descriptor);
            }
        }
    } gate;
    REQUIRE(pipe(gate.descriptors) == 0);

    auto jit = jit_module(
        "module __module_application_anchor__;\n"
        "import modreg;\n"
        "@ffi::Extern(\"C\") private read(fd : int, output : byte*, size : unsigned long) : long;\n"
        "class Application : public k::Application {\n"
        "    private _capture : k::io::ArrayOutputStream<byte>*;\n"
        "    public Application() {\n"
        "        stdoutRoot : k::io::ArrayOutputStream<byte>! =\n"
        "            new k::io::ArrayOutputStream<byte>();\n"
        "        _capture = &*stdoutRoot;\n"
        "        setStandardOutput(stdoutRoot);\n"
        "    }\n"
        "    public main() : int {\n"
        "        observed : byte = 0;\n"
        "        if (read(" + std::to_string(gate.descriptors[0]) +
            ", &observed, (unsigned long) 1) != (long) 1 || observed != (byte) 1) { return 5; }\n"
        "        before : int = modreg::registeredCount();\n"
        "        loaded : Expected<Shared<Module>, LoadModuleError> = loadModule(String(\"" +
            FIXTURES + "/libapplication_probe.so\"));\n"
        "        if (!loaded.hasResult()) { return 1; }\n"
        "        if (modreg::registeredCount() != before + 1) { return 2; }\n"
        "        if (modreg::registeredId(before) != 4) { return 3; }\n"
        "        output : byte[]! = _capture->toArray();\n"
        "        valid : bool = output.size == 1u && output[0] == (byte) 'P';\n"
        "        delete output;\n"
        "        if (!valid) { return 4; }\n"
        "        return 0;\n"
        "    }\n"
        "}\n");
    REQUIRE(jit);

    using CurrentApplication = void* (*)();
    auto current = reinterpret_cast<CurrentApplication>(
        dlsym(RTLD_DEFAULT, "__k_application_current"));
    REQUIRE(current);
    std::atomic<bool> running = true;
    std::atomic<bool> saw_active_application = false;
    std::thread reader([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (running.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < deadline) {
            if (current()) {
                saw_active_application.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
        }
        // Keep the Application active until the cross-thread observation completes.
        const unsigned char observed =
            saw_active_application.load(std::memory_order_acquire) ? 1 : 0;
        ssize_t written;
        do {
            written = write(gate.descriptors[1], &observed, sizeof(observed));
        } while (written < 0 && errno == EINTR);
        close(gate.descriptors[1]);
        gate.descriptors[1] = -1;
    });
    const int status = run_main(*jit);
    running.store(false, std::memory_order_release);
    reader.join();
    CHECK(status == 0);
    CHECK(saw_active_application.load(std::memory_order_acquire));
}

TEST_CASE("Application singleton — statically copied runtime bridge resolves the shared provider",
          "[libk][module][application]") {
    REQUIRE(ensure_libk_loaded());

    using ApplicationOperation = int (*)(void*);
    using HostOperation = int (*)();
    using HostLeaveOperation = void (*)();
    auto host_enter = reinterpret_cast<HostOperation>(
        dlsym(RTLD_DEFAULT, "__k_application_jit_host_enter"));
    auto host_leave = reinterpret_cast<HostLeaveOperation>(
        dlsym(RTLD_DEFAULT, "__k_application_jit_host_leave"));
    auto activate = reinterpret_cast<ApplicationOperation>(
        dlsym(RTLD_DEFAULT, "__k_application_activate"));
    auto deactivate = reinterpret_cast<ApplicationOperation>(
        dlsym(RTLD_DEFAULT, "__k_application_deactivate"));
    auto current = reinterpret_cast<void* (*)()>(
        dlsym(RTLD_DEFAULT, "__k_application_current"));
    REQUIRE(host_enter);
    REQUIRE(host_leave);
    REQUIRE(activate);
    REQUIRE(deactivate);
    REQUIRE(current);
    CHECK(current() == nullptr);

    REQUIRE(host_enter() == 0);
    CHECK(current() == nullptr);
    int active_application = 0;
    REQUIRE(activate(&active_application) == 0);

    const std::string path = FIXTURES + "/libapplication_static_copy_probe.so";
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    REQUIRE(handle);
    using ConstructorProbe = int (*)();
    using ApplicationProbe = int (*)(void*);
    auto constructor_probe = reinterpret_cast<ConstructorProbe>(
        dlsym(handle, "application_static_copy_constructor_saw_active"));
    auto application_probe = reinterpret_cast<ApplicationProbe>(
        dlsym(handle, "application_static_copy_observes"));
    REQUIRE(constructor_probe);
    REQUIRE(application_probe);
    CHECK(constructor_probe() == 1);
    CHECK(application_probe(&active_application) == 1);
    REQUIRE(deactivate(&active_application) == 0);
    CHECK(application_probe(&active_application) == 0);
    dlclose(handle);
    host_leave();
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

TEST_CASE("Module::lookupSymbol retains native library owners and searches dependencies",
          "[libk][module][ffi][symbol]") {
    std::string source = R"SRC(
        module __module_native_symbols__;
        saved : ffi::NativeSymbol;
        saved_dependency : ffi::NativeSymbol;
        main() : int {
            loaded : Expected<Shared<Module>, LoadModuleError> =
                loadModule(String("@NATIVE_PATH@"));
            if (!loaded.hasResult()) { return 1; }
            image : Shared<Module> = loaded.getResult();
            if (image.get()->kind() != ModuleKind::NATIVE_LIBRARY) { return 2; }
            baseline : int = image.useCount();
            {
                found : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                    image.get()->lookupSymbol(String("klang_native_dependency_fixture"));
                if (!found.hasResult() || found.hasError()) { return 3; }
                symbol : ffi::NativeSymbol = found.getResult();
                if (symbol.isEmpty() || !symbol.hasAddress()) { return 4; }
                if (symbol.rawAddress() == null || symbol.owner() != image) { return 5; }
                copy : ffi::NativeSymbol(symbol);
                saved = copy;
                if (image.useCount() <= baseline) { return 6; }
            }
            if (image.useCount() != baseline + 1) { return 7; }
            registry : Shared<Module> = lookupModule(String("@REGISTRY_PATH@"));
            if (!registry || registry == image) { return 8; }
            dependency : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                image.get()->lookupSymbol(String("__k_rtti_manifest_v1"));
            provider : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                registry.get()->lookupSymbol(String("__k_rtti_manifest_v1"));
            if (!dependency.hasResult() || !provider.hasResult()) { return 9; }
            saved_dependency = dependency.getResult();
            if (saved_dependency.rawAddress() != provider.getResult().rawAddress()) { return 10; }
            if (saved_dependency.owner() != image) { return 11; }
            empty : ffi::NativeSymbol;
            if (!empty.isEmpty() || empty.hasAddress() || empty.rawAddress() != null) { return 12; }
            if (empty.owner()) { return 13; }
            return 0;
        }
        saved_address() : byte* { return saved.rawAddress(); }
        check_saved() : int {
            if (saved.isEmpty() || saved_dependency.isEmpty()) { return 1; }
            if (!saved.hasAddress() || !saved_dependency.hasAddress()) { return 2; }
            if (!saved.owner() || saved.owner() != saved_dependency.owner()) { return 3; }
            if (saved.owner().get()->kind() != ModuleKind::NATIVE_LIBRARY) { return 4; }
            return 0;
        }
    )SRC";
    const std::string native = fs::canonical(fs::path(FIXTURES) / "libmodnative_dep.so").string();
    const std::string registry = fs::canonical(fs::path(FIXTURES) / "libmodreg.so").string();
    source.replace(source.find("@NATIVE_PATH@"), std::string("@NATIVE_PATH@").size(), native);
    source.replace(source.find("@REGISTRY_PATH@"), std::string("@REGISTRY_PATH@").size(), registry);
    auto jit = jit_module(source);
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);

    auto check_saved = jit->lookup_symbol<int(*)()>("check_saved");
    REQUIRE(check_saved);
    CHECK(check_saved() == 0);
    auto saved_address = jit->lookup_symbol<void*(*)()>("saved_address");
    REQUIRE(saved_address);
    void* handle = dlopen(native.c_str(), RTLD_NOW | RTLD_NOLOAD);
    REQUIRE(handle);
    dlerror();
    void* expected = dlsym(handle, "klang_native_dependency_fixture");
    REQUIRE(dlerror() == nullptr);
    CHECK(saved_address() == expected);
    dlclose(handle);
}

TEST_CASE("Module::lookupSymbol resolves application symbols and accepts a zero address",
          "[libk][module][ffi][symbol]") {
    auto jit = jit_module(R"SRC(
        module __module_application_symbols__;
        saved : ffi::NativeSymbol;
        main() : int {
            app : Shared<ApplicationModule> = applicationModule();
            found : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(String("klang_module_symbol_application_fixture"));
            if (!found.hasResult()) { return 1; }
            saved = found.getResult();
            if (saved.isEmpty() || !saved.hasAddress()) { return 2; }
            if (saved.owner() != Shared<Module>(app)) { return 3; }
            zero : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(String("klang_module_symbol_zero_fixture"));
            if (!zero.hasResult() || zero.hasError()) { return 4; }
            zero_symbol : ffi::NativeSymbol = zero.getResult();
            if (zero_symbol.isEmpty() || zero_symbol.hasAddress()) { return 5; }
            if (zero_symbol.rawAddress() != null) { return 6; }
            if (zero_symbol.owner() != Shared<Module>(app)) { return 7; }
            return 0;
        }
        saved_address() : byte* { return saved.rawAddress(); }
        check_saved() : int {
            if (saved.isEmpty() || !saved.owner()) { return 1; }
            if (saved.owner().get()->kind() != ModuleKind::APPLICATION) { return 2; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    REQUIRE(run_main(*jit) == 0);
    auto saved_address = jit->lookup_symbol<void*(*)()>("saved_address");
    REQUIRE(saved_address);
    CHECK(saved_address() == reinterpret_cast<void*>(&klang_module_symbol_application_fixture));
    auto check_saved = jit->lookup_symbol<int(*)()>("check_saved");
    REQUIRE(check_saved);
    CHECK(check_saved() == 0);
}

TEST_CASE("Module::lookupSymbol rejects invalid names and owns native error messages",
          "[libk][module][ffi][symbol][error]") {
    auto jit = jit_module(R"SRC(
        module __module_symbol_errors__;
        main() : int {
            app : Shared<ApplicationModule> = applicationModule();
            failure : ffi::SymbolLookupError(ffi::SymbolLookupErrorCode::LOOKUP_FAILED, String(""));
            {
                missing : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                    app.get()->lookupSymbol(String("klang_module_symbol_absent_fixture"));
                if (!missing.hasError() || missing.hasResult()) { return 1; }
                failure = missing.getError();
            }
            if (failure.code() != ffi::SymbolLookupErrorCode::NOT_FOUND) { return 2; }
            if (failure.message().empty()) { return 3; }
            message : String = failure.message();
            copy : ffi::SymbolLookupError(failure);

            empty : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(String(""));
            if (!empty.hasError()) { return 4; }
            if (empty.getError().code() != ffi::SymbolLookupErrorCode::INVALID_NAME) { return 5; }
            if (empty.getError().message().empty()) { return 6; }

            name : StringBuilder("klang_module_symbol_application_fixture");
            name.appendChar((char) 0);
            name.appendChar('x');
            embedded : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(name.toString());
            if (!embedded.hasError() || embedded.hasResult()) { return 7; }
            if (embedded.getError().code() != ffi::SymbolLookupErrorCode::INVALID_NAME) { return 8; }

            long_name : StringBuilder("klang_module_symbol_absent_");
            i : unsigned int = 0u;
            while (i < 4096u) { long_name.appendChar('x'); ++i; }
            second : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(long_name.toString());
            if (!second.hasError()) { return 9; }
            if (second.getError().code() != ffi::SymbolLookupErrorCode::NOT_FOUND) { return 10; }
            if (second.getError().message().size() < 4096u) { return 11; }
            if (failure.message() != message || copy.message() != message) { return 12; }

            utf8_name : String("klang_module_symbol_absent_\u00e9");
            utf8_missing : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(utf8_name);
            if (!utf8_missing.hasError()) { return 15; }
            if (utf8_missing.getError().code() != ffi::SymbolLookupErrorCode::NOT_FOUND) { return 16; }
            if (!utf8_missing.getError().message().contains(utf8_name)) { return 17; }

            present : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> =
                app.get()->lookupSymbol(String("klang_module_symbol_application_fixture"));
            if (!present.hasResult()) { return 13; }
            if (failure.message() != message) { return 14; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("NativeSymbol cannot be constructed from an arbitrary address",
          "[libk][module][ffi][symbol][access]") {
    auto jit = jit_module(R"SRC(
        module __module_symbol_arbitrary_address__;
        main() : int {
            owner : Shared<Module>(applicationModule());
            symbol : ffi::NativeSymbol(owner, null);
            return 0;
        }
    )SRC");
    CHECK_FALSE(jit);
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

TEST_CASE("Module manifest exposes unit and namespace annotations",
          "[libk][module][rtti][annotation]") {
    const std::string reg = fs::canonical(fs::path(FIXTURES) / "libmodreg.so").string();
    auto exported = kdi::kdi_read_cbor_file((fs::path(FIXTURES) / "modreg.kdi").string());
    REQUIRE(exported.unit.annotations.size() == 1);
    CHECK(exported.unit.annotations[0].name == "ManifestTag");
    CHECK(exported.unit.annotations[0].source == "@ManifestTag(17)");
    CHECK(exported.unit.root_ns.annotations.empty());
    std::function<const kdi::kdi_namespace*(const kdi::kdi_namespace&)> find_annotated_namespace;
    find_annotated_namespace = [&](const kdi::kdi_namespace& ns) -> const kdi::kdi_namespace* {
        if (ns.name == "child") return &ns;
        for (const auto& child : ns.namespaces) {
            if (auto found = find_annotated_namespace(child)) return found;
        }
        return nullptr;
    };
    const auto* exported_namespace = find_annotated_namespace(exported.unit.root_ns);
    REQUIRE(exported_namespace != nullptr);
    REQUIRE(exported_namespace->annotations.size() == 1);
    CHECK(exported_namespace->annotations[0].source == "@ManifestTag(23)");

    auto jit = jit_module(
        "module __module_annotation_view__;\n"
        "import modreg;\n"
        "main() : int {\n"
        "    module_handle : Shared<Module> = loadModule(String(\"" + reg + "\")).getResult();\n"
        "    units : Vector<UnitView> = module_handle.get()->units();\n"
        "    if (units.size() == 0u) { return 1; }\n"
        "    unit : UnitView = units.get(0);\n"
        "    unit_annotations : const k::Annotation?[]? = unit.getDescriptor().getAnnotations();\n"
        "    if (unit_annotations == null || unit_annotations->size != 1u) { return 2; }\n"
        "    unit_annotation : const k::Annotation? = unit_annotations[0];\n"
        "    if (unit_annotation == null) { return 3; }\n"
        "    unit_type_name : String = String(unit_annotation->getAnnotationType().getName());\n"
        "    if (unit_type_name != String(\"ManifestTag\")) { return 4; }\n"
        "    functions : const k::Function?[]? = unit.getDescriptor().getFunctions();\n"
        "    if (functions == null) { return 12; }\n"
        "    found_probe : bool = false;\n"
        "    i : unsigned int = 0;\n"
        "    while (i < functions->size) {\n"
        "        function : const k::Function? = functions[i];\n"
        "        if (function != null && String(function->getName()) == String(\"annotatedProbe\")) {\n"
        "            function_annotations : const k::Annotation?[]? = function->getAnnotations();\n"
        "            if (function_annotations == null || function_annotations->size != 1u) { return 13; }\n"
        "            function_annotation : const k::Annotation? = function_annotations[0];\n"
        "            if (function_annotation == null) { return 14; }\n"
        "            if (String(function_annotation->getAnnotationType().getName()) != String(\"ManifestTag\")) { return 15; }\n"
        "            parameters : const k::Parameter?[]? = function->getParameters();\n"
        "            if (parameters == null || parameters->size != 1u) { return 16; }\n"
        "            parameter : const k::Parameter? = parameters[0];\n"
        "            if (parameter == null) { return 17; }\n"
        "            parameter_annotations : const k::Annotation?[]? = parameter->getAnnotations();\n"
        "            if (parameter_annotations == null || parameter_annotations->size != 1u) { return 18; }\n"
        "            parameter_annotation : const k::Annotation? = parameter_annotations[0];\n"
        "            if (parameter_annotation == null) { return 19; }\n"
        "            if (String(parameter_annotation->getAnnotationType().getName()) != String(\"ManifestTag\")) { return 20; }\n"
        "            found_probe = true;\n"
        "        }\n"
        "        ++i;\n"
        "    }\n"
        "    if (!found_probe) { return 21; }\n"
        "    root : NamespaceView = unit.rootNamespace();\n"
        "    root_children : Vector<NamespaceView> = root.children();\n"
        "    if (root_children.size() == 0u) { return 5; }\n"
        "    fixture_children : Vector<NamespaceView> = root_children.get(0).children();\n"
        "    if (fixture_children.size() == 0u) { return 6; }\n"
        "    leaf : NamespaceView = fixture_children.get(0);\n"
        "    namespace_annotations : const k::Annotation?[]? = leaf.getDescriptor().getAnnotations();\n"
        "    if (namespace_annotations == null || namespace_annotations->size != 1u) { return 7; }\n"
        "    namespace_annotation : const k::Annotation? = namespace_annotations[0];\n"
        "    if (namespace_annotation == null) { return 8; }\n"
        "    namespace_type_name : String = String(namespace_annotation->getAnnotationType().getName());\n"
        "    if (namespace_type_name != String(\"ManifestTag\")) { return 9; }\n"
        "    return 0;\n"
        "}\n");
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("RTTI visitor dispatches, traverses, prunes, and retains module owners",
          "[libk][module][rtti][visitor]") {
    const std::string fixture = fs::canonical(
        fs::path(FIXTURES) / "libvisitor_fixture.so").string();
    const std::string plugin = fs::canonical(
        fs::path(FIXTURES) / "libmodplugin_a.so").string();

    std::string source = R"SRC(
        module __rtti_visitor__;
        import visitor_fixture;

        class TraceVisitor : public RecursiveRttiVisitor {
            private:
                _events : Vector<int>;
                _expectedOwner : Shared<Module>;
                _ownerOkay : bool = true;

                _record(event : int) { _events.append(event); }

            protected:
                override enterLibraryModule(image : const LibraryModule&) : bool {
                    _record(1);
                    if (currentModule() != _expectedOwner) { _ownerOkay = false; }
                    return true;
                }
                override leaveLibraryModule(image : const LibraryModule&) { _record(101); }
                override enterUnit(unitDescriptor : const Unit&) : bool { _record(2); return true; }
                override leaveUnit(unitDescriptor : const Unit&) { _record(102); }
                override enterNamespace(scope : const Namespace&) : bool { _record(3); return true; }
                override leaveNamespace(scope : const Namespace&) { _record(103); }
                override enterInterface(typeDescriptor : const Interface&) : bool { _record(4); return true; }
                override leaveInterface(typeDescriptor : const Interface&) { _record(104); }
                override enterFunction(functionDescriptor : const Function&) : bool { _record(5); return true; }
                override leaveFunction(functionDescriptor : const Function&) { _record(105); }
                override enterParameter(parameterDescriptor : const Parameter&) : bool { _record(6); return true; }
                override leaveParameter(parameterDescriptor : const Parameter&) { _record(106); }
                override enterAnnotationType(typeDescriptor : const AnnotationType&) : bool { _record(7); return true; }
                override leaveAnnotationType(typeDescriptor : const AnnotationType&) { _record(107); }
                override enterClass(typeDescriptor : const Class&) : bool { _record(8); return true; }
                override leaveClass(typeDescriptor : const Class&) { _record(108); }
                override enterConstructor(constructorDescriptor : const Constructor&) : bool { _record(9); return true; }
                override leaveConstructor(constructorDescriptor : const Constructor&) { _record(109); }
                override annotationObserved(annotationDescriptor : const Annotation&) { _record(50); }

            public:
                setExpectedOwner(owner : Shared<Module>) { _expectedOwner = owner; }
                const ownerIsValid() : bool { return _ownerOkay; }
                const currentIsEmpty() : bool { return !currentModule(); }
                const matches() : bool {
                    expected : int[] = int[]{
                        1, 2, 50, 3, 3, 50,
                        4, 50, 5, 50, 6, 106, 105, 104,
                        7, 50, 107,
                        8, 50, 8, 108,
                        5, 50, 6, 50, 106, 105,
                        9, 50, 6, 50, 106, 109, 108,
                        5, 50, 6, 50, 106, 105,
                        103, 7, 107,
                        5, 6, 106, 6, 106, 6, 106, 105,
                        5, 6, 106, 6, 106, 105,
                        103, 102, 101
                    };
                    if (_events.size() != expected.size) { return false; }
                    i : unsigned int = 0;
                    while (i < expected.size) {
                        if (_events.get(i) != expected[i]) { return false; }
                        ++i;
                    }
                    return true;
                }
        }

        class PruningVisitor : public RecursiveRttiVisitor {
            private:
                _events : Vector<int>;
            protected:
                override enterLibraryModule(image : const LibraryModule&) : bool { _events.append(1); return true; }
                override leaveLibraryModule(image : const LibraryModule&) { _events.append(101); }
                override enterUnit(unitDescriptor : const Unit&) : bool { _events.append(2); return false; }
                override leaveUnit(unitDescriptor : const Unit&) { _events.append(102); }
                override enterNamespace(scope : const Namespace&) : bool { _events.append(3); return true; }
            public:
                const skippedChildrenAndLeft() : bool {
                    return _events.size() == 4u
                        && _events.get(0) == 1
                        && _events.get(1) == 2
                        && _events.get(2) == 102
                        && _events.get(3) == 101;
                }
        }

        class DirectVisitor : public RecursiveRttiVisitor {
            private:
                _units : int = 0;
                _namespaces : int = 0;
            public:
                override visitUnit(unitDescriptor : const Unit&) { ++_units; }
                override visitNamespace(scope : const Namespace&) { ++_namespaces; }
                const unitCount() : int { return _units; }
                const namespaceCount() : int { return _namespaces; }
        }

        class ReentrantVisitor : public RecursiveRttiVisitor {
            private:
                _nested : Shared<Module>;
                _insideNested : bool = false;
                _restored : bool = true;
            protected:
                override enterUnit(unitDescriptor : const Unit&) : bool {
                    if (!_insideNested) {
                        outer : Shared<Module> = currentModule();
                        _insideNested = true;
                        walkModule(_nested);
                        if (currentModule() != outer) { _restored = false; }
                    }
                    return false;
                }
            public:
                setNested(image : Shared<Module>) { _nested = image; }
                const restored() : bool { return _restored && !currentModule(); }
        }

        class ThrowingVisitor : public RecursiveRttiVisitor {
            protected:
                override enterUnit(unitDescriptor : const Unit&) : bool {
                    throw FatalError();
                }
        }

        class SnapshotVisitor : public RecursiveRttiVisitor {
            private:
                _application : Application&;
                _libraries : int = 0;
                _loaded : bool = false;
                _okay : bool = true;
            protected:
                override enterLibraryModule(image : const LibraryModule&) : bool {
                    ++_libraries;
                    if (!_loaded) {
                        _loaded = true;
                        loaded : Expected<Shared<Module>, LoadModuleError> =
                            _application.loadModule(String("@PLUGIN_PATH@"));
                        if (!loaded.hasResult()) { _okay = false; }
                    }
                    return false;
                }
            public:
                SnapshotVisitor(application : Application&) : _application(application) {}
                const valid() : bool { return _okay && _loaded && _libraries == 1; }
        }

        class NativeOnlyVisitor : public RecursiveRttiVisitor {
            private:
                _native : int = 0;
                _units : int = 0;
                _left : int = 0;
            protected:
                override enterNativeLibraryModule(image : const NativeLibraryModule&) : bool {
                    ++_native;
                    return true;
                }
                override leaveNativeLibraryModule(image : const NativeLibraryModule&) {
                    ++_left;
                }
                override enterUnit(unitDescriptor : const Unit&) : bool {
                    ++_units;
                    return true;
                }
            public:
                const valid() : bool { return _native == 1 && _left == 1 && _units == 0; }
        }

        class LinkedModuleCounter : public RecursiveRttiVisitor {
            private:
                _modules : int = 0;
            protected:
                override enterApplicationModule(image : const ApplicationModule&) : bool {
                    ++_modules;
                    return false;
                }
                override enterLibraryModule(image : const LibraryModule&) : bool {
                    ++_modules;
                    return false;
                }
                override enterNativeLibraryModule(image : const NativeLibraryModule&) : bool {
                    ++_modules;
                    return false;
                }
            public:
                const count() : int { return _modules; }
        }

        main() : int {
            loaded : Expected<Shared<Module>, LoadModuleError> =
                loadModule(String("@FIXTURE_PATH@"));
            if (!loaded.hasResult()) { return 1; }
            image : Shared<Module> = loaded.getResult();

            trace : TraceVisitor;
            trace.setExpectedOwner(image);
            trace.walkModule(image);
            if (!trace.matches()) { return 2; }
            if (!trace.ownerIsValid() || !trace.currentIsEmpty()) { return 3; }
            emptyVisitor : RecursiveRttiVisitor;
            emptyVisitor.walkModule(image);
            if (emptyVisitor.currentModule()) { return 14; }
            pruner : PruningVisitor;
            pruner.walkModule(image);
            if (!pruner.skippedChildrenAndLeft()) { return 4; }

            units : Vector<UnitView> = image.get()->units();
            if (units.size() != 1u) { return 5; }
            direct : DirectVisitor;
            units.get(0).accept(direct);
            if (direct.unitCount() != 1 || direct.namespaceCount() != 0) { return 6; }
            root : NamespaceView = units.get(0).rootNamespace();
            root.accept(direct);
            if (direct.unitCount() != 1 || direct.namespaceCount() != 1) { return 7; }
            reentrant : ReentrantVisitor;
            reentrant.setNested(image);
            reentrant.walkModule(image);
            if (!reentrant.restored()) { return 8; }

            throwing : ThrowingVisitor;
            didThrow : bool = false;
            try {
                throwing.walkModule(image);
            } catch (error : FatalError&) {
                didThrow = true;
            }
            if (!didThrow || throwing.currentModule()) { return 9; }

            roots : Vector<Shared<Module>>;
            roots.append(image);
            roots.append(image);
            snapshot : SnapshotVisitor(this);
            snapshot.walkModules(roots);
            if (!snapshot.valid()) { return 10; }

            nativeResult : Expected<Shared<Module>, LoadModuleError> =
                loadModule(String("@NATIVE_PATH@"));
            if (!nativeResult.hasResult()) { return 11; }
            nativeVisitor : NativeOnlyVisitor;
            nativeVisitor.walkModule(nativeResult.getResult());
            if (!nativeVisitor.valid()) { return 12; }

            app : Shared<ApplicationModule> = applicationModule();
            linked : Vector<Shared<Module>> = app.get()->linkedModules();
            distinct : Vector<Shared<Module>>;
            i : unsigned int = 0;
            while (i < linked.size()) {
                candidate : Shared<Module> = linked.get(i);
                found : bool = false;
                j : unsigned int = 0;
                while (j < distinct.size()) {
                    if (distinct.get(j) == candidate) { found = true; break; }
                    ++j;
                }
                if (!found) { distinct.append(candidate); }
                ++i;
            }
            expectedModules : unsigned int = distinct.size();
            linkedCounter : LinkedModuleCounter;
            linkedCounter.walkLinkedModules(app);
            if ((unsigned int) linkedCounter.count() != expectedModules) { return 13; }
            return 0;
        }
    )SRC";
    source.replace(source.find("@FIXTURE_PATH@"), std::string("@FIXTURE_PATH@").size(), fixture);
    source.replace(source.find("@PLUGIN_PATH@"), std::string("@PLUGIN_PATH@").size(), plugin);
    const std::string native = fs::canonical(
        fs::path(FIXTURES) / "libmodnative_dep.so").string();
    source.replace(source.find("@NATIVE_PATH@"), std::string("@NATIVE_PATH@").size(), native);
    auto jit = jit_module(source);
    REQUIRE(jit);
    CHECK(run_main(*jit) == 0);
}

TEST_CASE("RTTI visitor fixture inventories only its declared root functions",
          "[libk][module][rtti][visitor]") {
    const std::string fixture = fs::canonical(
        fs::path(FIXTURES) / "libvisitor_fixture.so").string();
    std::string source = R"SRC(
        module __rtti_visitor_root_inventory__;
        main() : int {
            loaded : Expected<Shared<Module>, LoadModuleError> =
                loadModule(String("@FIXTURE_PATH@"));
            if (!loaded.hasResult()) { return 1; }
            image : Shared<Module> = loaded.getResult();
            units : Vector<UnitView> = image.get()->units();
            if (units.size() != 1u) { return 2; }
            root : NamespaceView = units.get(0).rootNamespace();
            functions : const Function?[]? = root.getDescriptor().getFunctions();
            if (functions == null || functions->size != 2u) { return 3; }
            names : String[2] {
                String("rootThreeParameters"), String("rootTwoParameters")
            };
            parameterNames : String[3] {
                String("first"), String("second"), String("third")
            };
            i : unsigned int = 0;
            while (i < functions->size) {
                functionDescriptor : const Function? = functions[i];
                if (functionDescriptor == null) { return 4; }
                if (String(functionDescriptor->getName()) != names[i]) { return 5; }
                if (functionDescriptor->getOwner() != null
                    || functionDescriptor->isMember()) { return 6; }
                parameters : const Parameter?[]? = functionDescriptor->getParameters();
                if (parameters == null || parameters->size != 3u - i) { return 7; }
                j : unsigned int = 0;
                while (j < parameters->size) {
                    parameterDescriptor : const Parameter? = parameters[j];
                    if (parameterDescriptor == null) { return 8; }
                    if (String(parameterDescriptor->getName()) != parameterNames[j]) { return 9; }
                    ++j;
                }
                ++i;
            }
            return 0;
        }
    )SRC";
    source.replace(source.find("@FIXTURE_PATH@"), std::string("@FIXTURE_PATH@").size(), fixture);
    auto jit = jit_module(source);
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
        "    try {\n"
        "        th0.join();\n"
        "        th1.join();\n"
        "        th2.join();\n"
        "        th3.join();\n"
        "    } catch (e : ThreadInterruptionException&) {\n"
        "        return 5;\n"
        "    }\n"
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
