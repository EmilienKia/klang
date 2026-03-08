/*
 * K Language compiler — libkdi tests
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

#include <catch2/catch_all.hpp>
#include "kdi.hpp"
#include "kdi_symbols.hpp"

#include <filesystem>
#include <set>

using namespace kdi;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static kdi_file make_file_with_function(const std::string& mangled) {
    kdi_file f;
    f.header.module_name = "sym::test";
    f.unit.name          = "sym::test";
    kdi_function fn;
    fn.name         = "f";
    fn.fq_name      = "sym::test::f";
    fn.return_type  = kdi_type::make_void();
    fn.mangled_name = mangled;
    f.unit.root_ns.functions.push_back(fn);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_check_symbols — symbol-set variant
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("check-symbols: all present → result is ok", "[symbols]") {
    auto f = make_file_with_function("_KFN3sym4test1fEv");
    std::set<std::string> syms = {"_KFN3sym4test1fEv", "_KFN3sym4test1gEv"};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( r.is_ok() );
    REQUIRE( r.missing.empty() );
}

TEST_CASE("check-symbols: one missing → reported", "[symbols]") {
    auto f = make_file_with_function("_KFN3sym4test1fEv");
    std::set<std::string> syms = {};   // empty binary — everything is missing
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( !r.is_ok() );
    REQUIRE( r.missing.size() == 1 );
    REQUIRE( r.missing[0].mangled_name == "_KFN3sym4test1fEv" );
}

TEST_CASE("check-symbols: empty mangled name skipped", "[symbols]") {
    kdi_file f;
    f.header.module_name = "x"; f.unit.name = "x";
    kdi_function fn;
    fn.name = "g"; fn.fq_name = "x::g";
    fn.return_type = kdi_type::make_void();
    fn.mangled_name = "";         // intentionally empty
    f.unit.root_ns.functions.push_back(fn);
    std::set<std::string> syms = {};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( r.is_ok() );         // empty symbol is not checked
}

TEST_CASE("check-symbols: abstract method not checked", "[symbols]") {
    kdi_file f;
    f.header.module_name = "a"; f.unit.name = "a";
    kdi_aggregate agg;
    agg.name = "I"; agg.fq_name = "a::I"; agg.mangled_name = "_KSa1I";
    kdi_method m;
    m.name = "run"; m.fq_name = "a::I::run";
    m.return_type = kdi_type::make_void();
    m.is_abstract = true;
    m.mangled_name = "_KFMN1a1I3runEv";   // declared but abstract
    agg.methods.push_back(m);
    f.unit.root_ns.aggregates.push_back(agg);
    // Even though the symbol is not in the binary, it should not be reported
    std::set<std::string> syms = {};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( r.is_ok() );
}

TEST_CASE("check-symbols: constructor C1+C2 both checked", "[symbols]") {
    kdi_file f;
    f.header.module_name = "p"; f.unit.name = "p";
    kdi_aggregate agg;
    agg.name = "Pt"; agg.fq_name = "p::Pt"; agg.mangled_name = "_KSp2Pt";
    kdi_constructor ctor;
    ctor.mangled_name    = "_KFMC1Np2PtE";
    ctor.mangled_name_c2 = "_KFMC2Np2PtE";
    agg.constructors.push_back(ctor);
    f.unit.root_ns.aggregates.push_back(agg);

    // Only C1 present → C2 should be missing
    std::set<std::string> syms = {"_KFMC1Np2PtE"};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( !r.is_ok() );
    REQUIRE( r.missing.size() == 1 );
    REQUIRE( r.missing[0].mangled_name == "_KFMC2Np2PtE" );
}

TEST_CASE("check-symbols: compiler-generated destructor not checked", "[symbols]") {
    kdi_file f;
    f.header.module_name = "q"; f.unit.name = "q";
    kdi_aggregate agg;
    agg.name = "Q"; agg.fq_name = "q::Q"; agg.mangled_name = "_KSq1Q";
    kdi_destructor dtor;
    dtor.is_compiler_generated = true;
    dtor.mangled_name    = "_KFMD1Nq1QE";
    dtor.mangled_name_d2 = "_KFMD2Nq1QE";
    agg.destructor = dtor;
    f.unit.root_ns.aggregates.push_back(agg);
    // Even though the symbols are not in the binary, they should not be reported
    std::set<std::string> syms = {};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( r.is_ok() );
}

TEST_CASE("check-symbols: vtable + rtti checked", "[symbols]") {
    kdi_file f;
    f.header.module_name = "v"; f.unit.name = "v";
    kdi_aggregate agg;
    agg.name = "V"; agg.fq_name = "v::V"; agg.mangled_name = "_KSv1V";
    kdi_vtable vt;
    vt.vtable_symbol = "_ZTVN1v1VE";
    vt.rtti_symbol   = "_ZTIN1v1VE";
    agg.vtable = vt;
    f.unit.root_ns.aggregates.push_back(agg);
    std::set<std::string> syms = {"_ZTVN1v1VE", "_ZTIN1v1VE"};
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( r.is_ok() );
}

TEST_CASE("check-symbols: vtable missing reported", "[symbols]") {
    kdi_file f;
    f.header.module_name = "v2"; f.unit.name = "v2";
    kdi_aggregate agg;
    agg.name = "V"; agg.fq_name = "v2::V"; agg.mangled_name = "_KSv21V";
    kdi_vtable vt;
    vt.vtable_symbol = "_ZTVN2v21VE";
    vt.rtti_symbol   = "_ZTIN2v21VE";
    agg.vtable = vt;
    f.unit.root_ns.aggregates.push_back(agg);
    std::set<std::string> syms = {};  // empty binary
    auto r = kdi_check_symbols(f, syms);
    REQUIRE( !r.is_ok() );
    REQUIRE( r.missing.size() == 2 );   // vtable + rtti
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_collect_binary_symbols — live test using this test binary itself
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("collect_binary_symbols: reads self (test binary)", "[symbols][live]") {
    // The test binary itself has exported symbols (Catch2 framework + test symbols).
    // We verify the API works at all.
    std::string self;
#if defined(__linux__)
    // /proc/self/exe is available on Linux
    self = std::filesystem::read_symlink("/proc/self/exe").string();
#else
    SKIP("Platform does not support reading /proc/self/exe");
#endif
    std::set<std::string> syms;
    REQUIRE_NOTHROW( syms = kdi_collect_binary_symbols(self) );
    REQUIRE( !syms.empty() );
}

TEST_CASE("collect_binary_symbols: non-existent file throws", "[symbols][live]") {
    REQUIRE_THROWS_AS(
        kdi_collect_binary_symbols("/tmp/__kdi_nonexistent_99999.so"),
        kdi_symbol_error
    );
}

TEST_CASE("kdi_symbol_error: message includes context", "[symbols]") {
    kdi_symbol_error err("test context message");
    REQUIRE( std::string(err.what()).find("test context message") != std::string::npos );
}

