/*
 * K Language standard library — RTTI tests
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
 * Tests for ::k::Class RTTI (Runtime Type Information).
 *
 * These tests exercise:
 *  - Object::getClass() returns the correct ::k::Class instance
 *  - Class::getName() returns the short (unqualified) class name
 *  - getClass() on stdlib types (String, Object)
 *  - Polymorphic getClass() via Object& reference
 *  - Class identity: same type -> same Class reference
 *
 * Note: calling inherited imported methods (like getClass/hash) directly on
 * user-defined classes is not yet supported by the compiler's imported-method
 * resolution.  These tests call getClass() through stdlib types or through
 * explicit Object& references.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <cstring>

// Compile-time paths injected by CMake (see libk/libk/CMakeLists.txt).
#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined -- check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined -- check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// =========================================================================
// 1. getClass().getName() on String returns "String"
// =========================================================================

TEST_CASE("RTTI: getClass().getName() on String", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_string_name__;

        test() : int {
            s : k::String("hello");
            name : const char[]? = s.getClass().getName();
            // "String" = 6 chars + null terminator
            if (name->size != 7) return 0;
            if (name[0] != 'S') return 1;
            if (name[1] != 't') return 2;
            if (name[2] != 'r') return 3;
            if (name[3] != 'i') return 4;
            if (name[4] != 'n') return 5;
            if (name[5] != 'g') return 6;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 2. getClass().getName() on String compared via String equality
// =========================================================================

TEST_CASE("RTTI: getName() compared with String ==", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_string_cmp__;

        test() : int {
            s : k::String("world");
            name : k::String(s.getClass().getName());
            expected : k::String("String");
            if (name == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 3. Class identity -- two String instances share the same Class
// =========================================================================

TEST_CASE("RTTI: two instances share the same Class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_identity__;

        test() : int {
            s1 : k::String("abc");
            s2 : k::String("xyz");
            n1 : const char[]? = s1.getClass().getName();
            n2 : const char[]? = s2.getClass().getName();
            // Both should point to "String" -- check size and first char
            if (n1->size != n2->size) return 0;
            if (n1[0] != 'S') return 1;
            if (n2[0] != 'S') return 2;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 4. getClass().getName() on Object directly
// =========================================================================

TEST_CASE("RTTI: getClass().getName() on Object", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_object_name__;

        test() : int {
            o : k::Object;
            name : k::String(o.getClass().getName());
            expected : k::String("Object");
            if (name == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 5. Polymorphic getClass() -- String behind Object& returns "String"
// =========================================================================

TEST_CASE("RTTI: polymorphic getClass() via Object& returns concrete type", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_poly__;

        get_class_name(o : k::Object&) : k::String {
            result : k::String(o.getClass().getName());
            return result;
        }

        test() : int {
            s : k::String("hello");
            name : k::String = get_class_name(s);
            expected : k::String("String");
            if (name == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 6. Polymorphic getClass() -- different types return different names
// =========================================================================

TEST_CASE("RTTI: polymorphic getClass() distinguishes types", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_poly_diff__;

        get_class_name(o : k::Object&) : k::String {
            result : k::String(o.getClass().getName());
            return result;
        }

        test() : int {
            s : k::String("hello");
            o : k::Object;
            sname : k::String = get_class_name(s);
            oname : k::String = get_class_name(o);
            sexpected : k::String("String");
            oexpected : k::String("Object");
            if (sname != sexpected) return 1;
            if (oname != oexpected) return 2;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 7. RTTI: String's Class descriptor has correct name via getName()
// =========================================================================

TEST_CASE("RTTI: String Class descriptor name", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_class_desc__;
        import k;

        test() : int {
            s : k::String("hello");
            cls : const k::Class& = s.getClass();
            name : k::String(cls.getName());
            expected : k::String("String");
            if (name == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 8. Class RTTI: getBases() is non-null on String
// =========================================================================

TEST_CASE("RTTI: getBases() non-null on String", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_bases_nn__;
        import k;

        test() : int {
            s : k::String("hello");
            if (s.getClass().getBases() == null) return 0;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 9. Class RTTI: getBases() on String has 1 element
// =========================================================================

TEST_CASE("RTTI: getBases() size on String is 1", "[libk][rtti]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __rtti_bases_sz__;
        import k;

        test() : int {
            s : k::String("hello");
            // String inherits from Object, so 1 base
            return s.getClass().getBases()->size;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR, /*dump=*/true, /*optimize=*/false);
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}


// =========================================================================
// 10. Class RTTI: getBases() is null on Object (no direct bases)
// =========================================================================

TEST_CASE("RTTI: getBases() null on Object", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_bases_null__;
        import k;

        test() : int {
            o : k::Object;
            // Object is the root class — getBases() should be null
            if (o.getClass().getBases() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 11. Class RTTI: Object has no bases (null)
// =========================================================================

TEST_CASE("RTTI: Object has no bases", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_no_bases__;
        import k;

        test() : int {
            o : k::Object;
            // Object is the root class — getBases() should be null
            if (o.getClass().getBases() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 12. RTTI: getNested() is null on String (no nested types)
// =========================================================================

TEST_CASE("RTTI: getNested() null on String", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_nested_null__;
        import k;

        test() : int {
            s : k::String("hello");
            // String has no nested types
            if (s.getClass().getNested() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 13. RTTI: getEnclosing() is null on String (top-level type)
// =========================================================================

TEST_CASE("RTTI: getEnclosing() null on String", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_enclosing_null__;
        import k;

        test() : int {
            s : k::String("hello");
            // String is a top-level class
            if (s.getClass().getEnclosing() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 14. RTTI: getNested() on a class with a nested class
// =========================================================================

TEST_CASE("RTTI: getNested() on class with nested class", "[libk][rtti]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __rtti_nested_cls__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
        }

        test_name() : int {
            o : Outer;
            cls : const k::Class& = o.getClass();
            name : k::String(cls.getName());
            expected : k::String("Outer");
            if (name == expected) return 42;
            return 0;
        }

        test_nested_null() : int {
            o : Outer;
            if (o.getClass().getNested() == null) return 0;
            return 42;
        }

        test_nested_size() : int {
            o : Outer;
            nested : const k::TypeInfo?[]? = o.getClass().getNested();
            if (nested == null) return 0;
            if (nested->size != 1) return 1;
            return 42;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR, /*dump=*/false, /*optimize=*/false);
    REQUIRE(jit);

    auto test_name = jit->lookup_symbol<int(*)()>("test_name");
    REQUIRE(test_name);
    CHECK(test_name() == 42);

    auto test_nested_null = jit->lookup_symbol<int(*)()>("test_nested_null");
    REQUIRE(test_nested_null);
    CHECK(test_nested_null() == 42);

    auto test_nested_size = jit->lookup_symbol<int(*)()>("test_nested_size");
    REQUIRE(test_nested_size);
    CHECK(test_nested_size() == 42);
}


// =========================================================================
// 15. RTTI: getNested() — nested type name is "Inner"
// =========================================================================

TEST_CASE("RTTI: getNested() name of nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_nested_name__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
        }

        test() : int {
            o : Outer;
            nested : const k::TypeInfo?[]? = o.getClass().getNested();
            if (nested == null) return 0;
            // Get the first nested type's name
            innerInfo : const k::TypeInfo? = nested[0];
            if (innerInfo == null) return 1;
            name : k::String(innerInfo->getName());
            expected : k::String("Inner");
            if (name == expected) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 16. RTTI: getEnclosing() — nested class reports its enclosing type
// =========================================================================

TEST_CASE("RTTI: getEnclosing() on nested class returns outer", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_enclosing_cls__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_enclosing() : int {
                i : Inner;
                enc : const k::TypeInfo? = i.getClass().getEnclosing();
                if (enc == null) return 0;
                name : k::String(enc->getName());
                expected : k::String("Outer");
                if (name == expected) return 42;
                return 1;
            }
        }

        test() : int {
            o : Outer;
            return o.test_enclosing();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 17. RTTI: getEnclosing() is null on a top-level user class
// =========================================================================

TEST_CASE("RTTI: getEnclosing() null on top-level class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_enclosing_top__;
        import k;

        class TopLevel {
            public TopLevel() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : TopLevel;
            if (t.getClass().getEnclosing() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 18. RTTI: getNested() with multiple nested types
// =========================================================================

TEST_CASE("RTTI: getNested() with multiple nested types", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_nested_multi__;
        import k;

        class Container {
            public Container() {}
            public dummy() : int { return 0; }
            class Alpha {
                public Alpha() {}
                public val() : int { return 1; }
            }
            class Beta {
                public Beta() {}
                public val() : int { return 2; }
            }
        }

        test() : int {
            c : Container;
            nested : const k::TypeInfo?[]? = c.getClass().getNested();
            if (nested == null) return 0;
            // Should have exactly 2 nested types
            return nested->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}

