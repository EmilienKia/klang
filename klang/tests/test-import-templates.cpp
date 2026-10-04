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
 * Integration tests for template K module imports and cross-module instantiation.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
include "../src/model/tools/kdi_exporter.hpp"
#include "../src/model/template.hpp"
#include "../src/model/imported.hpp"
#include "../src/model/template.hpp"
#include "../src/model/expressions.hpp"
#include "../src/model/tools/k_source_emitter.hpp"
#include "../src/model/tools/kdi_exporter.hpp"
#include "../src/model/type.hpp"
#include "../src/errors.hpp"
#include "../../libk/libk/src/runtime/callable_abi.h"

#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

xtern "C" int __k_test_import_template_count(const int* count) {
    return *count;
}

namespace {

constexpr std::string_view protected_member_template_library = R"K(
    module protected_member_tpl_lib;

    public class Base {
    protected:
        _value : int;
        template<typename T>
        assign(value : T) { _value = (int)value; }
    private:
        template<typename T>
        hidden(value : T) { _value = (int)value; }
    public:
        Base() : _value(0) {}
        template<typename T>
        identity(value : T) : T { return value; }
    }
)K";

constexpr std::string_view templated_host_member_template_library = R"K(
    module templated_host_member_tpl_lib;

    template<typename T>
    public struct Cell {
    private:
        friend Cell;
        _value : int;
    public:
        Cell(value : int) : _value(value) {}
        template<typename U>
        sum(other : Cell<U>&) : int { return _value + other._value; }
    }

    instantiate_library_cells() : int {
        first : Cell<int>(20);
        second : Cell<char>(22);
        return first.sum<char>(second);
    }
)K";

constexpr std::string_view private_extern_template_library = R"K(
    module private_extern_tpl_lib;

    template<typename T>
    public final struct NativeCounter {
        friend NativeOwner<T>;
    private:
        _count : int;
        NativeCounter(value : int) : _count(value) {}
        @ffi::Extern("C")
        static __k_test_import_template_count(count : const int*) : int;
        @ffi::Extern("C", null, "__k_test_import_template_count")
        static aliased_count(count : const int*) : int;
        read() : int {
            return __k_test_import_template_count(&_count) + aliased_count(&_count);
        }
    }

    template<typename T>
    public struct NativeOwner {
        read(value : int) : int {
            counter : NativeCounter<T>(value);
            return counter.read();
        }
    }

    template<typename T>
    public struct InvalidNativeCounter {
        @ffi::Extern("Java") private static count(value : const int*) : int;
    }
)K";

struct PrivateExternTemplateImport {
    fs::path kdi_path;
    std::shared_ptr<k::path_lookup_file_resolver> resolver =
        std::make_shared<k::path_lookup_file_resolver>();

    explicit PrivateExternTemplateImport(const std::string& name)
        : kdi_path(fs::current_path() / (name + ".kdi")) {
        auto lib = compile_model_with_stdlib(private_extern_template_library);
        REQUIRE(lib);
        k::model::kdi_builder builder(*lib->get_context_for_test());
        lib->get_unit()->accept(builder);
        REQUIRE(kdi::kdi_write_cbor_file(builder.take(), kdi_path.string()));
        resolver->add_explicit_path("private_extern_tpl_lib", kdi_path);
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    }

    ~PrivateExternTemplateImport() {
        std::error_code ec;
        fs::remove(kdi_path, ec);
    }
};

constexpr std::string_view private_friend_template_library = R"K(
    module private_friend_tpl_lib;

    template<typename T>
    public final struct Block {
        friend Owner<T>;
        friend Reader<T>;
    private:
        _tag : T*;
        _value : int;
        Block(v : int) : _value(v) {}
        get() : int { return _value; }
    }

    template<typename T>
    public struct Reader {
        read(block : Block<T>*) : int { return block->get(); }
    }

    template<typename T>
    public struct Owner {
        read(v : int) : int {
            block : Block<T>(v);
            reader : Reader<T>;
            return block.get() + reader.read(&block);
        }
    }

    dummy() : int { return 0; }
)K";

struct PrivateFriendTemplateImport {
    fs::path kdi_path;
    std::shared_ptr<k::path_lookup_file_resolver> resolver =
        std::make_shared<k::path_lookup_file_resolver>();

    explicit PrivateFriendTemplateImport(const std::string& name)
        : kdi_path(fs::current_path() / (name + ".kdi")) {
        auto lib = compile_model(private_friend_template_library);
        REQUIRE(lib);
        k::model::kdi_builder builder(*lib->get_context_for_test());
        lib->get_unit()->accept(builder);
        REQUIRE(kdi::kdi_write_cbor_file(builder.take(), kdi_path.string()));
        resolver->add_explicit_path("private_friend_tpl_lib", kdi_path);
    }

    ~PrivateFriendTemplateImport() {
        std::error_code ec;
        fs::remove(kdi_path, ec);
    }
};

} // namespace

TEST_CASE("import template aggregate — member templates remain in the host source",
          "[import][template][member-template][private-template-layout][regression]") {
    auto lib = compile_model(templated_host_member_template_library);
    REQUIRE(lib);

    k::model::kdi_builder builder(*lib->get_context_for_test());
    lib->get_unit()->accept(builder);
    auto file = builder.take();
    const auto& defs = file.unit.root_ns.template_defs;
    REQUIRE(defs.size() == 1);
    CHECK(defs.front().name == "Cell");
    CHECK(defs.front().host_fq_name.empty());
    CHECK(std::none_of(defs.begin(), defs.end(), [](const auto& def) {
        return !def.host_fq_name.empty();
    }));

    auto path = fs::current_path() / "templated_host_member_template.kdi";
    REQUIRE(kdi::kdi_write_cbor_file(file, path.string()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("templated_host_member_tpl_lib", path);

    auto comp = k::compiler::create();
    comp->set_file_resolver(resolver);
    comp->parse_source("templated_host_member_tpl_consumer.k", R"K(
        module templated_host_member_tpl_consumer;
        import templated_host_member_tpl_lib;

        struct FirstTag {}
        struct SecondTag {}

        test() : int {
            first : Cell<FirstTag>(20);
            second : Cell<SecondTag>(22);
            return first.sum<SecondTag>(second);
        }
    )K", false, false);

    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    CHECK(compile_should_fail(R"K(
        module templated_host_member_tpl_private_access;
        import templated_host_member_tpl_lib;
        struct Tag {}
        test(cell : Cell<Tag>&) : int { return cell._value; }
    )K", resolver));
}

TEST_CASE("import member template — non-template hosts export protected and public recipes",
          "[import][template][member-template][protected-member-template]") {
    auto lib = compile_model(protected_member_template_library);
    REQUIRE(lib);
    k::model::kdi_builder builder(*lib->get_context_for_test());
    lib->get_unit()->accept(builder);
    auto file = builder.take();
    const auto& defs = file.unit.root_ns.template_defs;
    REQUIRE(defs.size() == 2);
    for (const auto& def : defs) {
        CHECK(def.host_fq_name == "protected_member_tpl_lib::Base");
        CHECK(def.fq_name == "protected_member_tpl_lib::Base::" + def.name);
        CHECK_FALSE(def.source.empty());
        REQUIRE(def.function_signature);
        CHECK_FALSE(def.is_generic);
        CHECK(def.name != "hidden");
        CHECK(def.visibility == (def.name == "assign" ? "protected" : "public"));
    }
}

TEST_CASE("import member template — inherited protected recipes resolve through all receiver paths",
          "[import][template][e2e][member-template][protected-member-template]") {
    auto result = build_exec_with_lib(protected_member_template_library, R"K(
        module protected_member_tpl_consumer;
        import protected_member_tpl_lib;

        class Derived : public Base {
        public:
            Derived() { assign(11); }
            unqualified() : int {
                assign(12);
                return _value;
            }
            direct() : int {
                this.assign<int>(13);
                return _value;
            }
            qualified() : int {
                Base::assign<int>(14);
                return _value;
            }
            value() : int { return _value; }
        }

        main() : int {
            obj : Derived;
            if (obj.value() != 11) return 1;
            if (obj.unqualified() != 12) return 2;
            if (obj.direct() != 13) return 3;
            if (obj.qualified() != 14) return 4;
            if (obj.identity<int>(42) != 42) return 5;
            return 0;
        }
    )K");
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("import member template — protected recipes remain inaccessible outside the host hierarchy",
          "[import][template][member-template][protected-member-template][error]") {
    auto lib = compile_model(protected_member_template_library);
    REQUIRE(lib);
    k::model::kdi_builder builder(*lib->get_context_for_test());
    lib->get_unit()->accept(builder);
    auto path = fs::current_path() / "protected_member_template_negative.kdi";
    REQUIRE(kdi::kdi_write_cbor_file(builder.take(), path.string()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("protected_member_tpl_lib", path);
    auto comp = k::compiler::create();
    comp->set_file_resolver(resolver);
    CHECK_THROWS(comp->parse_source("protected_member_tpl_bad.k", R"K(
        module protected_member_tpl_bad;
        import protected_member_tpl_lib;
        test(base : Base&) { base.assign<int>(42); }
    )K", false, false));
}

TEST_CASE("import member template — inherited protected owner setter transfers derived ownership",
          "[import][template][e2e][member-template][protected-member-template][owner]") {
    auto result = build_exec_with_lib(R"K(
        module protected_owner_tpl_lib;
        public class Payload {
            public value() : int { return 7; }
        }
        public class Owner {
        protected:
            _owned : Payload!;
            template<typename T>
            adopt(value : T!) {
                if (_owned != null) { delete _owned; }
                _owned = value;
            }
        public:
            Owner() {}
            ~Owner() { if (_owned != null) { delete _owned; } }
            value() : int { return _owned->value(); }
        }
    )K", R"K(
        module protected_owner_tpl_consumer;
        import protected_owner_tpl_lib;
        class DerivedPayload : public Payload {
            public override value() : int { return 42; }
        }
        class DerivedOwner : public Owner {
            public DerivedOwner() { adopt(new DerivedPayload()); }
            public replace() { adopt(new DerivedPayload()); }
        }
        main() : int {
            owner : DerivedOwner;
            k::io::stdout().println(owner.value());
            if (owner.value() != 42) return 1;
            owner.replace();
            if (owner.value() != 42) return 2;
            return 0;
        }
    )K");
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("import member template — protected recipes resolve namespaced stream owner locals",
          "[import][template][e2e][member-template][protected-member-template][owner][stream-owner]") {
    auto result = build_exec_with_lib(R"K(
        module protected_stream_tpl_lib;
        namespace io {
            template<typename E>
            public interface Stream {
                public value() : E;
            }
            public class Borrower {
                private _stream : Stream<byte>*;
                public Borrower(stream : Stream<byte>*) : _stream(stream) {}
                public Borrower(value : int) : _stream(null) {}
                public rebind(stream : Stream<byte>*) { _stream = stream; }
                public rebind(value : int) { _stream = null; }
                public value() : byte { return _stream->value(); }
            }
        }
        public class Owner {
        protected:
            _owned : io::Stream<byte>!;
            _adapter : io::Borrower!;
            template<typename T>
            adopt(value : T!) {
                if (_adapter != null) { delete _adapter; }
                if (_owned != null) { delete _owned; }
                ownedRoot : io::Stream<byte>! = value;
                _adapter = new io::Borrower(&*ownedRoot);
                root : io::Stream<byte>* = &*ownedRoot;
                _adapter->rebind(root);
                _adapter->rebind(&*ownedRoot);
                _owned = ownedRoot;
            }
        public:
            Owner() {}
            ~Owner() {
                if (_adapter != null) { delete _adapter; }
                if (_owned != null) { delete _owned; }
            }
            value() : byte { return _adapter->value(); }
            const borrowedValue() : byte {
                reader : io::Borrower(_owned);
                return reader.value();
            }
        }
    )K", R"K(
        module protected_stream_tpl_consumer;
        import protected_stream_tpl_lib;
        class FixtureStream : public protected_stream_tpl_lib::io::Stream<byte> {
            private _destroyed : int*;
            public FixtureStream(destroyed : int*) : _destroyed(destroyed) {}
            public ~FixtureStream() { ++*_destroyed; }
            public override value() : byte { return (byte)42; }
        }
        class DerivedOwner : public Owner {
            public DerivedOwner(destroyed : int*) { adopt(new FixtureStream(destroyed)); }
            public replace(destroyed : int*) { adopt(new FixtureStream(destroyed)); }
        }
        main() : int {
            destroyed : int = 0;
            {
                owner : DerivedOwner(&destroyed);
                if ((int)owner.value() != 42 || destroyed != 0) return 1;
                if ((int)owner.borrowedValue() != 42 || destroyed != 0) return 4;
                owner.replace(&destroyed);
                if ((int)owner.value() != 42 || destroyed != 1) return 2;
                if ((int)owner.borrowedValue() != 42 || destroyed != 1) return 5;
            }
            if (destroyed != 2) return 3;
            return 0;
        }
    )K");
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("import member template — default recipes retain their interface host",
          "[import][template][e2e][member-template][default-member-template]") {
    auto result = build_exec_with_lib(R"K(
        module default_member_tpl_lib;
        public interface Mapper {
            template<typename T>
            public default identity(value : T) : T { return value; }
            public default seed() : int { return 7; }
        }
    )K", R"K(
        module default_member_tpl_consumer;
        import default_member_tpl_lib;
        class ConcreteMapper : public Mapper {
            public ConcreteMapper() {}
        }
        main() : int {
            mapper : ConcreteMapper;
            if (mapper.identity<int>(42) != 42) return 1;
            if (mapper.identity(7) != mapper.seed()) return 2;
            return 0;
        }
    )K");
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("import member template — default remains invalid on class recipes",
          "[import][template][member-template][default-member-template][error]") {
    auto lib = compile_model(R"K(
        module class_default_member_tpl_lib;
        public class Mapper {
            template<typename T>
            public identity(value : T) : T { return value; }
        }
    )K");
    REQUIRE(lib);
    k::model::kdi_builder builder(*lib->get_context_for_test());
    lib->get_unit()->accept(builder);
    auto file = builder.take();
    REQUIRE(file.unit.root_ns.template_defs.size() == 1);
    file.unit.root_ns.template_defs.front().source = R"K(
        template<typename T>
        public default identity(value : T) : T { return value; }
    )K";
    auto path = fs::current_path() / "class_default_member_template_negative.kdi";
    REQUIRE(kdi::kdi_write_cbor_file(file, path.string()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("class_default_member_tpl_lib", path);
    test_logger logger;
    CHECK_FALSE(compile_collect_diagnostics(R"K(
        module class_default_member_tpl_consumer;
        import class_default_member_tpl_lib;
        test(mapper : Mapper&) : int { return mapper.identity<int>(42); }
    )K", resolver, logger));
    CHECK(std::any_of(logger.diagnostics.begin(), logger.diagnostics.end(),
        [](const k::log::diagnostic& diagnostic) {
            return diagnostic.message.find("No viable overload found for")
                != std::string::npos;
        }));
}

TEST_CASE("import template — private static externs retain raw C linkage in consumer instantiations",
          "[import][template][consumer-inst][ffi][template-extern]") {
    PrivateExternTemplateImport lib("private_extern_template_positive");
    REQUIRE(ensure_libk_loaded());
    auto comp = k::compiler::create();
    comp->set_file_resolver(lib.resolver);
    comp->parse_source("private_extern_tpl_exe.k", R"K(
        module private_extern_tpl_exe;
        import private_extern_tpl_lib;

        struct FirstTag {}
        struct SecondTag {}
        inspect(owner : NativeOwner<FirstTag>&) : int { return owner.read(10); }
        test() : int {
            first : NativeOwner<FirstTag>;
            second : NativeOwner<SecondTag>;
            return inspect(first) + second.read(11);
        }
    )K", false, false);

    auto& module = comp->get_context_for_test()->module();
    auto* native = module.getFunction("__k_test_import_template_count");
    REQUIRE(native != nullptr);
    CHECK(native->isDeclaration());
    CHECK(module.getFunction("__k_test_import_template_count.1") == nullptr);

    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("import template — invalid private static extern annotations are still rejected",
          "[import][template][ffi][template-extern][error]") {
    PrivateExternTemplateImport lib("private_extern_template_negative");
    auto comp = k::compiler::create();
    comp->set_file_resolver(lib.resolver);
    try {
        comp->parse_source("private_extern_tpl_bad.k", R"K(
            module private_extern_tpl_bad;
            import private_extern_tpl_lib;
            test() : int {
                counter : InvalidNativeCounter<int>;
                return 0;
            }
        )K", false, false);
        FAIL("Expected the unsupported FFI language diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(std::string(error.what()).find("unsupported language 'Java'") != std::string::npos);
    }
}

TEST_CASE("import template — private constructor and helper methods remain accessible to friends",
          "[import][template][cross-tpl][consumer-inst][private-template-friend]") {
    PrivateFriendTemplateImport lib("private_friend_template_positive");
    REQUIRE(ensure_libk_loaded());
    auto comp = k::compiler::create();
    comp->set_file_resolver(lib.resolver);
    comp->parse_source("private_friend_tpl_exe.k", R"K(
            module private_friend_tpl_exe;
            import private_friend_tpl_lib;

            struct ConsumerTag {}

            test() : int {
                owner : Owner<ConsumerTag>;
                return owner.read(21);
            }
        )K", false, false);

    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("import template — imported instantiation restores private fields when synthesized",
          "[import][template][cross-tpl][consumer-inst][private-template-layout][regression]") {
    PrivateFriendTemplateImport lib("private_friend_template_layout");
    REQUIRE(ensure_libk_loaded());
    auto comp = k::compiler::create();
    comp->set_file_resolver(lib.resolver);
    comp->parse_source("private_friend_tpl_layout.k", R"K(
        module private_friend_tpl_layout;
        import private_friend_tpl_lib;
        test() : int {
            owner : Owner<int>;
            return owner.read(21);
        }
    )K", false, false);

    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("import template — private helper API remains inaccessible to non-friends",
          "[import][template][cross-tpl][private-template-friend][error]") {
    PrivateFriendTemplateImport lib("private_friend_template_negative");

    SECTION("private constructor") {
        REQUIRE(compile_should_fail(R"K(
            module private_friend_ctor_denied;
            import private_friend_tpl_lib;

            struct ConsumerTag {}

            main() : int {
                block : Block<ConsumerTag>(42);
                return 0;
            }
        )K", lib.resolver));
    }

    SECTION("private pointer member invocation") {
        REQUIRE(compile_should_fail(R"K(
            module private_friend_call_denied;
            import private_friend_tpl_lib;

            struct ConsumerTag {}

            main() : int {
                block : Block<ConsumerTag>*;
                return block->get();
            }
        )K", lib.resolver));
    }
namespace {
int private_handle_calls;
int private_callback_calls;
int private_operation_calls;
int private_submit_calls;
int private_native_errors;
int private_native_storage;
}

extern "C" void* __k_private_native_handle(void* handle) {
    ++private_handle_calls;
    if (handle) ++private_native_errors;
    return &private_native_storage;
}

extern "C" void __k_private_native_callback(void* handle, KCallable callback) {
    ++private_callback_calls;
    if (handle != &private_native_storage || callback.fn || callback.ctx) ++private_native_errors;
}

extern "C" void* __k_native_operation() {
    ++private_operation_calls;
    return &private_native_storage;
}

extern "C" int __k_native_submit(void* operation) {
    ++private_submit_calls;
    if (operation && operation != &private_native_storage) ++private_native_errors;
    return 0;
}

extern "C" int __k_private_callback_import(KCallable invoke, KCallable drop,
                                           KCallable borrowed, KCallable owned,
                                           const char* text) {
    // The runtime fixture uses free functions only; no owned context crosses FFI.
    if (!invoke.fn || !drop.fn || !borrowed.fn || !owned.fn
        || invoke.ctx || drop.ctx || borrowed.ctx || owned.ctx || text) return -2;
    reinterpret_cast<void (*)(void*, void*)>(invoke.fn)(nullptr, nullptr);
    reinterpret_cast<void (*)(void*)>(drop.fn)(nullptr);
    return reinterpret_cast<int (*)(int)>(borrowed.fn)(40)
        + reinterpret_cast<int (*)(int)>(owned.fn)(2);
}

// Template struct / function import tests
//
// These tests exercise the basic import of template instantiations across
// module boundaries.  The library defines a template and creates concrete
// instantiations; the consumer module imports the library and uses
// the instantiations.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Template struct: library exports a template struct instantiation and a
// wrapper function.  Consumer calls the wrapper that exercises the template.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct instantiation used via wrapper function",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tpllib;

            template<typename T>
            struct Box {
                val : T;
            }

            box_roundtrip(v : int) : int {
                b : Box<int>;
                b.val = v;
                return b.val;
            }
        )K",
        R"K(
            module tplexec;
            import tpllib;

            main() : int {
                return tpllib::box_roundtrip(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template function: library exports a template function instantiation.
// Consumer calls a wrapper that exercises it.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — function instantiation used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplfnlib;

            template<typename T>
            identity(x : T) : T {
                return x;
            }

            call_identity(v : int) : int {
                return identity<int>(v);
            }
        )K",
        R"K(
            module tplfnexec;
            import tplfnlib;

            main() : int {
                return tplfnlib::call_identity(77);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 77 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with method: consumer calls wrapper that exercises
// methods on a concrete template instantiation.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct with method used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplmethlib;

            template<typename T, int N>
            struct Holder {
                val : T;
                get_n() : int { return N; }
            }

            holder_test() : int {
                h : Holder<int, 33>;
                return h.get_n();
            }
        )K",
        R"K(
            module tplmethexec;
            import tplmethlib;

            main() : int {
                return tplmethlib::holder_test();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 33 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template with access to struct data from outside the template:
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — consumer calls wrapper returning template struct value",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplaccess;

            template<typename T>
            struct Wrapper {
                val : T;
            }

            make_and_read(v : int) : int {
                w : Wrapper<int>;
                w.val = v;
                return w.val;
            }
        )K",
        R"K(
            module tplaccessexec;
            import tplaccess;

            main() : int {
                return tplaccess::make_and_read(55);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 55 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with constructor:
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct with constructor used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplctor;

            template<typename T>
            struct Container {
                val : T;
            }

            make_container(v : int) : int {
                c : Container<int>;
                c.val = v;
                return c.val;
            }
        )K",
        R"K(
            module tplctorexec;
            import tplctor;

            main() : int {
                return tplctor::make_container(88);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 88 );
}

TEST_CASE("import template — exit code via wrapper function",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplexit;

            template<typename T>
            struct Pair {
                first : T;
                second : T;
            }

            sum_pair(a : int, b : int) : int {
                p : Pair<int>;
                p.first = a;
                p.second = b;
                return p.first + p.second;
            }
        )K",
        R"K(
            module tplexitexec;
            import tplexit;

            main() : int {
                return tplexit::sum_pair(42, 57);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 99 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template definition export: verify that template_def is present in KDI
// when a library defines a template without instantiating it.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — template_def exported in KDI for uninstantiated template",
          "[import][template][model]") {
    TmpKdi lib(R"K(
        module tpldefonly;

        template<typename T>
        struct Storage {
            data : T;
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    // The template definition should be exported as a kdi_template_def
    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Storage") {
                REQUIRE(td.fq_name == "tpldefonly::Storage");
                REQUIRE(td.entity_kind == "struct");
                REQUIRE(td.params.size() == 1);
                REQUIRE(td.params[0].kind == "typename");
                REQUIRE(td.params[0].name == "T");
                REQUIRE(!td.source.empty());
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import generic — template_def exported as signature-only metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericmeta;

        generic<typename T>
        struct Box {
            public value : T&;
            relay(v : T&) : T& { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Box") {
                REQUIRE(td.is_generic);
                REQUIRE(td.source.empty());
                REQUIRE(td.aggregate_signature != nullptr);
                REQUIRE(td.aggregate_signature->methods.size() == 1);
                auto* member = std::get_if<kdi::kdi_layout_member>(&td.aggregate_signature->layout[0]);
                REQUIRE(member != nullptr);
                REQUIRE(std::holds_alternative<kdi::kdi_ref_type>(member->type.value));
                auto& inner = *std::get<kdi::kdi_ref_type>(member->type.value).inner;
                REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(inner.value));
                REQUIRE(std::get<kdi::kdi_template_param_ref>(inner.value).name == "T");
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import template — private FFI dependencies keep their definition scope",
          "[import][template][ffi][private-dependency]") {
    constexpr std::string_view library = R"K(
        module tplprivateffi;
        @k::ffi::Extern("C", null, "abs") private __private_abs(x : int) : int;

        template<typename T>
        struct PrivateFfiFuture {
            cancel() : bool { return __private_abs(-1) == 1; }
            waitQuiescent() : int { return tplprivateffi::__private_abs(-42); }
        }

        template<typename T>
        struct PrivateFfiExecutor {
            execute() : int { return __private_abs(-42); }
        }

        template<typename T>
        privateFfiCall(x : int) : int { return __private_abs(x); }
    )K";

    SECTION("the KDI carries declarations in source, never in the public API") {
        TmpKdi lib(library, true);
        auto file = kdi::kdi_read_cbor_file(lib.kdi_path);
        const auto& root = file.unit.root_ns;
        REQUIRE(std::none_of(root.functions.begin(), root.functions.end(),
            [](const auto& fn) { return fn.name == "__private_abs"; }));
        size_t dependency_templates = 0;
        for (const auto& definition : root.template_defs) {
            if (definition.name != "PrivateFfiFuture"
                && definition.name != "PrivateFfiExecutor"
                && definition.name != "privateFfiCall") continue;
            ++dependency_templates;
            REQUIRE(definition.source.find("namespace __kdi_private_") != std::string::npos);
            REQUIRE(definition.source.find("private __private_abs") != std::string::npos);
            REQUIRE(definition.source.find(".symbol = \"abs\"") != std::string::npos);
        }
        REQUIRE(dependency_templates == 3);
    }

    SECTION("late aggregate and function clones use private FFI, not consumer declarations") {
        auto result = build_exec_with_lib(library, R"K(
            module tplprivatefficonsumer;
            import tplprivateffi;

            __private_abs(x : int) : int { return -100; }
            main() : int {
                future : PrivateFfiFuture<int>;
                executor : PrivateFfiExecutor<long>;
                if (!future.cancel()) return -1;
                if (future.waitQuiescent() != 42) return -2;
                if (executor.execute() != 42) return -3;
                return privateFfiCall<int>(-42);
            }
        )K");
        INFO(result.err);
        REQUIRE(result.exit_code == 42);
    }

    SECTION("consumer code cannot call the private dependency") {
        TmpKdi lib(library, true);
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        resolver->add_explicit_path("tplprivateffi", lib.kdi_path);
        compiler->set_file_resolver(resolver);
        try {
            compiler->parse_source("consumer.k", R"K(
            module tplprivateffirejected;
            import tplprivateffi;
            main() : int { return __private_abs(-42); }
        )K");
        } catch (const k::log::compiler_error& error) {
            CHECK(error.get_diagnostic().code
                == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_AMBIGUOUS_OVERLOAD));
        }
        CHECK_FALSE(compiler->to_jit());
    }

    SECTION("knowing the synthetic namespace does not grant definition privileges") {
        TmpKdi lib(library, true);
        auto scope = k::model::template_private_scope_name("tplprivateffi");
        for (bool in_namespace : {false, true}) {
            auto compiler = k::compiler::create();
            auto resolver = std::make_shared<k::path_lookup_file_resolver>();
            resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
            resolver->add_explicit_path("tplprivateffi", lib.kdi_path);
            compiler->set_file_resolver(resolver);
            auto call = in_namespace ? "__private_abs(-42)"
                : "tplprivateffi::" + scope + "::__private_abs(-42)";
            auto body = "template<typename T> forbidden(): int { return " + call + "; }\n"
                + "probe(): int { return forbidden<int>(); }\n";
            auto source = "module tplprivateffirejected; import tplprivateffi;\n"
                + (in_namespace ? "namespace tplprivateffi::" + scope + " {\n" + body
                    + "}\nmain(): int { return tplprivateffi::" + scope + "::probe(); }\n"
                    : body + "main(): int { return probe(); }\n");
            try {
                compiler->parse_source("consumer.k", source);
            } catch (const k::log::compiler_error& error) {
                CHECK(error.get_diagnostic().code
                    == static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED));
            }
        }
    }

    SECTION("private dependencies cannot escape through addresses or redirectors") {
        TmpKdi lib(library, true);
        auto qualified = "tplprivateffi::" + k::model::template_private_scope_name("tplprivateffi")
            + "::__private_abs";
        for (const auto& body : {
                "main(): int { callback: *(int):int = " + qualified + "; return callback(-42); }",
                "redirect(value: int): int -> " + qualified + "; main(): int { return redirect(-42); }",
                "callback: *(int):int = " + qualified + "; main(): int { return callback(-42); }"}) {
            auto compiler = k::compiler::create();
            auto resolver = std::make_shared<k::path_lookup_file_resolver>();
            resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
            resolver->add_explicit_path("tplprivateffi", lib.kdi_path);
            compiler->set_file_resolver(resolver);
            try {
                compiler->parse_source("consumer.k",
                    "module tplprivateffirejected; import tplprivateffi;\n" + body);
            } catch (const k::log::compiler_error& error) {
                CHECK(error.get_diagnostic().code
                    == static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED));
            }
        }
    }

    SECTION("re-export preserves private declarations and their native symbols") {
        std::vector<LibSpec> libraries = {
            {library},
            {R"K(
                module tplprivateffifacade;
                import tplprivateffi;
                exported() : int { return 0; }
            )K"}
        };
        auto result = build_exec_with_libs(libraries, R"K(
            module tplprivateffitransitive;
            import tplprivateffifacade;
            __private_abs(x : int) : int { return -100; }
            main() : int {
                future : PrivateFfiFuture<long>;
                if (!future.cancel()) return -1;
                return future.waitQuiescent();
            }
        )K");
        INFO(result.err);
        REQUIRE(result.exit_code == 42);
    }
}

TEST_CASE("import template aliases retain canonical declaration types under consumer shadows",
          "[import][template][alias][declaration-scope][type-provenance]") {
    constexpr std::string_view library = R"K(
        module tplaliasorigin;
        namespace io {
            struct KdiAliasIoError { code: int; }
            template<typename T, typename E>
            struct Result { value: T; error: E; }
            public template<typename T> alias IoResult : Result<T, KdiAliasIoError>;
            public template<typename T> alias IoCompletion : !(IoResult<T>);
            template<typename T>
            makeResult(value: T): IoResult<T> {
                result: IoResult<T>;
                result.value = value;
                result.error.code = 2;
                return result;
            }
        }
    )K";
    TmpKdi lib(library, true);
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplaliasorigin", lib.kdi_path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    constexpr std::string_view consumer = R"K(
        module tplaliasconsumer;
        import tplaliasorigin;
        namespace consumer {
        struct KdiAliasIoError { wrong: long; }
        template<typename T> alias IoResult : int;
        template<typename T, typename E> struct Result { wrong: int; }
        receive(completion: tplaliasorigin::io::IoCompletion<int>) {}
        run(): int {
            result: tplaliasorigin::io::IoResult<int> = tplaliasorigin::io::makeResult<int>(40);
            receive([](value: tplaliasorigin::io::IoResult<int>) {});
            return result.value + result.error.code;
        }
        }
        run(): int { return consumer::run(); }
        main(): int { return run(); }
    )K";
    compiler->parse_source("aliasconsumer.k", consumer);
    auto root = compiler->get_unit()->get_root_namespace();
    CHECK(root->get_fq_name() == "::tplaliasconsumer");
    auto origin = root->get_child_namespace("tplaliasorigin")->get_child_namespace("io");
    REQUIRE(origin->get_alias("IoResult"));
    CHECK(origin->get_alias("IoResult")->parent<k::model::ns>() == origin);
    CHECK(origin->get_alias("IoResult")->get_tpl_info()->origin_module_ns_fq == "tplaliasorigin::io");
    REQUIRE(origin->get_aggregate("Result"));
    CHECK(origin->get_aggregate("Result")->parent<k::model::ns>() == origin);
    auto completion = std::dynamic_pointer_cast<k::model::callable_type>(
        root->get_child_namespace("consumer")->get_function("receive")->parameters().front()->get_type());
    REQUIRE(completion);
    REQUIRE(completion->get_parameter_types().size() == 1);
    auto result = std::dynamic_pointer_cast<k::model::struct_type>(completion->get_parameter_types()[0]);
    REQUIRE(result);
    CHECK(result->get_struct()->get_fq_name().starts_with("::tplaliasorigin::io::Result"));
    REQUIRE(result->get_struct()->get_tpl_args().size() == 2);
    CHECK(result->get_struct()->get_tpl_args()[1].type_arg == origin->get_aggregate("KdiAliasIoError")->get_struct_type());
    auto facade = k::model::build_kdi(*compiler->get_context_for_test(), *compiler->get_unit(), "libaliasfacade.so");
    std::ostringstream encoded;
    kdi::kdi_write_json(facade, encoded);
    std::istringstream json(encoded.str());
    auto restored = kdi::kdi_read_json(json);
    size_t aliases = 0;
    std::function<void(const kdi::kdi_namespace&)> check_aliases = [&](const auto& ns) {
        for (const auto& alias : ns.aliases) {
            if (!alias.is_template || alias.origin_module != "tplaliasorigin::io") continue;
            ++aliases;
            CHECK_FALSE(alias.source.empty());
            CHECK(alias.fq_name.starts_with("tplaliasorigin::io::"));
        }
        for (const auto& child : ns.namespaces) check_aliases(child);
    };
    check_aliases(restored.unit.root_ns);
    CHECK(aliases == 2);

    auto direct = build_exec_with_lib(library, consumer);
    INFO(direct.err);
    CHECK(direct.exit_code == 42);

    std::vector<LibSpec> libraries = {{library}, {R"K(
        module tplaliasfacade;
        import tplaliasorigin;
        exported(): int { return 0; }
    )K"}};
    auto transitive = build_exec_with_libs(libraries, R"K(
        module tplaliastransitive;
        import tplaliasfacade;
        namespace consumer {
        struct KdiAliasIoError { wrong: long; }
        template<typename T> alias IoResult : int;
        run(): int {
            result: tplaliasorigin::io::IoResult<int> = tplaliasorigin::io::makeResult<int>(40);
            return result.value + result.error.code;
        }
        }
        main(): int { return consumer::run(); }
    )K");
    INFO(transitive.err);
    CHECK(transitive.exit_code == 42);
}

TEST_CASE("import template native dependencies resolve parent and sibling scopes exactly",
          "[import][template][ffi][private-dependency][root-dependency][regression]") {
    constexpr std::string_view library = R"K(
        module tplnativedependencies;
        @k::ffi::Extern("C", null, "abs")
        private __native_value(value: int): int;
        @k::ffi::Extern("C", null, "__unreferenced_native")
        private __comment_only(value: int): int;
        template<typename T>
        struct RootModuleCarrier {
            apply(value: int): int { return __native_value(value); }
        }
        namespace detail {
            @k::ffi::Extern("C", null, "labs")
            private __native_value(value: long): long;
            template<typename T>
            struct SiblingCarrier {
                apply(value: long): long { return __native_value(value); }
            }
        }
        namespace io {
            template<typename T>
            struct RootCarrier {
                apply(value: int): int {
                    // __comment_only(value) is not a dependency.
                    label: const char[] = "__comment_only";
                    return __native_value(value)
                        + tplnativedependencies::__native_value(-1)
                        + ::tplnativedependencies::__native_value(-1);
                }
            }
            template<typename T>
            rootCall(value: int): int { return __native_value(value); }
            template<typename T>
            shadowed(__comment_only: *(int):int): int { return __comment_only(1); }
        }
    )K";
    TmpKdi lib(library, true);
    auto file = kdi::kdi_read_cbor_file(lib.kdi_path);
    auto io = std::find_if(file.unit.root_ns.namespaces.begin(), file.unit.root_ns.namespaces.end(),
        [](const auto& ns) { return ns.name == "io"; });
    REQUIRE(io != file.unit.root_ns.namespaces.end());
    size_t checked = 0;
    for (const auto& definition : io->template_defs) {
        if (definition.name == "shadowed") {
            CHECK(definition.source.find("__kdi_private_") == std::string::npos);
            continue;
        }
        if (definition.name != "RootCarrier" && definition.name != "rootCall") continue;
        ++checked;
        INFO(definition.source);
        REQUIRE(definition.source.find(".symbol = \"abs\"") != std::string::npos);
        CHECK(definition.source.find("__unreferenced_native") == std::string::npos);
        if (definition.name == "RootCarrier") {
            CHECK(definition.source.find(".symbol = \"labs\"") == std::string::npos);
            CHECK(definition.source.find("// __comment_only(value)") != std::string::npos);
            CHECK(definition.source.find("\"__comment_only\"") != std::string::npos);
            CHECK(definition.source.find("tplnativedependencies::__native_value") == std::string::npos);
        }
        test_logger logger;
        k::source source{definition.source};
        k::parse::parser parser(logger, source);
        auto ast = parser.parse_unit();
        REQUIRE(ast);
        REQUIRE_FALSE(logger.has_error());
        CHECK(ast->declarations.size() == 2);
    }
    REQUIRE(checked == 2);
    auto detail = std::find_if(file.unit.root_ns.namespaces.begin(), file.unit.root_ns.namespaces.end(),
        [](const auto& ns) { return ns.name == "detail"; });
    REQUIRE(detail != file.unit.root_ns.namespaces.end());
    auto sibling = std::find_if(detail->template_defs.begin(), detail->template_defs.end(),
        [](const auto& definition) { return definition.name == "SiblingCarrier"; });
    REQUIRE(sibling != detail->template_defs.end());
    CHECK(sibling->source.find(".symbol = \"labs\"") != std::string::npos);
    CHECK(sibling->source.find(".symbol = \"abs\"") == std::string::npos);
    std::vector<LibSpec> libraries = {{library}, {R"K(
        module tplnativedependenciesfacade;
        import tplnativedependencies;
        exported(): int { return 0; }
    )K"}};
    auto result = build_exec_with_libs(libraries, R"K(
        module tplnativedependenciesconsumer;
        import tplnativedependenciesfacade;
        __native_value(value: int): int { return -100; }
        main(): int {
            root: tplnativedependencies::RootModuleCarrier<int>;
            carrier: tplnativedependencies::io::RootCarrier<int>;
            sibling: tplnativedependencies::detail::SiblingCarrier<long>;
            if (root.apply(-42) != 42) return -3;
            if (sibling.apply(-42L) != 42L) return -2;
            if (tplnativedependencies::io::rootCall<int>(-42) != 42) return -1;
            return carrier.apply(-40);
        }
    )K");
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("import template native dependency aliases require compatible ABI declarations",
          "[import][template][ffi][private-dependency][native-abi][regression]") {
    TmpKdi lib(R"K(
        module tplnativeabi;
        @k::ffi::Extern("C", null, "abs") private __private_abs(value: int): int;
        template<typename T>
        nativeCall(value: int): int { return __private_abs(value); }
    )K", true);
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplnativeabi", lib.kdi_path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    std::string_view declaration;
    bool compatible = false;
    SECTION("compatible aliases reuse the exact native declaration") {
        declaration = "compatible(value: int): int;";
        compatible = true;
    }
    SECTION("incompatible parameter type is diagnosed") {
        declaration = "incompatible(value: long): int;";
    }
    SECTION("incompatible return type is diagnosed") {
        declaration = "incompatible(value: int): long;";
    }
    try {
        compiler->parse_source("consumer.k", std::string(R"K(
            module tplnativeabiconsumer;
            import tplnativeabi;
            @k::ffi::Extern("C", null, "abs") private
        )K") + std::string(declaration) + R"K(
            main(): int { return nativeCall<int>(-42); }
        )K");
    } catch (const k::log::compiler_error& error) {
        REQUIRE_FALSE(compatible);
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::codegen_diag::ERR_DUPLICATE_MANGLED_NAME));
    }
    if (compatible) {
        REQUIRE(compiler->verify_gen_code());
        auto& module = compiler->get_context_for_test()->module();
        CHECK(module.getFunction("abs") != nullptr);
        CHECK(module.getFunction("abs.1") == nullptr);
    } else {
        CHECK_FALSE(compiler->to_jit());
    }
}

TEST_CASE("private producer native aliases share symbols only with compatible declarations",
          "[import][ffi][private-dependency][native-abi][regression]") {
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    std::string_view aliases;
    bool compatible = false;
    SECTION("same-owner and sibling aliases reuse the C declaration") {
        compatible = true;
        aliases = R"K(
            @k::ffi::Extern("C", null, "abs") private second(value: int): int;
            namespace sibling {
                @k::ffi::Extern("C", null, "abs") private third(value: int): int;
                call(): int { return third(-14); }
            }
            run(): int { return first(-14) + second(-14) + sibling::call(); }
        )K";
    }
    SECTION("same-owner aliases reject incompatible parameters") {
        aliases = R"K(
            @k::ffi::Extern("C", null, "abs") private second(value: long): int;
        )K";
    }
    SECTION("same-name overloads reject incompatible parameters") {
        aliases = R"K(
            @k::ffi::Extern("C", null, "abs") private first(value: long): int;
        )K";
    }
    SECTION("same-owner aliases reject incompatible returns") {
        aliases = R"K(
            @k::ffi::Extern("C", null, "abs") private second(value: int): long;
        )K";
    }
    const auto source = std::string(R"K(
        module nativeproduceraliases;
        @k::ffi::Extern("C", null, "abs") private first(value: int): int;
    )K") + std::string(aliases);
    try {
        compiler->parse_source("nativeproduceraliases.k", source, false, false);
    } catch (const k::log::compiler_error& error) {
        REQUIRE_FALSE(compatible);
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::codegen_diag::ERR_DUPLICATE_MANGLED_NAME));
    }
    if (compatible) {
        REQUIRE(compiler->verify_gen_code());
        auto root = compiler->get_unit()->get_root_namespace();
        auto ctx = compiler->get_context_for_test();
        auto native = ctx->module().getFunction("abs");
        REQUIRE(native);
        CHECK(ctx->lookup_llvm_function(root->get_function("first")) == native);
        CHECK(ctx->lookup_llvm_function(root->get_function("second")) == native);
        CHECK(ctx->lookup_llvm_function(root->get_child_namespace("sibling")->get_function("third")) == native);
        CHECK(ctx->module().getFunction("abs.1") == nullptr);
        auto result = build_exec_with_lib(source, R"K(
            module nativeproduceraliasconsumer;
            import nativeproduceraliases;
            main(): int { return nativeproduceraliases::run(); }
        )K");
        INFO(result.err);
        CHECK(result.exit_code == 42);
    } else {
        CHECK_FALSE(compiler->to_jit());
    }
}

TEST_CASE("import homonymous template aliases specialize in independent origin namespaces",
          "[import][template][alias][homonym-imports][type-provenance]") {
    std::vector<LibSpec> libraries = {
        {R"K(
            module tplaliasleft;
            namespace detail { struct LeftAliasError { code: int; } }
            template<typename T, typename E> struct Result { value: T; error: E; }
            public template<typename T> alias IoResult: Result<T, detail::LeftAliasError>;
            template<typename T> make(value: T): IoResult<T> {
                result: IoResult<T>;
                result.value = value;
                result.error.code = 2;
                return result;
            }
        )K"},
        {R"K(
            module tplaliasright;
            namespace detail { struct RightAliasError { padding: long; code: int; } }
            template<typename T, typename E> struct Result { error: E; value: T; }
            public template<typename T> alias IoResult: Result<T, detail::RightAliasError>;
            template<typename T> make(value: T): IoResult<T> {
                result: IoResult<T>;
                result.value = value;
                result.error.code = 3;
                return result;
            }
        )K"}
    };
    auto result = build_exec_with_libs(libraries, R"K(
        module tplaliashomonyms;
        import tplaliasleft;
        import tplaliasright;
        namespace consumer {
        struct LeftAliasError { wrong: long; }
        struct RightAliasError { wrong: long; }
        template<typename T> alias IoResult: int;
        run(): int {
            left: tplaliasleft::IoResult<int> = tplaliasleft::make<int>(17);
            right: tplaliasright::IoResult<int> = tplaliasright::make<int>(20);
            return left.value + left.error.code + right.value + right.error.code;
        }
        }
        main(): int { return consumer::run(); }
    )K");
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("import template private native handles and nested result callables use lexical types",
          "[import][template][ffi][private-dependency][type-provenance]") {
    TmpKdi lib(R"K(
        module tplnativeorigin;
        namespace io {
            public struct NativeIoHandle {}
            public struct NativeIoOperation {}
            private struct NativePrivateHandle {}
            struct NativeAliasIoError { code: int; }
            template<typename T, typename E>
            struct Result { value: T; error: E; }
            public template<typename T> alias IoResult : Result<T, NativeAliasIoError>;
            public template<typename T> alias IoCompletion : !(IoResult<T>);
            @k::ffi::Extern("C", null, "__k_private_native_handle")
            private __private_handle(handle: NativePrivateHandle*): NativePrivateHandle*;
            @k::ffi::Extern("C", null, "__k_private_native_callback")
            private __private_callback(handle: NativePrivateHandle*, callback: *(IoResult<int>));
            @k::ffi::Extern("C", null, "__k_native_operation")
            private __private_operation(): NativeIoOperation*;
            @k::ffi::Extern("C", null, "__k_native_operation")
            private __private_operation_alias(): NativeIoOperation*;
            @k::ffi::Extern("C", null, "__k_native_submit")
            private __private_submit(operation: NativeIoOperation*): int;
            template<typename T>
            prepare(handle: NativeIoHandle*, completion: IoCompletion<T>): IoResult<T> {
                native: NativePrivateHandle* = __private_handle(null);
                __private_callback(native, null);
                operation: NativeIoOperation* = __private_operation();
                same: NativeIoOperation* = __private_operation_alias();
                __private_submit(operation);
                result: IoResult<T>;
                return result;
            }
            template<typename T>
            prepareAgain(handle: NativeIoHandle*): IoResult<T> {
                __private_handle(null);
                result: IoResult<T>;
                return result;
            }
        }
    )K", true);
    auto file = kdi::kdi_read_cbor_file(lib.kdi_path);
    const auto io_it = std::find_if(file.unit.root_ns.namespaces.begin(), file.unit.root_ns.namespaces.end(),
        [](const auto& ns) { return ns.name == "io"; });
    REQUIRE(io_it != file.unit.root_ns.namespaces.end());
    const auto& io = *io_it;
    REQUIRE(std::none_of(io.functions.begin(), io.functions.end(),
        [](const auto& fn) { return fn.name.starts_with("__private_"); }));
    REQUIRE(std::none_of(io.aggregates.begin(), io.aggregates.end(),
        [](const auto& agg) { return agg.name == "NativePrivateHandle"; }));
    size_t templates = 0;
    for (const auto& definition : io.template_defs) {
        if (definition.name != "prepare" && definition.name != "prepareAgain") continue;
        ++templates;
        INFO(definition.source);
        CHECK(definition.source.find("namespace ::") == std::string::npos);
        test_logger logger;
        k::source source{definition.source};
        k::parse::parser parser(logger, source);
        REQUIRE(parser.parse_unit());
        REQUIRE_FALSE(logger.has_error());
        CHECK(definition.source.find("private struct NativePrivateHandle") != std::string::npos);
        CHECK(definition.source.find("tplnativeorigin::io::NativePrivateHandle*") != std::string::npos);
        if (definition.name == "prepare") {
            CHECK(definition.source.find("callback : *(") != std::string::npos);
            CHECK(definition.source.find("::tplnativeorigin::io::Result<int, ::tplnativeorigin::io::NativeAliasIoError>")
                  != std::string::npos);
            CHECK(definition.source.find("Result__") == std::string::npos);
        }
    }
    REQUIRE(templates == 2);
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplnativeorigin", lib.kdi_path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    compiler->parse_source("nativeconsumer.k", R"K(
        module tplnativeconsumer;
        import tplnativeorigin;
        namespace consumer {
        struct NativeIoHandle { wrong: long; }
        struct NativeIoOperation { wrong: long; }
        struct NativePrivateHandle { wrong: long; }
        struct NativeAliasIoError { wrong: long; }
        @k::ffi::Extern("C", null, "__k_native_submit")
        private submit(operation: tplnativeorigin::io::NativeIoOperation*): int;
        test(handle: tplnativeorigin::io::NativeIoHandle*): int {
            result: tplnativeorigin::io::IoResult<int> = tplnativeorigin::io::prepare<int>(
                handle, [](result: tplnativeorigin::io::IoResult<int>) {});
            again: tplnativeorigin::io::IoResult<long> = tplnativeorigin::io::prepareAgain<long>(handle);
            submit(null);
            return 0;
        }
        }
        runNativeConsumer(): int { return consumer::test(null); }
    )K");
    REQUIRE(compiler->verify_gen_code());
    auto origin = compiler->get_unit()->get_root_namespace()->get_child_namespace("tplnativeorigin")->get_child_namespace("io");
    auto ctx = compiler->get_context_for_test();
    size_t checked = 0;
    for (const auto& child : origin->get_children()) {
        auto fn = std::dynamic_pointer_cast<k::model::function>(child);
        if (!fn || !fn->is_instantiation() || !fn->get_short_name().starts_with("prepare")) continue;
        ++checked;
        CHECK(fn->parameters().front()->get_type()->get_subtype() == origin->get_aggregate("NativeIoHandle")->get_struct_type());
        REQUIRE(fn->get_template_definition_scope());
        auto native = fn->get_template_definition_scope()->get_function("__private_handle");
        REQUIRE(native);
        REQUIRE(native->get_return_type());
        CHECK(native->parameters().front()->get_type()->get_subtype() == origin->get_aggregate("NativePrivateHandle")->get_struct_type());
        CHECK(native->get_return_type()->get_subtype() == origin->get_aggregate("NativePrivateHandle")->get_struct_type());
        if (fn->get_tpl_args().front().type_arg == ctx->from_string("int")) {
            REQUIRE(fn->get_existing_block());
            auto local_native = fn->get_existing_block()->get_variable("native");
            REQUIRE(local_native);
            CHECK(local_native->get_type()->get_subtype()
                  == origin->get_aggregate("NativePrivateHandle")->get_struct_type());
            auto scope = fn->get_template_definition_scope();
            auto operation = scope->get_function("__private_operation");
            auto alias = scope->get_function("__private_operation_alias");
            REQUIRE(operation);
            REQUIRE(alias);
            auto submit = scope->get_function("__private_submit");
            REQUIRE(submit);
            REQUIRE(operation->get_return_type());
            REQUIRE(alias->get_return_type());
            auto expected = origin->get_aggregate("NativeIoOperation")->get_struct_type();
            CHECK(operation->get_return_type()->get_subtype() == expected);
            CHECK(alias->get_return_type()->get_subtype() == expected);
            REQUIRE(submit->parameters().front()->get_type());
            CHECK(submit->parameters().front()->get_type()->get_subtype() == expected);
            auto native_llvm = ctx->lookup_llvm_function(operation);
            REQUIRE(native_llvm);
            CHECK(ctx->lookup_llvm_function(alias) == native_llvm);
            CHECK(native_llvm == ctx->module().getFunction("__k_native_operation"));
            CHECK(ctx->module().getFunction("__k_native_operation.1") == nullptr);
            auto consumer = compiler->get_unit()->get_root_namespace()->get_child_namespace("consumer");
            REQUIRE(consumer);
            auto consumer_submit = consumer->get_function("submit");
            REQUIRE(consumer_submit);
            auto submit_llvm = ctx->lookup_llvm_function(submit);
            REQUIRE(submit_llvm);
            CHECK(ctx->lookup_llvm_function(consumer_submit) == submit_llvm);
            CHECK(ctx->module().getFunction("__k_native_submit.1") == nullptr);
        }
    }
    CHECK(checked == 2);
    private_handle_calls = private_callback_calls = private_operation_calls = private_submit_calls = 0;
    private_native_errors = 0;
    llvm::sys::DynamicLibrary::AddSymbol("__k_private_native_handle",
        reinterpret_cast<void*>(&__k_private_native_handle));
    llvm::sys::DynamicLibrary::AddSymbol("__k_private_native_callback",
        reinterpret_cast<void*>(&__k_private_native_callback));
    llvm::sys::DynamicLibrary::AddSymbol("__k_native_operation",
        reinterpret_cast<void*>(&__k_native_operation));
    llvm::sys::DynamicLibrary::AddSymbol("__k_native_submit",
        reinterpret_cast<void*>(&__k_native_submit));
    std::string load_error;
    const auto load_failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        (std::string(KLANG_STDLIB_LIB_DIR) + "/libk.so").c_str(), &load_error);
    INFO(load_error);
    REQUIRE_FALSE(load_failed);
    // Imported non-template constructors belong to the producer, not the consumer JIT.
    const auto producer_load_failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        lib.so_path.c_str(), &load_error);
    INFO(load_error);
    REQUIRE_FALSE(producer_load_failed);
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("runNativeConsumer");
    REQUIRE(run);
    int result = -1;
    REQUIRE_NOTHROW(result = run());
    CHECK(result == 0);
    CHECK(private_handle_calls == 2);
    CHECK(private_callback_calls == 1);
    CHECK(private_operation_calls == 2);
    CHECK(private_submit_calls == 2);
    CHECK(private_native_errors == 0);
}

TEST_CASE("import template execute preserves every same-named overload",
          "[import][template][lambda][owner][overload][regression]") {
    const bool priority_first = GENERATE(false, true);
    INFO("priority overload declared first: " << priority_first);
    const std::string priority_definition = R"K(
        template<typename R>
        execute(endpoint: int, task: !():R, priority: int): R { return task(); }
    )K";
    const std::string ordinary_definition = R"K(
        template<typename R>
        execute(endpoint: int, task: !():R): R { return execute<R>(endpoint, task, 0); }
    )K";
    TmpKdi lib("module tploverloadedexecute;\n"
        + (priority_first ? priority_definition + ordinary_definition
                          : ordinary_definition + priority_definition), true);
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tploverloadedexecute", lib.kdi_path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    compiler->parse_source("overloadconsumer.k", R"K(
        module overloadconsumer;
        import tploverloadedexecute;
        run(): int {
            first: int = tploverloadedexecute::execute<int>(0, []():int { return 20; });
            return first + tploverloadedexecute::execute<int>(0, []():int { return 22; }, 7);
        }
    )K");
    REQUIRE(compiler->verify_gen_code());
    auto origin = compiler->get_unit()->get_root_namespace()->get_child_namespace("tploverloadedexecute");
    REQUIRE(origin);
    CHECK(origin->get_functions("execute").size() == 2);
    auto concrete = origin->get_functions("execute__int");
    REQUIRE(concrete.size() == 2);
    CHECK(concrete[0]->get_parameter_size() != concrete[1]->get_parameter_size());
    CHECK_FALSE(concrete[0]->get_mangled_name().empty());
    CHECK_FALSE(concrete[1]->get_mangled_name().empty());
    CHECK(concrete[0]->get_mangled_name() != concrete[1]->get_mangled_name());
    for (const auto& instance : concrete) {
        CHECK(instance->get_fq_name() == "::tploverloadedexecute::execute__int");
    }
    std::string load_error;
    const auto load_failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        (std::string(KLANG_STDLIB_LIB_DIR) + "/libk.so").c_str(), &load_error);
    INFO(load_error);
    REQUIRE_FALSE(load_failed);
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    int result = 0;
    REQUIRE_NOTHROW(result = run());
    CHECK(result == 42);
}

TEST_CASE("import template — private callback preludes preserve native ABI and privacy",
          "[import][template][ffi][private-dependency][callable][cstring][throws]") {
    TmpKdi lib(R"K(
        module tplprivatecallbacks;
        @k::ffi::Extern("C", null, "__k_private_callback_import")
        private __private_callbacks(
            invoke : *(byte*, byte*), drop : *(byte*),
            borrowed : &(int):int throws(k::Exception), owned : !(int):int,
            @k::ffi::CString text : const char*) : int throws(k::Exception);

        template<typename T>
        privateCallbacks(invoke : *(byte*, byte*), drop : *(byte*),
                         borrowed : &(int):int throws(k::Exception), owned : !(int):int,
                         text : const char*) : int throws(k::Exception) {
            return __private_callbacks(invoke, drop, borrowed, owned, text);
        }
        template<typename T>
        privateCallbacksAgain(invoke : *(byte*, byte*), drop : *(byte*),
                              borrowed : &(int):int throws(k::Exception), owned : !(int):int,
                              text : const char*) : int throws(k::Exception) {
            return __private_callbacks(invoke, drop, borrowed, owned, text);
        }
    )K", true);
    auto file = kdi::kdi_read_cbor_file(lib.kdi_path);
    auto check_export = [](const kdi::kdi_namespace& root) {
        size_t count = 0;
        std::function<void(const kdi::kdi_namespace&)> check = [&](const auto& ns) {
            REQUIRE(std::none_of(ns.functions.begin(), ns.functions.end(),
                [](const auto& fn) { return fn.name == "__private_callbacks"; }));
            for (const auto& definition : ns.template_defs) {
                if (definition.name != "privateCallbacks"
                    && definition.name != "privateCallbacksAgain") continue;
                ++count;
                INFO(definition.source);
                test_logger logger;
                k::source source{definition.source};
                k::parse::parser parser(logger, source);
                auto ast = parser.parse_unit();
                REQUIRE(ast);
                REQUIRE_FALSE(logger.has_error());
                REQUIRE(ast->declarations.size() == 2);
                REQUIRE(definition.source.find("invoke : *(byte*, byte*)") != std::string::npos);
                REQUIRE(definition.source.find("drop : *(byte*)") != std::string::npos);
                REQUIRE(definition.source.find("borrowed : &(int) : int throws(::k::Exception)")
                        != std::string::npos);
                REQUIRE(definition.source.find("owned : !(int) : int") != std::string::npos);
                REQUIRE(definition.source.find("@k::ffi::CString text : const char*")
                        != std::string::npos);
                REQUIRE(definition.source.find(".symbol = \"__k_private_callback_import\"")
                        != std::string::npos);
                REQUIRE(definition.function_signature);
                REQUIRE(definition.function_signature->name == definition.name);
                REQUIRE(definition.function_signature->params.size() == 5);
                const auto& owned = definition.function_signature->params[3].type.value;
                REQUIRE(std::holds_alternative<kdi::kdi_callable_type>(owned));
                REQUIRE(std::get<kdi::kdi_callable_type>(owned).addresser
                        == kdi::kdi_callable_addresser::owner);
            }
            for (const auto& child : ns.namespaces) check(child);
        };
        check(root);
        REQUIRE(count == 2);
    };
    check_export(file.unit.root_ns);

    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplprivatecallbacks", lib.kdi_path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    compiler->set_file_resolver(resolver);
    REQUIRE_NOTHROW(compiler->parse_source("consumer.k", R"K(
        module tplprivatecallbackconsumer;
        import tplprivatecallbacks;
        invokeCalls: int = 0;
        dropCalls: int = 0;
        invoke(context : byte*, result : byte*) { ++invokeCalls; }
        drop(context : byte*) { ++dropCalls; }
        identity(x: int): int { return x; }
        instantiate() : int throws(k::Exception) {
            base : int = 40;
            borrowed : &(int):int throws(k::Exception) =
                [base](x : int) : int { return base + x; };
            owned : !(int):int = [base](x : int) : int { return base + x; };
            return privateCallbacks<int>(invoke, drop, borrowed, owned, null);
        }
        runCallbacks(): int throws(k::Exception) {
            invokeCalls = 0;
            dropCalls = 0;
            borrowed: &(int):int throws(k::Exception) = identity;
            owned: !(int):int = identity;
            result: int = privateCallbacks<int>(invoke, drop, borrowed, owned, null);
            againBorrowed: &(int):int throws(k::Exception) = identity;
            againOwned: !(int):int = identity;
            again: int = privateCallbacksAgain<long>(
                invoke, drop, againBorrowed, againOwned, null);
            return result == 42 && again == 42 && invokeCalls == 2 && dropCalls == 2 ? 42 : -1;
        }
    )K"));
    auto root = compiler->get_unit()->get_root_namespace();
    auto origin = root->get_child_namespace("tplprivatecallbacks");
    REQUIRE(origin);
    auto definition = origin->get_function("privateCallbacks");
    auto second = origin->get_function("privateCallbacksAgain");
    REQUIRE(definition);
    REQUIRE(second);
    CHECK_FALSE(root->get_function("privateCallbacks"));
    CHECK_FALSE(root->get_function("__private_callbacks"));
    REQUIRE(definition->get_tpl_info());
    REQUIRE(second->get_tpl_info());
    auto scope = definition->get_tpl_info()->private_definition_scope;
    REQUIRE(scope);
    REQUIRE(scope == second->get_tpl_info()->private_definition_scope);
    auto dependencies = scope->get_functions("__private_callbacks");
    REQUIRE(dependencies.size() == 1);
    auto dependency = dependencies.front();
    REQUIRE(dependency->get_visibility() == k::model::PRIVATE);
    REQUIRE(dependency->get_extern_c_symbol().has_value());
    REQUIRE(dependency->get_extern_c_symbol().value() == "__k_private_callback_import");
    size_t bound = 0;
    for (const auto& child : origin->get_children()) {
        auto fn = std::dynamic_pointer_cast<k::model::function>(child);
        if (!fn || !fn->is_instantiation() || !fn->get_short_name().starts_with("privateCallbacks")) continue;
        CHECK(fn->get_template_definition_scope() == scope);
        std::vector<k::model::function_invocation_expression*> calls;
        collect_in_stmt(fn->get_existing_block().get(), calls);
        REQUIRE(calls.size() == 1);
        auto symbol = std::dynamic_pointer_cast<k::model::symbol_expression>(calls.front()->callee_expr());
        REQUIRE(symbol);
        CHECK(symbol->get_function() == dependency);
        ++bound;
    }
    CHECK(bound == 2);
    REQUIRE(dependency->get_throws_spec().size() == 1);
    REQUIRE(dependency->parameters().size() == 5);
    REQUIRE(dependency->parameters()[4]->is_ffi_cstring());
    const auto addressers = {k::model::callable_type::addresser::pointer,
                            k::model::callable_type::addresser::pointer,
                            k::model::callable_type::addresser::reference,
                            k::model::callable_type::addresser::owner};
    size_t index = 0;
    for (auto addresser : addressers) {
        auto callable = std::dynamic_pointer_cast<k::model::callable_type>(
            dependency->parameters()[index++]->get_type());
        REQUIRE(callable);
        REQUIRE(callable->get_addresser() == addresser);
        if (index == 3) REQUIRE(callable->get_throws().size() == 1);
        auto layout = llvm::dyn_cast<llvm::StructType>(callable->get_llvm_type());
        REQUIRE(layout);
        REQUIRE(layout->getNumElements() == 2);
    }

    k::model::kdi_builder exporter(*compiler->get_context_for_test());
    compiler->get_unit()->accept(exporter);
    auto reexported = exporter.take();
    check_export(reexported.unit.root_ns);

    REQUIRE(compiler->verify_gen_code());
    std::string load_error;
    const auto load_failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(
    INFO(load_error);
    REQUIRE_FALSE(load_failed);
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("runCallbacks");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("import generic — signature-only template_def is materialised into model",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericimport;

        generic<typename T>
        struct Box {
            public value : T&;
            relay(v : T&) : T& { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplgenericimport", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import tplgenericimport;
    )K"));

    auto root_ns = comp->get_unit()->get_root_namespace();
    REQUIRE(root_ns != nullptr);
    auto imported_ns = root_ns->get_child_namespace("tplgenericimport");
    REQUIRE(imported_ns != nullptr);

    auto box_tpl = imported_ns->get_aggregate("Box");
    REQUIRE(box_tpl != nullptr);
    REQUIRE(box_tpl->is_template());
    REQUIRE(box_tpl->is_generic());
    REQUIRE(box_tpl->get_tpl_info() != nullptr);
    REQUIRE(box_tpl->get_tpl_info()->is_imported_signature_only);
    REQUIRE(box_tpl->get_variable("value") != nullptr);
    REQUIRE(box_tpl->get_function("relay") != nullptr);
}

TEST_CASE("import generic<class> — pointer placeholder is preserved in signature metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericownermeta;

        class Dog {
        }

        generic<class T>
        class Box {
            relay(v : T*) : T* { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name != "Box") continue;
            REQUIRE(td.is_generic);
            REQUIRE(td.aggregate_signature != nullptr);
            REQUIRE(td.aggregate_signature->methods.size() == 1);

            const auto& method = td.aggregate_signature->methods[0];
            REQUIRE(std::holds_alternative<kdi::kdi_ptr_type>(method.return_type.value));
            auto& ret_inner = *std::get<kdi::kdi_ptr_type>(method.return_type.value).inner;
            REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(ret_inner.value));
            REQUIRE(std::get<kdi::kdi_template_param_ref>(ret_inner.value).name == "T");

            REQUIRE(method.params.size() == 1);
            REQUIRE(std::holds_alternative<kdi::kdi_ptr_type>(method.params[0].type.value));
            auto& param_inner = *std::get<kdi::kdi_ptr_type>(method.params[0].type.value).inner;
            REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(param_inner.value));
            REQUIRE(std::get<kdi::kdi_template_param_ref>(param_inner.value).name == "T");
            found_def = true;
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };

    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import generic<class> — nested aggregate template_def keeps generic metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericclassmeta;

        generic<class T>
        class Box {
            private struct Node {
                public value : T&;
            }

        public:
            public head : Node*;
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Box") {
                REQUIRE(td.is_generic);
                REQUIRE(td.source.empty());
                REQUIRE(td.aggregate_signature != nullptr);
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("cross-module generic function — pointer parameter works after KDI import",
          "[import][e2e][template][generic]") {
    auto result = build_exec_with_lib(
        R"K(
            module generic_owner_lib;

            class Dog {
            }

            generic<class T>
            is_not_null(v : T*) : int {
                if (v == null) {
                    return 0;
                }
                return 42;
            }
        )K",
        R"K(
            module generic_owner_exe;
            import generic_owner_lib;

            main() : int {
                d : generic_owner_lib::Dog;
                return generic_owner_lib::is_not_null<generic_owner_lib::Dog>(&d);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("cross-module generic class — erased synthesis is exported even if the library never instantiates it",
          "[import][e2e][template][generic]") {
    auto result = build_exec_with_lib(
        R"K(
            module generic_class_lib;

            public:
            class Seed {
                public id : int = 3;
            }

            generic<class T>
            class Holder {
                private val : T*;
                public:
                Holder() { val = null; }
                set(v : T*) { val = v; }
                has() : bool { return val != null; }
                get() : T* { return val; }
            }
        )K",
        R"K(
            module generic_class_exe;
            import generic_class_lib;

            class Dog {
                public n : int = 40;
            }

            main() : int {
                d : Dog;
                s : generic_class_lib::Seed;
                a : Holder<Dog>;
                b : Holder<generic_class_lib::Seed>;
                a.set(&d);
                b.set(&s);
                pd : Dog* = a.get();
                ps : generic_class_lib::Seed* = b.get();
                if (!a.has()) return 100;
                return pd->n + ps->id;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 43);
}

TEST_CASE("cross-module generic method of a non-generic class — explicit and deduced arguments",
          "[import][e2e][template][generic]") {
    auto result = build_exec_with_lib(
        R"K(
            module generic_method_lib;

            public:
            class Seed {
                public id : int = 3;
            }

            class Host {
                public:
                generic<class T> pass(v : T*) : T* { return v; }
                generic<class T> pick(a : T*, b : T*) : T* { return b; }
            }
        )K",
        R"K(
            module generic_method_exe;
            import generic_method_lib;

            main() : int {
                d : Seed;
                h : Host;
                a : Seed* = h.pass<Seed>(&d);
                b : Seed* = h.pass(&d);
                c : Seed* = h.pick(a, b);
                return c->id;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 3);
}

TEST_CASE("import generic class — erased synthesis is exported with erased parameter types",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericerased;

        public:
        generic<class T>
        class Box {
            private item : T*;
            public:
            Box() { item = null; }
            put(v : T*) { item = v; }
            take() : T* { return item; }
        }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found = false;
    for (const auto& agg : kdi.unit.root_ns.aggregates) {
        if (agg.name != "Box") continue;
        found = true;
        REQUIRE(agg.methods.size() == 2);
        for (const auto& method : agg.methods) {
            const kdi::kdi_type& t = method.name == "put" ? method.params[0].type : method.return_type;
            REQUIRE(std::holds_alternative<kdi::kdi_ptr_type>(t.value));
            auto& inner = *std::get<kdi::kdi_ptr_type>(t.value).inner;
            REQUIRE(std::holds_alternative<kdi::kdi_erased_param_type>(inner.value));
        }
    }
    REQUIRE(found);
}

// ─────────────────────────────────────────────────────────────────────────────
// Template with value parameter: verify template_origin includes value args.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — template_origin with value parameter metadata",
          "[import][template][model]") {
    TmpKdi lib(R"K(
        module tplvalmeta;

        template<typename T, int N>
        struct ValHolder {
            val : T;
            get_n() : int { return N; }
        }

        use_it() : int {
            h : ValHolder<int, 10>;
            return h.get_n();
        }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    // Look for a concrete aggregate with template_origin that has a value arg
    bool found_origin = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& agg : ns.aggregates) {
            if (agg.template_origin.has_value() &&
                agg.template_origin->base_name == "ValHolder") {
                REQUIRE(agg.template_origin->args.size() == 2);
                // First arg: type (int)
                REQUIRE(agg.template_origin->args[0].type_arg.has_value());
                auto& targ = std::get<kdi::kdi_int_type>(
                    agg.template_origin->args[0].type_arg->value);
                REQUIRE(targ.bits == 32);
                // Second arg: value (10)
                REQUIRE(agg.template_origin->args[1].value_arg.has_value());
                REQUIRE(*agg.template_origin->args[1].value_arg == "10");
                found_origin = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_origin);
}

// ═════════════════════════════════════════════════════════════════════════════
// Cross-module template definition + instantiation tests (Phase 1)
//
// In Phase 1, a library defines templates and instantiates them internally.
// The concrete instances are exported in the KDI as regular entities (with
// template_origin metadata).  A consumer module imports the library and uses
// the instantiations.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-basic] Basic struct template: lib defines + instantiates,
// consumer uses via wrapper.
//
// lib:  template<typename T> struct Holder { val: T; }
//       set_and_get(v: int) : int  — wraps Holder<int>
// exe:  main() → set_and_get(42) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — basic struct template via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module holder_lib;

            template<typename T>
            struct Holder {
                val : T;
            }

            set_and_get(v : int) : int {
                h : Holder<int>;
                h.val = v;
                return h.val;
            }
        )K",
        R"K(
            module holder_exe;
            import holder_lib;

            main() : int {
                return holder_lib::set_and_get(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-basic] Basic function template: lib defines + instantiates,
// consumer calls the concrete wrapper.
//
// lib:  template<typename T> add(a: T, b: T) : T { return a + b; }
//       add_ints(a: int, b: int) : int  — wraps add<int>
// exe:  main() → add_ints(17, 25) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — basic function template via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module adder_lib;

            template<typename T>
            add(a : T, b : T) : T {
                return a + b;
            }

            add_ints(a : int, b : int) : int {
                return add<int>(a, b);
            }
        )K",
        R"K(
            module adder_exe;
            import adder_lib;

            main() : int {
                return adder_lib::add_ints(17, 25);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-multiple-instantiations] Two distinct instantiations of the same
// template in the same lib, consumer exercises both via separate wrappers.
//
// lib:  template<typename T> struct Cell { val: T; }
//       cell_int(v: int) : int    — wraps Cell<int>
//       cell_long(v: long) : long — wraps Cell<long>
// exe:  main() → cell_int(10) + cell_long(32) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — multiple instantiations of same template",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module cell_lib;

            template<typename T>
            struct Cell {
                val : T;
            }

            cell_int(v : int) : int {
                c : Cell<int>;
                c.val = v;
                return c.val;
            }

            cell_long(v : long) : long {
                c : Cell<long>;
                c.val = v;
                return c.val;
            }
        )K",
        R"K(
            module cell_exe;
            import cell_lib;

            main() : int {
                r1 : int;
                r1 = cell_lib::cell_int(10);
                r2 : long;
                r2 = cell_lib::cell_long(32);
                return r1 + r2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-method] Template struct with methods across import boundary.
// Consumer calls wrappers that exercise member functions on concrete instances.
//
// NOTE: Member variable access via 'this.' in template method bodies is a
// known limitation. These tests use external field access and value params.
//
// lib:  template<typename T, int N>
//       struct Acc { val: T; get_n() : int { return N; } }
//       accumulate(a: int, b: int, c: int) : int
// exe:  main() → accumulate(10, 20, 12) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct with methods via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module acc_lib;

            template<typename T, int N>
            struct Acc {
                val : T;
                get_n() : int { return N; }
            }

            accumulate(a : int, b : int, c : int) : int {
                acc : Acc<int, 0>;
                acc.val = a + b + c;
                return acc.val + acc.get_n();
            }
        )K",
        R"K(
            module acc_exe;
            import acc_lib;

            main() : int {
                return acc_lib::accumulate(10, 20, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-value-param] Template with value parameter across import boundary.
//
// lib:  template<int N>
//       struct Fixed { get() : int { return N; } }
// exe:  main() → Fixed<42>.get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer instantiates with value parameter",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module val_tpl_lib;

            template<int N>
            struct Fixed {
                get() : int { return N; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module val_tpl_consumer;
            import val_tpl_lib;

            main() : int {
                f : Fixed<42>;
                return f.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [consumer-inst-mixed-params] Template with both type and value parameters
// across import boundary.
//
// lib:  template<typename T, int Scale>
//       scaled(x: T) : T { return x * Scale; }
//       test_scaled() : int
// exe:  main() → test_scaled() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer instantiates with mixed type and value params",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module mixed_tpl_lib;

            template<typename T, int Scale>
            scaled(x : T) : T {
                return x * Scale;
            }

            test_scaled() : int {
                return scaled<int, 6>(7);
            }
        )K",
        R"K(
            module mixed_tpl_exe;
            import mixed_tpl_lib;

            main() : int {
                return mixed_tpl_lib::test_scaled();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 7 * 6 = 42
}



TEST_CASE("cross-module template declaration",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module point_lib;

            template<typename T>
            struct Point {
                x : T;
                y : T;
                sum() : T {
                    return x + y;
                }
            }

            test_int_point() : int {
                pt : Point<int>{.x = 2, .y = 3};
                return pt.sum();
            }
        )K",
        R"K(
            module point_exe;
            import point_lib;

            main() : int {
                pt : Point<short>{.x = 5, .y = 7};
                return point_lib::test_int_point() + pt.sum();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 17 );  // 2 + 3 + 5 + 7 = 17
}

// ═════════════════════════════════════════════════════════════════════════════
// Using-alias de-resolution in template KDI export
//
// These tests verify that when a template body references a type through a
// `using` alias, the KDI export (model-based source reconstruction) emits the
// fully-qualified name so the importing module can resolve it without having
// the original `using` directive.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Struct template whose member type is aliased via `using` outside the template.
//
// lib:
//   namespace inner { struct Coord { x: int; y: int; } }
//   using Pt = inner::Coord;
//   template<typename T> struct Wrapper { pos: Pt; val: T; }
//
// exe: Wrapper<int> → set pos.x, pos.y, val → return pos.x + pos.y + val
//
// Without model-based reconstruction, the KDI would contain "Pt" which the
// consumer cannot resolve. With the emitter, it should contain "inner::Coord".
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct member type aliased via using",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_tpl_lib;

            namespace inner {
                struct Coord {
                    x : int;
                    y : int;
                }
            }

            using Pt = inner::Coord;

            template<typename T>
            struct Wrapper {
                pos : Pt;
                val : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_tpl_exe;
            import alias_tpl_lib;

            main() : int {
                w : Wrapper<int>;
                w.pos.x = 10;
                w.pos.y = 20;
                w.val = 12;
                return w.pos.x + w.pos.y + w.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Function template whose parameter type is aliased via `using`.
//
// lib:
//   namespace types { struct Pair { a: int; b: int; } }
//   using P = types::Pair;
//   template<typename T> sum_pair(p: P, extra: T) : int { return p.a + p.b + extra; }
//
// exe: sum_pair<int>(Pair{10,20}, 12) → 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — function param type aliased via using",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_fn_lib;

            namespace types {
                struct Pair {
                    a : int;
                    b : int;
                }
            }

            using P = types::Pair;

            template<typename T>
            sum_pair(p : P, extra : T) : int {
                return p.a + p.b + extra;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_fn_exe;
            import alias_fn_lib;

            main() : int {
                p : types::Pair;
                p.a = 10;
                p.b = 20;
                return sum_pair<int>(p, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Function template whose return type is aliased via `using`.
//
// lib:
//   namespace data { struct Result { val: int; } }
//   using Res = data::Result;
//   template<typename T> extract(r: Res, extra: T) : int { return r.val + extra; }
//
// exe: create Result, call extract<int>(r, 10) → 42+10 = 52
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — function param type aliased via using (struct)",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_ret_lib;

            namespace data {
                struct Result {
                    val : int;
                }
            }

            using Res = data::Result;

            template<typename T>
            extract(r : Res, extra : T) : int {
                return r.val + extra;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_ret_exe;
            import alias_ret_lib;

            main() : int {
                r : alias_ret_lib::data::Result;
                r.val = 42;
                return alias_ret_lib::extract<int>(r, 10);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 52 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Struct template with a member whose type is aliased and a method that uses it.
//
// lib:
//   namespace aux { struct Acc { total: int; } }
//   using Accum = aux::Acc;
//   template<typename T>
//   struct Adder {
//       val : Accum;
//       extra : T;
//       sum() : int { return val.total + extra; }
//   }
//
// exe: Adder<int> with val.total=30, extra=12 → sum() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — member type aliased via using with method",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_body_lib;

            namespace aux {
                struct Acc {
                    total : int;
                }
            }

            using Accum = aux::Acc;

            template<typename T>
            struct Adder {
                val : Accum;
                extra : T;
                sum() : int { return val.total + extra; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_body_exe;
            import alias_body_lib;

            main() : int {
                d : Adder<int>;
                d.val.total = 30;
                d.extra = 12;
                return d.sum();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with a member whose type is resolved via `using namespace`.
//
// lib:
//   namespace base { struct Elem { val: int; } }
//   using namespace base;
//   template<typename T>
//   struct Container {
//       item : Elem;
//       extra : T;
//       total() : int { return item.val + extra; }
//   }
//
// exe: Container<int> with item.val=30, extra=12 → total() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — member type resolved via using namespace",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_base_lib;

            namespace base {
                struct Elem {
                    val : int;
                }
            }

            using namespace base;

            template<typename T>
            struct Container {
                item : Elem;
                extra : T;
                total() : int { return item.val + extra; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_base_exe;
            import alias_base_lib;

            main() : int {
                c : Container<int>;
                c.item.val = 30;
                c.extra = 12;
                return c.total();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Additional cross-module template coverage
//
// These tests complete Phase 1 coverage for cross-module template instantiation
// scenarios: class templates, default parameters, constructors, direct function
// template calls, and type constraints.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-class] Template class across module boundary.
// Consumer instantiates a template class from the library and calls a method.
//
// lib:  template<typename T>
//       class Box { val: T; public get() : T { return val; } set(v: T) { val = v; } }
// exe:  Box<int> → set(42) → get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — class template consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module cls_tpl_lib;

            template<typename T>
            class Box {
                public val : T;
                Box() {}
                public get() : T { return val; }
                public set(v : T) { val = v; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module cls_tpl_exe;
            import cls_tpl_lib;

            main() : int {
                b : Box<int>();
                b.set(42);
                return b.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("cross-module template — preserve library-promoted virtual base layout",
          "[import][e2e][template][cross-tpl][virtual-base-layout]") {
    auto result = build_exec_with_lib(
        R"K(
            module promoted_virtual_tpl_lib;

            template<typename T>
            public class Root {
                public value : T;
                public Root(initial : T) : value(initial) {}
            }

            template<typename T>
            public class Left : public Root<T> {
                public ownValue : T;
                public Left() : Root(1), ownValue(42) {}
            }
            template<typename T>
            public class Right : public Root<T> {
                public Right() : Root(2) {}
            }
            template<typename T>
            public class Diamond : public Left<T>, public Right<T> {
                public Diamond() : Root(3) {}
            }

            public instantiateDiamond() : int {
                diamond : Diamond<int>();
                return 0;
            }
            public read(left : Left<int>&) : int { return left.ownValue; }
        )K",
        R"K(
            module promoted_virtual_tpl_exe;
            import promoted_virtual_tpl_lib;

            main() : int {
                left : Left<int>();
                return read(left);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("cross-module template — canonical filter layout preserves borrowed and owned roots",
          "[import][e2e][template][cross-tpl][virtual-base-layout][private-template-layout][regression]") {
    auto result = build_exec_with_lib(
        R"K(
            module canonical_filter_tpl_lib;

            template<typename T>
            public interface Source {
                read() : T;
                close();
            }

            template<typename T>
            public class Filter : public Source<T> {
            protected:
                _borrowed : Source<T>*;
                final canAdopt(candidate : Source<T>*) : bool {
                    return candidate != null && _borrowed == candidate && _owned == null;
                }
            private:
                _owned : Source<T>!;
            public:
                Filter(input : Source<T>*) : _borrowed(input), _owned(null) {}
                Filter() : _borrowed(null), _owned(null) {}
                final probe(candidate : Source<T>*) : bool { return canAdopt(candidate); }
                final adopt(input : Source<T>!) { _owned = input; }
                override read() : T { return _borrowed->read(); }
                override close() { _borrowed->close(); }
            }

            template<typename T>
            public class Other : public Source<T> {
                public override read() : T { return (T) 0; }
                public override close() {}
            }

            template<typename T>
            public class Diamond : public Filter<T>, public Other<T> {
                public override read() : T { return (T) 0; }
                public override close() {}
            }

            public instantiateDiamond() {
                diamond : Diamond<byte>;
            }
            public libraryProbe(filter : Filter<byte>&, candidate : Source<byte>*) : bool {
                return filter.probe(candidate);
            }
            public libraryRead(source : Source<byte>*) : byte { return source->read(); }
            public libraryClose(source : Source<byte>*) { source->close(); }
        )K",
        R"K(
            module canonical_filter_tpl_exe;
            import canonical_filter_tpl_lib;

            rootDestructions : int = 0;
            filterDestructions : int = 0;
            intercepts : int = 0;
            rootCloses : int = 0;
            filterCloses : int = 0;

            class Root : public Source<byte> {
                public override read() : byte { return (byte) 42; }
                public override close() { ++rootCloses; }
                ~Root() { ++rootDestructions; }
            }
            class Decorator : public Filter<byte> {
                public Decorator(input : Source<byte>*) : Filter(input) {}
                public valid(candidate : Source<byte>*) : bool { return canAdopt(candidate); }
                public override read() : byte {
                    ++intercepts;
                    return _borrowed->read();
                }
                public override close() {
                    ++filterCloses;
                    _borrowed->close();
                }
                ~Decorator() { ++filterDestructions; }
            }

            main() : int {
                {
                    root : Root! = new Root();
                    candidate : Source<byte>* = &*root;
                    filter : Decorator(candidate);
                    if (!filter.valid(candidate) || !libraryProbe(filter, candidate)) return 1;
                    view : Source<byte>* = &filter;
                    if (view->read() != (byte) 42) return 21;
                    if (intercepts != 1) return 22;
                    if (libraryRead(view) != (byte) 42) return 23;
                    if (intercepts != 2) return 24;
                    filter.adopt((Source<byte>!) root);
                    if (root != null || filter.valid(candidate) || libraryProbe(filter, candidate)) return 3;
                    if (rootDestructions != 0 || filterDestructions != 0) return 4;
                    libraryClose(view);
                    if (rootCloses != 1 || filterCloses != 1) return 7;
                }
                if (rootDestructions != 1 || filterDestructions != 1) return 5;
                if (rootCloses != 1 || filterCloses != 1) return 8;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("cross-module template — nested imported return arguments keep their declaration identity",
          "[import][e2e][template][cross-tpl][regression][canonical-nested-return]") {
    auto result = build_exec_with_lib(
        R"K(
            module canonical_nested_return_lib;
            template<typename K, typename V>
            public interface Entry {
                const key() : const K&;
                const value() : const V&;
            }
            template<typename T>
            public struct Holder {
                public present : bool;
                public Holder() : present(false) {}
            }
            template<typename K, typename V>
            public interface Ordered {
                const first() : Holder<Entry<K,V>>;
                const last() : Holder<Entry<K,V>>;
            }
            template<typename K, typename V>
            public class LocalMap : public Ordered<K,V> {
                public const first() : Holder<Entry<K,V>> {
                    result : Holder<Entry<K,V>>;
                    return result;
                }
                public const last() : Holder<Entry<K,V>> {
                    result : Holder<Entry<K,V>>;
                    return result;
                }
            }
            public libraryFirst(map : const Ordered<int,int>&) : Holder<Entry<int,int>> {
                return map.first();
            }
            public libraryLast(map : const Ordered<int,int>&) : Holder<Entry<int,int>> {
                return map.last();
            }
        )K",
        R"K(
            module canonical_nested_return_exe;
            import canonical_nested_return_lib;
            main() : int {
                map : LocalMap<int,int>;
                first : Holder<Entry<int,int>> = map.first();
                last : Holder<Entry<int,int>> = map.last();
                importedFirst : Holder<Entry<int,int>> = libraryFirst(map);
                importedLast : Holder<Entry<int,int>> = libraryLast(map);
                if (first.present || last.present || importedFirst.present || importedLast.present)
                    return 1;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("cross-module template — locally instantiated map recipes retain lexical return types",
          "[import][e2e][template][cross-tpl][regression][lexical-nested-return]") {
    auto result = build_exec_with_lib(
        R"K(
            module lexical_map_lib;
            template<typename K, typename V>
            public struct Entry {
                public key : K;
                public value : V;
            }
            template<typename T>
            public struct OptionalHolder {
                public value : const T*;
                public OptionalHolder() : value(null) {}
            }
            template<typename K, typename V>
            public class SourceMap {
                public const first() : OptionalHolder<Entry<K,V>> {
                    result : ::lexical_map_lib::OptionalHolder<::lexical_map_lib::Entry<K,V>>;
                    return result;
                }
                public const last() : ::lexical_map_lib::OptionalHolder<::lexical_map_lib::Entry<K,V>> {
                    result : OptionalHolder<Entry<K,V>>;
                    return result;
                }
            }
        )K",
        R"K(
            module lexical_map_consumer;
            import lexical_map_lib;
            main() : int {
                map : lexical_map_lib::SourceMap<int,int>;
                first : lexical_map_lib::OptionalHolder<lexical_map_lib::Entry<int,int>> = map.first();
                last : ::lexical_map_lib::OptionalHolder<::lexical_map_lib::Entry<int,int>> = map.last();
                if (first.value != null || last.value != null) return 1;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("cross-module template — empty ListMap nested returns preserve imported Entry identity",
          "[import][template][regression][listmap-lexical-return]") {
    auto comp = compile_model_with_stdlib(R"K(
        module listmap_lexical_consumer;
        main() : int {
            map : ListMap<int,int>;
            first : OptionalConstRef<Entry<int,int>> = map.first();
            last : ::k::OptionalConstRef<::k::Entry<int,int>> = map.last();
            if (first.hasValue() || last.hasValue()) return 1;
            return 42;
        }
    )K");
    REQUIRE(comp);
    auto map = find_aggregate(comp, "ListMap__int__int");
    REQUIRE(map);
    for (const auto* method : {"first", "last"}) {
        auto fn = map->get_function(method);
        REQUIRE(fn);
        auto holder_type = std::dynamic_pointer_cast<k::model::struct_type>(fn->get_return_type());
        REQUIRE(holder_type);
        auto holder = holder_type->get_struct();
        REQUIRE(holder);
        REQUIRE(holder->get_tpl_args().size() == 1);
        auto entry_type = std::dynamic_pointer_cast<k::model::struct_type>(
            holder->get_tpl_args().front().type_arg);
        REQUIRE(entry_type);
        REQUIRE(entry_type->get_struct());
        REQUIRE(entry_type->get_struct()->get_fq_name() == "::k::Entry__int__int");
        REQUIRE(holder->get_fq_name().find("listmap_lexical_consumer") == std::string::npos);
    }
}

TEST_CASE("cross-module template — canonical stream signatures retain overloads and template returns",
          "[import][e2e][template][cross-tpl][regression][canonical-stream-signatures]") {
    auto result = build_exec_with_lib(
        R"K(
            module canonical_stream_signature_lib;
            template<typename T>
            public struct Packet {
                public value : T;
                public Packet(value : T) : value(value) {}
            }
            template<typename T>
            public interface Input {
                read() : Packet<T>;
                read(values : T[]) : int;
                read(values : T[], off : unsigned int, len : unsigned int) : int;
            }
            template<typename T>
            public interface Output {
                write(value : T) : Output<T>&;
                write(values : const T[]) : Output<T>&;
                write(values : const T[], off : unsigned int, len : unsigned int) : Output<T>&;
                flush() : Output<T>&;
            }
            public readPacket(input : Input<byte>&) : Packet<byte> { return input.read(); }
            public readAll(input : Input<byte>&, values : byte[]) : int {
                return input.read(values) + input.read(values, 0u, 1u);
            }
            public writeAll(output : Output<byte>&, values : const byte[]) {
                output.write((byte) 42);
                output.write(values);
                output.write(values, 0u, 1u);
                output.flush();
            }
        )K",
        R"K(
            module canonical_stream_signature_exe;
            import canonical_stream_signature_lib;
            reads : int = 0;
            writes : int = 0;
            flushes : int = 0;
            class Reader : public Input<byte> {
                public override read() : Packet<byte> {
                    ++reads;
                    result : Packet<byte>((byte) 42);
                    return result;
                }
                public override read(values : byte[]) : int {
                    ++reads;
                    return 17;
                }
                public override read(values : byte[], off : unsigned int, len : unsigned int) : int {
                    ++reads;
                    return 25;
                }
            }
            class Writer : public Output<byte> {
                public override write(value : byte) : Output<byte>& {
                    writes += 1;
                    return this;
                }
                public override write(values : const byte[]) : Output<byte>& {
                    writes += 10;
                    return this;
                }
                public override write(values : const byte[], off : unsigned int, len : unsigned int)
                        : Output<byte>& {
                    writes += 100;
                    return this;
                }
                public override flush() : Output<byte>& {
                    ++flushes;
                    return this;
                }
            }
            main() : int {
                reader : Reader;
                writer : Writer;
                values : byte[2];
                packet : Packet<byte> = readPacket(reader);
                if (packet.value != (byte) 42 || reads != 1) return 1;
                if (readAll(reader, values) != 42 || reads != 3) return 2;
                writeAll(writer, values);
                if (writes != 111 || flushes != 1) return 3;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-default-params] Template with default type parameter.
// Consumer uses `<>` syntax to rely on the default.
//
// lib:  template<typename T = int>
//       struct DefaultBox { val: T; }
// exe:  DefaultBox<> → val = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — default type parameter with <> syntax",
          "[import][e2e][template][cross-tpl][consumer-inst][defaults]") {
    auto result = build_exec_with_lib(
        R"K(
            module def_tpl_lib;

            template<typename T = int>
            struct DefaultBox {
                val : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module def_tpl_exe;
            import def_tpl_lib;

            main() : int {
                b : DefaultBox<>;
                b.val = 42;
                return b.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-default-partial] Template with mixed params, consumer supplies only
// the first and relies on the default for the second.
//
// lib:  template<typename T, int N = 10>
//       struct SizedVal { val: T; get_size() : int { return N; } }
// exe:  SizedVal<int> → val=32, get_size()=10, sum=42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — partial default params across modules",
          "[import][e2e][template][cross-tpl][consumer-inst][defaults]") {
    auto result = build_exec_with_lib(
        R"K(
            module partial_def_lib;

            template<typename T, int N = 10>
            struct SizedVal {
                val : T;
                get_size() : int { return N; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module partial_def_exe;
            import partial_def_lib;

            main() : int {
                s : SizedVal<int>;
                s.val = 32;
                return s.val + s.get_size();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 32 + 10 = 42
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-direct] Consumer directly calls a template function from the
// library (not via a wrapper).
//
// lib:  template<typename T>
//       identity(x: T) : T { return x; }
// exe:  main() → identity<int>(42) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls template function directly",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_direct_lib;

            template<typename T>
            identity(x : T) : T {
                return x;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_direct_exe;
            import fn_direct_lib;

            main() : int {
                return identity<int>(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-multi-type] Consumer directly calls a multi-type-param template
// function from the library.
//
// lib:  template<typename T, typename U>
//       add_cast(a: T, b: U) : int { return a + b; }
// exe:  main() → add_cast<int, short>(30, 12) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls multi-param template function",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_multi_lib;

            template<typename T, typename U>
            add_cast(a : T, b : U) : int {
                return a + b;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_multi_exe;
            import fn_multi_lib;

            main() : int {
                return add_cast<int, short>(30, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-ctor] Template struct with constructor across module boundary.
// Consumer instantiates the template and uses the constructor.
//
// lib:  template<typename T>
//       struct Wrap { val: T; Wrap(v: T) : val(v) {} get() : T { return val; } }
// exe:  Wrap<int>(42) → get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct with constructor consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module ctor_tpl_lib;

            template<typename T>
            struct Wrap {
                val : T;
                Wrap(v : T) : val(v) {}
                get() : T { return val; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module ctor_tpl_exe;
            import ctor_tpl_lib;

            main() : int {
                w : Wrap<int>(42);
                return w.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-two-consumers] Two different consumer instantiations of the same
// template from one library, verifying that distinct types are produced.
//
// lib:  template<typename T>
//       struct Val { data: T; }
// exe:  Val<int> and Val<short> — set different values and sum
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer creates two distinct instantiations",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module two_inst_lib;

            template<typename T>
            struct Val {
                data : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module two_inst_exe;
            import two_inst_lib;

            main() : int {
                vi : Val<int>;
                vi.data = 30;
                vs : Val<short>;
                vs.data = 12;
                return vi.data + vs.data;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 30 + 12 = 42
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-value-param-direct] Consumer directly calls a template function
// with a value parameter from the library.
//
// lib:  template<int N>
//       offset(x: int) : int { return x + N; }
// exe:  main() → offset<2>(40) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls value-param template function",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_val_lib;

            template<int N>
            offset(x : int) : int {
                return x + N;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_val_exe;
            import fn_val_lib;

            main() : int {
                return offset<2>(40);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-intrinsic] Template struct with @Intrinsic-annotated constructor,
// destructor, and methods across module boundary. Consumer instantiates the
// template with its own type and calls intrinsic methods.
//
// lib:  template<typename T> struct Slot { @Intrinsic ctor/dtor/construct/destruct }
// exe:  main() → Slot<Widget>.construct(); .get().v = 42; .destruct(); return 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — intrinsic UniSlot consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst][intrinsic]") {
    auto result = build_exec_with_lib(
        R"K(
            module intrlib;

            namespace annotations {
                annotation Intrinsic {
                    name : int;
                }
            }

            template<typename T>
            struct Slot {
                private:
                _slot : T;

                public:
                @annotations::Intrinsic(0)
                Slot();

                @annotations::Intrinsic(0)
                ~Slot();

                @annotations::Intrinsic(1)
                construct();

                @annotations::Intrinsic(2)
                destruct();

                get() : T& { return _slot; }
            }

            // Force at least one lib-side instantiation for linkage
            lib_test() : int {
                s : Slot<int>;
                s.construct();
                s.get() = 77;
                result : int = s.get();
                s.destruct();
                return result;
            }
        )K",
        R"K(
            module intrexec;
            import intrlib;

            struct Widget {
                v : int;
                Widget() { v = 42; }
            }

            main() : int {
                s : Slot<Widget>;
                s.construct();
                result : int = s.get().v;
                s.destruct();
                return result;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-intrinsic][member-template] Template struct with member template
// method (variadic pack) and @Intrinsic annotation across module boundary.
// This mirrors the real UniSlot<T>::construct<Args...>(Args...args) pattern.
//
// lib:  template<typename T> struct Slot { template<typename...Args> construct(Args...args); }
// exe:  main() → Slot<Point>.construct<int,int>(10,32); return .get().x + .get().y
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — intrinsic member template with variadic pack",
          "[import][e2e][template][cross-tpl][consumer-inst][intrinsic][member-template]") {
    auto result = build_exec_with_lib(
        R"K(
            module mtlib;

            namespace annotations {
                annotation Intrinsic {
                    name : int;
                }
            }

            template<typename T>
            struct Slot {
                private:
                _slot : T;

                public:
                @annotations::Intrinsic(0)
                Slot();

                @annotations::Intrinsic(0)
                ~Slot();

                @annotations::Intrinsic(1)
                template<typename...Args>
                construct(Args...args);

                @annotations::Intrinsic(2)
                destruct();

                get() : T& { return _slot; }
            }

            // Force at least one lib-side instantiation for linkage
            lib_test() : int {
                s : Slot<int>;
                s.construct();
                s.get() = 77;
                result : int = s.get();
                s.destruct();
                return result;
            }
        )K",
        R"K(
            module mtexec;
            import mtlib;

            struct Point {
                x : int;
                y : int;
                Point(ax : int, ay : int) { x = ax; y = ay; }
            }

            main() : int {
                s : Slot<Point>;
                s.construct<int, int>(10, 32);
                result : int = s.get().x + s.get().y;
                s.destruct();
                return result;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-enum-default] Template with enum value parameter and default value
// exported to KDI and instantiated in consumer module.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — enum value parameter with default across modules",
          "[import][e2e][template][cross-tpl][consumer-inst][enum][value-param]") {
    auto result = build_exec_with_lib(
        R"K(
            module enum_val_lib;

            enum Mode { Slow = 10; Fast = 40; Turbo = 50; }

            template<Mode M = Mode::Fast>
            struct SpeedController {
                get_speed() : int { return M; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module enum_val_exe;
            import enum_val_lib;

            main() : int {
                c_default : SpeedController<>;
                c_explicit : SpeedController<Mode::Slow>;
                return c_default.get_speed() + c_explicit.get_speed();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 50 ); // 40 + 10 = 50
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-constexpr-default] Template with unparenthesized constexpr binary default
// exported to KDI and instantiated in consumer module.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — unparenthesized binary default across modules",
          "[import][e2e][template][cross-tpl][consumer-inst][value-param][default]") {
    auto result = build_exec_with_lib(
        R"K(
            module constexpr_def_lib;

            template<int N = 20 + 22>
            struct ConfigHolder {
                get_val() : int { return N; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module constexpr_def_exe;
            import constexpr_def_lib;

            main() : int {
                c : ConfigHolder<>;
                return c.get_val();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [homonym-imports] Known limitation — two *imported* templates with the same
// short name from different modules.
//
// A consumer imports two libraries, each exporting a top-level template named
// `Box`. It instantiates each, qualified, as `boxa::Box<int>` and
// `boxb::Box<int>`, which have different layouts (1 vs 2 fields) and different
// behaviour. Expected: 21 + (10 + 10) = 41.
//
// CURRENT LIMITATION: imported templates are re-injected by flattening them into
// the consumer's *root* namespace (the `module <ns>;`-rename trick in
// `kdi_importer::materialise_template_def`). This is required so that imported
// top-level symbols stay reachable *unqualified* (`import lib;` behaves like
// `using namespace lib;`). As a side effect, two homonymous imported templates
// collide on their short name in root and get merged — the second silently wins.
//
// The instantiation `struct_type` *identity* registry is already collision-safe
// (origin-qualified key, see `[template][instantiation][ns-collision]`); the
// remaining gap is the model-level *symbol* clash. Fixing it cleanly requires a
// deeper change: scope lookup must resolve unqualified imported symbols from
// their origin namespaces (an `import`-as-`using-namespace` mechanism) so the
// flatten can be dropped and homonymous imports require qualification.
// Tracked in TODO.md.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Known-limitation: homonymous imported templates from different modules",
          "[.][import][template][homonym-imports]") {
    SKIP("Two imported templates with the same short name from different modules "
         "still clash at the model/symbol level because imported templates are "
         "flattened into the consumer root for unqualified access. Requires "
         "import-as-using-namespace scope lookup; tracked in TODO.md.");

    std::vector<LibSpec> libs = {
        { R"K(
            module boxa;
            template<typename T>
            struct Box {
                v : T;
            public:
                Box(x : T) { v = x; }
                const get() : T { return v; }
            }
        )K" },
        { R"K(
            module boxb;
            template<typename T>
            struct Box {
                v1 : T;
                v2 : T;
            public:
                Box(x : T) { v1 = x; v2 = x; }
                const sum() : T { return v1 + v2; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module box_exec;
            import boxa;
            import boxb;
            main() : int {
                ba : boxa::Box<int>(21);
                bb : boxb::Box<int>(10);
                return ba.get() + bb.sum();   // 21 + (10 + 10) = 41
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 41 );
}


// ═════════════════════════════════════════════════════════════════════════════
// TEMPLATE INSTANTIATION DIAMOND (libk templates, linkonce_odr + COMDAT)
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared] Diamond instantiation of a libk template.
//
//                 libk: Optional<T>
//                /                  \
//   optdiamond_a (Optional<int>)   optdiamond_b (Optional<int>)
//                \                  /
//            optdiamond_exec (Optional<int>)   <- imports both A and B
//
// Each of the two libraries and the executable instantiate ::k::Optional<int>
// independently (k is auto-imported). Every instantiation symbol is synthesised
// under its origin-absolute name (::k::Optional<int>::...) and emitted
// linkonce_odr + COMDAT, so the shared-library diamond links without
// duplicate-symbol errors and the dynamic linker interposes a single copy at
// load (default visibility).
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - Optional<int> from libk in two libs + exe",
          "[import][e2e][instantiation-diamond-shared]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module optdiamond_a;
            makeOptA() : int {
                v : int = 40;
                z : int = 0;
                o : Optional<int>;
                o.set(v);
                return o.getOr(z);
            }
        )K" },
        { R"K(
            module optdiamond_b;
            makeOptB() : int {
                v : int = 2;
                z : int = 0;
                o : Optional<int>;
                o.set(v);
                return o.getOr(z);
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module optdiamond_exec;
            import optdiamond_a;
            import optdiamond_b;
            main() : int {
                z : int = 0;
                o : Optional<int>;
                return makeOptA() + makeOptB() + o.getOr(z);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared] Same diamond for the two-parameter Expected<R,E>.
//
// Two libraries and the executable each instantiate ::k::Expected<int,int>.
// Verifies that multi-parameter libk instantiations dedup the same way across a
// shared-library diamond.
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - Expected<int,int> from libk in two libs + exe",
          "[import][e2e][instantiation-diamond-shared]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module expdiamond_a;
            makeExpA() : int {
                v : int = 40;
                e : Expected<int, int>;
                e.setResult(v);
                return e.getResult();
            }
        )K" },
        { R"K(
            module expdiamond_b;
            makeExpB() : int {
                v : int = 2;
                e : Expected<int, int>;
                e.setResult(v);
                return e.getResult();
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module expdiamond_exec;
            import expdiamond_a;
            import expdiamond_b;
            main() : int {
                v : int = 0;
                e : Expected<int, int>;
                e.setResult(v);
                return makeExpA() + makeExpB() + e.getResult();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared][user-template] Diamond instantiation of a
// USER-DECLARED (non-libk) struct template.
//
//                 boxlib: Box<T>           (user library — NOT libk)
//                /                  \
//   boxdiamond_a (Box<int>)   boxdiamond_b (Box<int>)
//                \                  /
//            boxdiamond_exec (Box<int>)   <- imports boxlib + both A and B
//
// The COMDAT/linkonce_odr dedup of template instantiations is module-agnostic:
// it keys off the template's origin module (set by the KDI importer for ANY
// imported template) and the is_instantiation()/is_template() flags, never on
// libk specifically. This test proves it for a template declared in a plain
// user library: A, B and the executable each synthesise their own
// ::boxlib::Box<int> under the same origin-absolute name, emitted linkonce_odr
// + COMDAT, so the shared-library diamond links without duplicate-symbol errors
// and the dynamic linker interposes a single copy at load (default visibility).
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - user-declared Box<int> template in two libs + exe",
          "[import][e2e][instantiation-diamond-shared][user-template]") {
    std::vector<LibSpec> libs = {
        // lib[0]: the template-defining library (the template's true origin).
        { R"K(
            module boxlib_01;

            template<typename T>
            struct Box {
                val : T;
            }
        )K" },
        // lib[1]: imports boxlib and instantiates ::boxlib::Box<int> itself.
        { R"K(
            module boxdiamond_a;
            import boxlib_01;
            makeBoxA() : int {
                b : boxlib_01::Box<int>;
                b.val = 40;
                return b.val;
            }
        )K" },
        // lib[2]: imports boxlib and instantiates ::boxlib::Box<int> itself.
        { R"K(
            module boxdiamond_b;
            import boxlib_01;
            makeBoxB() : int {
                b : boxlib_01::Box<int>;
                b.val = 2;
                return b.val;
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module boxdiamond_exec;
            import boxlib_01;
            import boxdiamond_a;
            import boxdiamond_b;
            main() : int {
                b : boxlib_01::Box<int>;
                b.val = 0;
                return makeBoxA() + makeBoxB() + b.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-static] STATIC-LINK diamond of a libk template.
//
// FIXED. The static-archive variant of the diamond above (link the executable
// against two static archives instead of two .so files) used to fail to link
// with "multiple definition" errors on secondary (base/interface) vtable
// globals (named `<vtable>_for_<BaseName>`) re-emitted by every consumer of a
// libk/user template that implements a base interface or reaches a shared
// virtual base. These globals were always created with plain ExternalLinkage,
// unlike the primary vtable/RTTI globals which already got the merge-friendly
// linkonce_odr + COMDAT treatment for template instantiations.
//
// Fixed in gen_class.cpp: secondary vtable globals now go through the same
// `should_merge_aggregate_symbols()` + `apply_instantiation_linkage()` path as
// the primary vtable/RTTI globals.
//
// This scenario (both a libk-template diamond and a user-declared-template
// diamond, statically linked) is now covered by real, non-skipped, passing
// tests in test-klangc-static-diamond.cpp:
//   - "klangc: static-link diamond of a libk template instantiation"
//   - "klangc: static-link diamond of a user-declared template instantiation"
// ─────────────────────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────────────────────
// [import][mangling][enum-param] Cross-module overloads differing only by an enum
// parameter.
//
// The mangling half is FIXED: mangler::mangle_type() used to return an empty encoding for
// enumerations, so both overloads were recorded in the .kdi under the very same mangled
// name while the .so actually exported '<sym>' and '<sym>.1' (LLVM auto-uniquification,
// invisible to the KDI). Both overloads now get distinct symbols — asserted below.
//
// The remaining half is a KNOWN LIMITATION (skipped): an unqualified call to an imported
// function is bound eagerly by symbol_resolver to the first matching imported overload and
// never reaches type_reference_resolver::get_best_matching_function, so the consumer still
// calls the ErrA overload for an ErrB argument. See TODO.md, "Unqualified calls to imported
// functions bypass overload resolution".
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Imported overloads differing only by an enum parameter have distinct symbols",
          "[import][mangling][enum-param]") {
    auto comp = compile_model(R"K(
        module enumovl;
        public enum ErrA { a1 = 1; a2 = 2; }
        public enum ErrB { b1 = 7; b2 = 8; }
        public f(x : ErrA) : int { return 100; }
        public f(x : ErrB) : int { return 200; }
    )K");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto overloads = root_ns->get_functions("f");
    REQUIRE(overloads.size() == 2);
    CHECK_FALSE(overloads[0]->get_mangled_name().empty());
    CHECK_FALSE(overloads[1]->get_mangled_name().empty());
    CHECK(overloads[0]->get_mangled_name() != overloads[1]->get_mangled_name());
    // No symbol may carry LLVM's '.N' uniquification suffix: those never reach the KDI.
    CHECK(overloads[0]->get_mangled_name().find('.') == std::string::npos);
    CHECK(overloads[1]->get_mangled_name().find('.') == std::string::npos);
}

TEST_CASE("Known-limitation: cross-module overload selection on an imported enum argument",
          "[.][import][mangling][enum-param]") {
    auto result = build_exec_with_lib(R"K(
        module enumovl2;
        public enum ErrA { a1 = 1; a2 = 2; }
        public enum ErrB { b1 = 7; b2 = 8; }
        public f(x : ErrA) : int { return 100; }
        public f(x : ErrB) : int { return 200; }
    )K", R"K(
        module main;
        import enumovl2;
        main() : int {
            if (f(enumovl2::ErrA::a1) != 100) return 1;
            if (f(enumovl2::ErrB::b1) != 200) return 2;
            return 0;
        }
    )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 0 );
}

// ═════════════════════════════════════════════════════════════════════════════
// CROSS-MODULE VIRTUAL / EXCEPTION REGRESSIONS
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [import-virtual-void] Overriding a void-returning virtual method declared in
// an imported interface / class.
//
// build_vtable_layout() runs before `void` return types are normalised to
// nullptr, so a locally-declared `: void` method carried an unresolved_type
// named "void" while its imported counterpart carried nullptr. The signatures
// were reported as different and the override was rejected with error 00177.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import virtual — local class overrides a void-returning imported virtual",
          "[import][e2e][import-class][import-virtual-void]") {
    auto result = build_exec_with_lib(
        R"K(
            module voidvirt_lib;

            public interface Sink {
                accept(v: int) : void;
            }

            public class Emitter {
            public:
                Emitter() {}
                reset() : void {}
                emit(s: Sink+, v: int) : void { s->accept(v); }
            }
        )K",
        R"K(
            module exec_voidvirt;
            import voidvirt_lib;

            total : int = 0;

            class Collector : public voidvirt_lib::Sink {
            public:
                Collector() {}
                override accept(v: int) : void { total += v; }
            }

            class ResetCounter : public voidvirt_lib::Emitter {
            public:
                ResetCounter() {}
                override reset() : void { total += 100; }
            }

            main() : int {
                c : Collector! = new Collector();
                e : voidvirt_lib::Emitter;
                e.emit(c, 7);
                r : ResetCounter! = new ResetCounter();
                r->reset();
                return total;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 107 );   // 7 + 100
}

TEST_CASE("import virtual — local class implements imported template interface with self-return",
          "[import][e2e][import-class][import-virtual-template-interface]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplvirt_lib;

            template<typename T>
            public interface Out {
                write(v: T) : Out<T>&;
                close() : Out<T>&;
            }
        )K",
        R"K(
            module exec_tplvirt;
            import tplvirt_lib;

            class CounterOut : public tplvirt_lib::Out<byte> {
            public:
                sum : int = 0;
                isClosed : bool = false;

                CounterOut() {}

                override write(v: byte) : tplvirt_lib::Out<byte>& {
                    sum += v;
                    return this;
                }

                override close() : tplvirt_lib::Out<byte>& {
                    isClosed = true;
                    return this;
                }
            }

            main() : int {
                out : CounterOut! = new CounterOut();
                out->write(5);
                out->close();
                return out->sum + (out->isClosed ? 100 : 0);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 105 );   // 5 + 100
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-exception] An exception thrown inside a library must be catchable by
// the importing module, including by one of its base classes.
//
// The RTTI global of an imported class used to be re-emitted in the importing
// module as a `linkonce_odr` definition instead of an external declaration.
// Exception dispatch matches typeinfo by pointer identity, so each module ended
// up comparing its own copy of the symbol. It only ever worked because ELF
// symbol interposition happened to collapse the copies back into one.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import exception — exception thrown in a library is caught by the exe",
          "[import][e2e][import-exception]") {
    auto result = build_exec_with_lib(
        R"K(
            module exclib;

            public class LibError : public Exception {
            public:
                LibError(code: int) : Exception(code) {}
            }

            public boom(code: int) : int throws(LibError) {
                throw LibError(code);
            }
        )K",
        R"K(
            module exec_exc;
            import exclib;

            main() : int {
                direct : int = 0;
                try {
                    exclib::boom(5);
                } catch (e : exclib::LibError&) {
                    direct = e.getCode();
                }

                base : int = 0;
                try {
                    exclib::boom(6);
                } catch (e : Exception&) {
                    base = e.getCode();
                }

                return direct + base;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 11 );   // 5 + 6
}

// ═════════════════════════════════════════════════════════════════════════════
// [import-template-symbols] A template defined in a library and instantiated in
// the importing module must still resolve the symbols its body references from
// the library: free functions, static methods and module-level constants.
//
// Regression: template bodies are cloned by template_instantiator *after* the
// symbol_resolver pass, so the clone was resolved against the importing unit
// only, where those library symbols exist as `imported_*` nodes that neither
// scope_lookup::lookup_functions nor the symbol resolver ever consulted.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("import template — instantiated body resolves imported functions and constants",
          "[import][e2e][import-template-symbols]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplsym;

            public const BONUS : int = 5;

            public triple(v : int) : int { return v * 3; }

            public struct Helper {
                public static offset() : int { return 2; }
            }

            template<typename T>
            public struct Calc {
                public compute(v : int) : int {
                    // imported free function + imported static method + imported constant
                    return triple(v) + Helper::offset() + BONUS;
                }
            }
        )K",
        R"K(
            module main;
            import tplsym;
            using tplsym;

            main() : int {
                c : Calc<int>;
                return c.compute(5);   // 15 + 2 + 5 = 22
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 22 );
}

// ═════════════════════════════════════════════════════════════════════════════
// [import-struct-indirect-member] A struct member whose type is a pointer (or
// any other addresser) to an *imported* aggregate must be fully resolved.
//
// Regression: context::resolve_struct_type used to record the field with its
// still-unresolved pointee when the imported name was not yet bound (that only
// happens in the later aggregate_type_resolver pass). Because the LLVM body was
// set during that first attempt, the early-return at the top of
// resolve_struct_type then kept the stale field record forever, and any member
// access through it failed with "the left-hand side is a reference to
// '<<unresolved:X>>' which is not a struct".
//
// The bug only showed up when *no* by-value member of an imported aggregate was
// present: such a member made the first attempt bail out, so the struct was
// rebuilt later with correct field types — which is why the two shapes below
// are both exercised.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("import struct — indirect members targeting an imported aggregate resolve",
          "[import][e2e][import-struct-indirect-member]") {
    auto result = build_exec_with_lib(
        R"K(
            module boxlib_02;

            public struct Payload {
                public value : int;
                public get() : int { return value; }
            }

            public struct Marker {
                public tag : int;
            }
        )K",
        R"K(
            module main;
            import boxlib_02;
            using boxlib_02;

            // Only indirect members: nothing forces an early re-resolution pass.
            public struct OnlyIndirect {
                p : Payload*;
            }

            // Mixed shape: a by-value imported member alongside indirect ones.
            public struct Mixed {
                m : Marker;
                p : Payload*;
            }

            readOnly(s: OnlyIndirect*) : int { return s->p->get(); }
            readMixed(s: Mixed*) : int { return s->p->get() + s->m.tag; }

            main() : int {
                payload : Payload;
                payload.value = 20;

                a : OnlyIndirect;
                a.p = &payload;

                b : Mixed;
                b.m.tag = 2;
                b.p = &payload;

                return readOnly(&a) + readMixed(&b);   // 20 + (20 + 2) = 42
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Exported aliasing across a module boundary — 'alias' and 'typedef'
// ═════════════════════════════════════════════════════════════════════════════
