/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Tests for K language function redirectors (-> target;).
 *
 * Step 2: model-level representation
 * Step 3+: resolution, type checking, codegen, vtable interaction
 */

#include <catch2/catch_all.hpp>

#include "../src/model/model_builder.hpp"
#include "../src/model/tools/kdi_exporter.hpp"
#include "../src/errors.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/Module.h>

#include "helpers.hpp"

using namespace k::model;
namespace ast = k::parse::ast;

TEST_CASE("Redirect semantic signatures reject representation-only matches",
          "[redirect][semantic-signature][error][regression]") {
    const char* declaration = GENERATE(
        "target(x : int&) {} aliasFn(x : int*) -> target;",
        "target(x : int+) {} aliasFn(x : int*) -> target;",
        "target(x : int?) {} aliasFn(x : int+) -> target;",
        "target(x : int&) {} aliasFn(x : int#) -> target;",
        "target(x : unsigned int) {} aliasFn(x : int) -> target;",
        "target(x : int*) {} aliasFn(x : long*) -> target;",
        "target(x : int*) {} aliasFn(x : const int*) -> target;",
        "target(x : int, y : int) {} aliasFn(x : int) -> target;",
        "target() : int { return 1; } aliasFn() : unsigned int -> target;",
        "target() : int { return 1; } aliasFn() : void -> target;",
        "target() : int { return 1; } aliasFn() -> target;",
        "target(values : int[]) {} aliasFn(values... : long) -> target;",
        "target(values... : int) {} aliasFn(values : unsigned int[]) -> target;",
        "target(callback : &():int) {} aliasFn(callback : !():int) -> target;",
        "struct Item {} target(x : Item&) {} aliasFn(x : Item!) -> target;",
        "struct Item {} target() : Item! { return null; } aliasFn() : Item* -> target;",
        "typedef First : int; typedef Second : int; target(x : First) {} aliasFn(x : Second) -> target;",
        "union First { value : int; } union Second { value : int; } target(x : First&) {} aliasFn(x : Second&) -> target;",
        "template<typename T> typedef Token : int; target(x : Token<int>) {} aliasFn(x : Token<long>) -> target;",
        "struct Item { target() {} const aliasFn() -> target; }",
        "struct Other { target() {} } struct Item { aliasFn() -> Other::target; }",
        "struct Item { aliasFn() -> target; } target(x : Item*) {}",
        "struct Item { target() {} } aliasFn(x : const Item&) -> Item::target;",
        "target(x : int&) {} middle(x : int*) -> target; aliasFn(x : int&) -> middle;"
    );
    INFO(declaration);
    auto compiler = k::compiler::create();
    try {
        compiler->parse_source("redirect-semantic-signature.k",
            std::string("module k; ") + declaration, true, false);
        FAIL("Expected a semantic redirect signature diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_INCOMPATIBLE_SIG));
    }
}

TEST_CASE("Redirect semantic signatures enforce exception coverage",
          "[redirect][semantic-signature][exceptions][regression]") {
    const char* declaration = GENERATE(
        "target() throws(::k::Exception) {} aliasFn() -> target;",
        "target() {} middle() throws(::k::Exception) -> target; aliasFn() -> middle;"
    );
    INFO(declaration);
    try {
        gen_jit_throws(std::string("module redirect_throws; ") + declaration);
        FAIL("Expected a redirect exception contract diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_INCOMPATIBLE_SIG));
    }
    auto compiler = compile_model_with_stdlib(R"SRC(
        module redirect_throws_widening;
        target() {}
        aliasFn() throws(::k::Exception) -> target;
        checkedTarget() throws(::k::Exception) {}
        coveredAlias() throws(::k::Exception) -> checkedTarget;
    )SRC");
    REQUIRE(compiler);
    auto root = compiler->get_unit()->get_root_namespace();
    REQUIRE(root->get_function("aliasFn"));
    REQUIRE(root->get_function("coveredAlias"));
    CHECK(root->get_function("aliasFn")->get_throws_spec().size() == 1);
    CHECK(root->get_function("coveredAlias")->get_throws_spec().size() == 1);
}

TEST_CASE("Redirect semantic signatures preserve aliases defaults and target disambiguation",
          "[redirect][semantic-signature][regression]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module redirect_defaults;
        alias Number : int;
        test() : int { return TargetName(); }
        testDirect() : int { return aliasFn(); }
        testOther() : int { return OtherName(); }
        testChain() : int { return ChainName(); }
        testExplicit() : int { return TargetName(6); }
        testProvider() : int { return target(); }
        target(value : long) : long { return value; }
        target(value : int = 5) : int { return value + 1; }
        aliasFn(renamed : Number = 41) : Number -> target(int);
        otherAlias(independent : Number = 20) : Number -> target(int);
        chainAlias(chainValue : Number = 10) : Number -> aliasFn;
        alias TargetName : aliasFn;
        alias OtherName : otherAlias;
        alias ChainName : chainAlias;
    )SRC");
    REQUIRE(compiler);
    auto root = compiler->get_unit()->get_root_namespace();
    auto alias = root->get_function("aliasFn");
    auto other = root->get_function("otherAlias");
    auto chain = root->get_function("chainAlias");
    REQUIRE(alias);
    REQUIRE(other);
    REQUIRE(chain);
    auto provider = alias->get_redirect_target();
    REQUIRE(provider);
    CHECK(provider->get_parameter(0)->get_short_name() == "value");
    CHECK(alias->get_parameter(0)->get_short_name() == "renamed");
    CHECK(other->get_parameter(0)->get_short_name() == "independent");
    CHECK(chain->get_parameter(0)->get_short_name() == "chainValue");
    CHECK(alias->get_parameter(0)->has_default_expr());
    CHECK(other->get_parameter(0)->has_default_expr());
    CHECK(chain->get_parameter(0)->has_default_expr());
    CHECK(other->get_redirect_target() == provider);
    CHECK(chain->get_redirect_target() == provider);
    auto& module = compiler->get_context_for_test()->module();
    auto alias_entry = module.getNamedAlias(alias->get_mangled_name());
    REQUIRE(alias_entry);
    CHECK(alias_entry->getAliaseeObject() == module.getFunction(provider->get_mangled_name()));
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
    auto direct = jit->lookup_symbol<int(*)()>("testDirect");
    auto independent = jit->lookup_symbol<int(*)()>("testOther");
    auto chained = jit->lookup_symbol<int(*)()>("testChain");
    auto explicit_arg = jit->lookup_symbol<int(*)()>("testExplicit");
    auto provider_default = jit->lookup_symbol<int(*)()>("testProvider");
    REQUIRE(direct);
    REQUIRE(independent);
    REQUIRE(chained);
    REQUIRE(explicit_arg);
    REQUIRE(provider_default);
    CHECK(direct() == 42);
    CHECK(independent() == 21);
    CHECK(chained() == 11);
    CHECK(explicit_arg() == 7);
    CHECK(provider_default() == 6);
}

TEST_CASE("Redirect source calls do not borrow provider defaults",
          "[redirect][semantic-signature][error][regression]") {
    const char* call = GENERATE("aliasFn()", "AliasName()", "AliasName(1, 2)");
    INFO(call);
    try {
        gen_jit_throws(std::string(R"SRC(
            module k;
            target(value : int = 41) : int { return value + 1; }
            aliasFn(required : int) : int -> target;
            alias AliasName : aliasFn;
            test() : int { return )SRC") + call + "; }");
        FAIL("Expected source redirect argument validation");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH));
    }
}

TEST_CASE("Redirect semantic signatures preserve unified member and free prototypes",
          "[redirect][semantic-signature][receiver][regression]") {
    auto jit = gen_jit(R"SRC(
        module redirect_unified_receiver;
        struct Item {
            value : int = 21;
            read() : int -> readItem;
            original() : int { return value; }
        }
        readItem(object : Item&) : int { return object.value; }
        freeAlias(object : Item&) : int -> Item::original;
        chainAlias(object : Item&) : int -> Item::read;
        test() : int {
            object : Item;
            return object.read() + freeAlias(object) + chainAlias(object);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 63);
}

TEST_CASE("Redirect typed varargs packing and array defaults remain source local",
          "[redirect][semantic-signature][varargs][regression]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module redirect_typed_packing;
        defaultItems : int[2]{20, 22};
        arrayBody(bodyItems : int[]) : int { return bodyItems.size; }
        varargBody(bodyArguments... : int) : int { return bodyArguments.size; }
        varargAlias(sourceArguments... : int) : int -> arrayBody;
        arrayAlias(sourceItems : int[] = defaultItems) : int -> varargBody;
        test() : int {
            items : int[3]{7, 8, 9};
            if (varargAlias() != 0) return 1;
            if (varargAlias(7) != 1) return 2;
            if (varargAlias(7, 8, 9) != 3) return 3;
            if (varargAlias(items) != 3) return 4;
            if (arrayAlias(items) != 3) return 5;
            if (arrayAlias() != 2) return 6;
            return 42;
        }
    )SRC");
    REQUIRE(compiler);
    auto root = compiler->get_unit()->get_root_namespace();
    auto array_body = root->get_function("arrayBody");
    auto vararg_body = root->get_function("varargBody");
    auto vararg_alias = root->get_function("varargAlias");
    auto array_alias = root->get_function("arrayAlias");
    REQUIRE(array_body);
    REQUIRE(vararg_body);
    REQUIRE(vararg_alias);
    REQUIRE(array_alias);
    CHECK(type::are_equal(vararg_alias->get_parameter(0)->get_type(),
                          array_body->get_parameter(0)->get_type()));
    CHECK(type::are_equal(array_alias->get_parameter(0)->get_type(),
                          vararg_body->get_parameter(0)->get_type()));
    CHECK(vararg_alias->get_parameter(0)->is_varargs());
    CHECK_FALSE(array_body->get_parameter(0)->is_varargs());
    CHECK_FALSE(array_alias->get_parameter(0)->is_varargs());
    CHECK(vararg_body->get_parameter(0)->is_varargs());
    CHECK(array_alias->get_parameter(0)->has_default_expr());
    CHECK_FALSE(vararg_body->get_parameter(0)->has_default_expr());
    CHECK(vararg_alias->get_redirect_target() == array_body);
    CHECK(array_alias->get_redirect_target() == vararg_body);
    auto& module = compiler->get_context_for_test()->module();
    auto vararg_entry = module.getNamedAlias(vararg_alias->get_mangled_name());
    auto array_entry = module.getNamedAlias(array_alias->get_mangled_name());
    REQUIRE(vararg_entry);
    REQUIRE(array_entry);
    CHECK(vararg_entry->getAliaseeObject() == module.getFunction(array_body->get_mangled_name()));
    auto vararg_provider = module.getFunction(vararg_body->get_mangled_name());
    REQUIRE(vararg_provider);
    CHECK(array_entry->getAliaseeObject() == vararg_provider);
    CHECK_FALSE(vararg_provider->getFunctionType()->isVarArg());

    kdi_builder builder(*compiler->get_context_for_test());
    compiler->get_unit()->accept(builder);
    auto file = builder.take();
    bool saw_vararg_alias = false;
    bool saw_array_alias = false;
    for (const auto& function : file.unit.root_ns.functions) {
        if (function.name == "varargAlias") {
            REQUIRE(function.params.size() == 1);
            CHECK(function.params.front().name == "sourceArguments");
            CHECK(function.params.front().is_varargs);
            CHECK_FALSE(function.params.front().default_source);
            saw_vararg_alias = true;
        } else if (function.name == "arrayAlias") {
            REQUIRE(function.params.size() == 1);
            CHECK(function.params.front().name == "sourceItems");
            CHECK_FALSE(function.params.front().is_varargs);
            REQUIRE(function.params.front().default_source);
            CHECK(function.params.front().default_source->find("defaultItems") != std::string::npos);
            saw_array_alias = true;
        }
    }
    CHECK(saw_vararg_alias);
    CHECK(saw_array_alias);
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("Redirect overloads prefer exact receiver qualification",
          "[redirect][semantic-signature][receiver][regression]") {
    auto jit = gen_jit(R"SRC(
        module redirect_const_overloads;
        struct Item {
            body() : int { return 21; }
            const body() : int { return 42; }
            read() : int -> body;
            const constRead() : int -> body;
        }
        test() : int {
            object : Item;
            return object.read() + object.constRead();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 63);
}

TEST_CASE("Redirect inherited receivers use provider subobjects without redirect wrappers",
          "[redirect][semantic-signature][receiver][regression]") {
    auto jit = gen_jit(R"SRC(
        module redirect_receiver_projection;
        struct Prefix { padding : long = 91; }
        struct Base {
            value : int = 37;
            read() : int { return value; }
        }
        struct Derived : public Prefix, public Base {
            read() : int -> Base::read;
        }
        test() : int {
            object : Derived;
            callback : &():int = object.read;
            unbound : Base::*():int = Derived::read;
            base : Base;
            return object.read() + Derived::read(object) + callback() + (base.*unbound)();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 148);
}

TEST_CASE("Redirect unbound references cannot conceal a receiver adjustment",
          "[redirect][semantic-signature][receiver][error][regression]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module redirect_unbound_receiver;
        struct Prefix { padding : long; }
        struct Base { value : int; read() : int { return value; } }
        struct Derived : public Prefix, public Base {
            read() : int -> Base::read;
        }
        test() {
            unbound : Derived::*():int = Derived::read;
        }
    )SRC"));
}

TEST_CASE("Redirect receiver bindings survive flattened member and free chains",
          "[redirect][semantic-signature][receiver][regression]") {
    auto jit = gen_jit(R"SRC(
        module redirect_flattened_receiver;
        struct Prefix { padding : long = 91; }
        struct Base {
            value : int = 37;
            body() : int { return value; }
            freeBody() : int -> readBase;
        }
        readBase(object : Base&) : int { return object.value; }
        struct Derived : public Prefix, public Base {
            memberAlias() : int -> Base::body;
            freeAlias() : int -> Base::freeBody;
        }
        memberChain(object : Derived&) : int -> Derived::memberAlias;
        freeChain(object : Derived&) : int -> Derived::freeAlias;
        finalChain(object : Derived&) : int -> freeChain;
        test() : int {
            object : Derived;
            base : Base;
            callback : &(Base&):int = finalChain;
            return memberChain(object) + freeChain(object) + finalChain(object) + callback(base);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 148);
}

TEST_CASE("Redirect free callable cannot conceal a flattened receiver adjustment",
          "[redirect][semantic-signature][receiver][error][regression]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module redirect_flattened_callable;
        struct Prefix { padding : long; }
        struct Base { value : int; body() : int { return value; } }
        struct Derived : public Prefix, public Base {
            read() : int -> Base::body;
        }
        freeAlias(object : Derived&) : int -> Derived::read;
        test() { unsafe : &(Derived&):int = freeAlias; }
    )SRC"));
}

TEST_CASE("Redirect virtual receivers use the provider owner for ordinary vtable thunks",
          "[redirect][semantic-signature][receiver][virtual][regression]") {
    auto jit = gen_jit(R"SRC(
        module redirect_virtual_receiver_projection;
        class Base {
            value : int = 37;
            read() : int { return value; }
        }
        class Derived : public Base {
            read() : int -> Base::read;
        }
        dispatch(object : Base&) : int { return object.read(); }
        test() : int {
            object : Derived;
            callback : &():int = object.read;
            return object.read() + Derived::read(object) + dispatch(object) + callback();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 148);
}

// ─────────────────────────────────────────────────────────────────────────────
// Model building tests — verify that AST redirect is correctly propagated
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Model — free function redirect has correct flags", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module gen_redirector_01;
        bar() : int { return 42; }
        foo() : int -> bar;
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns);

    // Find the 'foo' function
    auto foo = root_ns->get_function("foo");
    REQUIRE(foo);
    CHECK(foo->is_redirected());
    CHECK(foo->get_aliasing() == function::function_aliasing::REDIRECT);
    CHECK(foo->get_redirect_target_name().size() == 1);
    CHECK(foo->get_redirect_target_name()[0] == "bar");
    // Redirect functions have no explicit body (but get_block() lazily creates one)
    CHECK(foo->is_redirected());
}

TEST_CASE("Model — member function redirect has correct flags", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module gen_redirector_02;
        struct Foo {
            bar() : int { return 42; }
            baz() : int -> bar;
        }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns);

    auto foo_st = root_ns->get_structure("Foo");
    REQUIRE(foo_st);

    auto baz = foo_st->get_function("baz");
    REQUIRE(baz);
    CHECK(baz->is_redirected());
    CHECK(baz->get_redirect_target_name()[0] == "bar");
}

TEST_CASE("Model — redirect with qualified target name", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module gen_redirector_03;
        struct Base {
            method() : int { return 10; }
        }
        struct Derived : public Base {
            method() : int -> Base::method;
        }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    auto derived = root_ns->get_structure("Derived");
    REQUIRE(derived);

    auto method = derived->get_function("method");
    REQUIRE(method);
    CHECK(method->is_redirected());
    CHECK(method->get_redirect_target_name().size() == 2);
    CHECK(method->get_redirect_target_name()[0] == "Base");
    CHECK(method->get_redirect_target_name()[1] == "method");
}

TEST_CASE("Model — regular function is not redirected", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module gen_redirector_04;
        bar() : int { return 42; }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    auto bar = root_ns->get_function("bar");
    REQUIRE(bar);
    CHECK_FALSE(bar->is_redirected());
    CHECK(bar->get_aliasing() == function::function_aliasing::NONE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 — Resolution tests (full pipeline via gen_jit)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — free function to free function", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_05;
        bar() : int { return 42; }
        foo() : int -> bar;
        main() : int {
            return foo();
        }
    )SRC");
    CHECK(result.exit_code == 42);
}

TEST_CASE("Redirect — free function with params", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_06;
        add(a: int, b: int) : int { return a + b; }
        sum(a: int, b: int) : int -> add;
        main() : int {
            return sum(3, 7);
        }
    )SRC");
    CHECK(result.exit_code == 10);
}

TEST_CASE("Redirect — member function in struct", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_07;
        struct Foo {
            bar() : int { return 99; }
            baz() : int -> bar;
        }
        main() : int {
            f : Foo;
            return f.baz();
        }
    )SRC");
    CHECK(result.exit_code == 99);
}

TEST_CASE("Redirect — member to parent struct method", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_08;
        struct Base {
            value() : int { return 7; }
        }
        struct Derived : public Base {
            value() : int -> Base::value;
        }
        main() : int {
            d : Derived;
            return d.value();
        }
    )SRC");
    CHECK(result.exit_code == 7);
}

TEST_CASE("Redirect — chained redirects", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_09;
        impl() : int { return 55; }
        mid() : int -> impl;
        top() : int -> mid;
        main() : int {
            return top();
        }
    )SRC");
    CHECK(result.exit_code == 55);
}

TEST_CASE("Redirect — error: unknown target", "[redirect][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_redirector_10;
        foo() : int -> nonexistent;
    )SRC"));
}

TEST_CASE("Redirect — error: circular redirect", "[redirect][error]") {
    const char* declarations = GENERATE(
        "foo() : int -> foo;",
        "foo() : int -> bar; bar() : int -> foo;",
        "foo() : int -> bar; bar() : int -> baz; baz() : int -> foo;"
    );
    INFO(declarations);
    try {
        gen_jit_throws(std::string("module gen_redirector_11; ") + declarations);
        FAIL("Expected a circular redirect diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_CHAIN_CYCLE));
    }
}

TEST_CASE("Redirect — error: target is abstract", "[redirect][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_redirector_12;
        abstract class Base {
            abstract speak() : int;
        }
        class Derived : public Base {
            speak() : int -> Base::speak;
        }
    )SRC"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6 — Virtual dispatch with redirects
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — virtual method redirect to parent", "[redirect][codegen][virtual]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_13;
        class Base {
            speak() : int { return 10; }
        }
        class Derived : public Base {
            speak() : int -> Base::speak;
        }
        dispatch(b: Base&) : int {
            return b.speak();
        }
        main() : int {
            d : Derived;
            return dispatch(d);
        }
    )SRC");
    CHECK(result.exit_code == 10);
}

TEST_CASE("Redirect — override a redirected virtual method", "[redirect][codegen][virtual]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_14;
        class Base {
            speak() : int { return 10; }
        }
        class Mid : public Base {
            speak() : int -> Base::speak;
        }
        class Final : public Mid {
            speak() : int { return 99; }
        }
        dispatch(b: Base&) : int {
            return b.speak();
        }
        main() : int {
            f : Final;
            return dispatch(f);
        }
    )SRC");
    CHECK(result.exit_code == 99);
}

TEST_CASE("Redirect — redirect with return type void", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_15;
        g : int = 0;
        impl() { g = 77; }
        aliasFn() -> impl;
        main() : int {
            aliasFn();
            return g;
        }
    )SRC");
    CHECK(result.exit_code == 77);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward reference and ordering
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — forward reference (target defined after redirector)", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_16;
        aliasFn() : int -> impl;
        impl() : int { return 33; }
        main() : int {
            return aliasFn();
        }
    )SRC");
    CHECK(result.exit_code == 33);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static member redirect to free function (cross-kind)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — static member to free function", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_17;
        helper(a: int) : int { return a * 3; }
        struct Foo {
            static compute(a: int) : int -> helper;
        }
        main() : int {
            return Foo::compute(5);
        }
    )SRC");
    CHECK(result.exit_code == 15);
}

// ─────────────────────────────────────────────────────────────────────────────
// Triple chained redirect
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — triple chain a -> b -> c -> impl", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module gen_redirector_18;
        impl() : int { return 88; }
        c() : int -> impl;
        b() : int -> c;
        a() : int -> b;
        main() : int {
            return a();
        }
    )SRC");
    CHECK(result.exit_code == 88);
}

// ─────────────────────────────────────────────────────────────────────────────
// ELF symbol presence via shared library
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — alias symbol present in shared library", "[redirect][codegen][lib]") {
    auto so_path = build_shared_library(R"SRC(
        module gen_redirector_19;
        bar() : int { return 42; }
        foo() : int -> bar;
    )SRC");

    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    REQUIRE(nm_res.exit_code == 0);

    // Both the target and the alias should appear in the symbol table
    CHECK(nm_res.out.find("bar") != std::string::npos);
    CHECK(nm_res.out.find("foo") != std::string::npos);

    std::remove(so_path.c_str());
}
