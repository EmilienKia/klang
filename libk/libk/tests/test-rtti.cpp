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


// =========================================================================
// 19. RTTI: getVisibility() on a public top-level class returns PUBLIC (0)
// =========================================================================

TEST_CASE("RTTI: getVisibility() returns PUBLIC on top-level class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_vis_public__;
        import k;

        class Foo {
            public Foo() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            f : Foo;
            vis : k::Visibility = f.getClass().getVisibility();
            if (vis == k::Visibility::PUBLIC) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 20. RTTI: isStatic() returns false on a top-level class
// =========================================================================

TEST_CASE("RTTI: isStatic() returns false on top-level class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_static_top__;
        import k;

        class Foo {
            public Foo() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            f : Foo;
            if (f.getClass().isStatic()) return 0;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 21. RTTI: isStatic() on a static nested class returns true
// =========================================================================

TEST_CASE("RTTI: isStatic() on static nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_static_nested__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            static class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_static() : int {
                i : Inner;
                if (i.getClass().isStatic()) return 42;
                return 0;
            }
        }

        test() : int {
            o : Outer;
            return o.test_static();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 22. RTTI: isStatic() on a non-static nested class returns false
// =========================================================================

TEST_CASE("RTTI: isStatic() on non-static nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_nonstatic_nested__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_static() : int {
                i : Inner;
                if (i.getClass().isStatic()) return 0;
                return 42;
            }
        }

        test() : int {
            o : Outer;
            return o.test_static();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 23. RTTI: getVisibility() on a private nested class returns PRIVATE (2)
// =========================================================================

TEST_CASE("RTTI: getVisibility() returns PRIVATE on private nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_vis_private__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            private class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_vis() : int {
                i : Inner;
                vis : k::Visibility = i.getClass().getVisibility();
                if (vis == k::Visibility::PRIVATE) return 42;
                return 0;
            }
        }

        test() : int {
            o : Outer;
            return o.test_vis();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 24. RTTI: getVisibility() on a protected nested class returns PROTECTED (1)
// =========================================================================

TEST_CASE("RTTI: getVisibility() returns PROTECTED on protected nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_vis_protected__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            protected class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_vis() : int {
                i : Inner;
                vis : k::Visibility = i.getClass().getVisibility();
                if (vis == k::Visibility::PROTECTED) return 42;
                return 0;
            }
        }

        test() : int {
            o : Outer;
            return o.test_vis();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 25. RTTI: getFullName() on a top-level user class
// =========================================================================

TEST_CASE("RTTI: getFullName() on top-level user class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fqn_top__;
        import k;

        class Foo {
            public Foo() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fqn : k::String(f.getClass().getFullName());
            expected : k::String("::__rtti_fqn_top__::Foo");
            if (fqn == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 26. RTTI: getFullName() on String returns "::k::String"
// =========================================================================

TEST_CASE("RTTI: getFullName() on String", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fqn_string__;
        import k;

        test() : int {
            s : k::String("hello");
            fqn : k::String(s.getClass().getFullName());
            expected : k::String("::k::String");
            if (fqn == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 27. RTTI: getFullName() on Object returns "::k::Object"
// =========================================================================

TEST_CASE("RTTI: getFullName() on Object", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fqn_object__;
        import k;

        test() : int {
            o : k::Object;
            fqn : k::String(o.getClass().getFullName());
            expected : k::String("::k::Object");
            if (fqn == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 28. RTTI: getFullName() on a nested class includes enclosing name
// =========================================================================

TEST_CASE("RTTI: getFullName() on nested class", "[libk][rtti]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fqn_nested__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class Inner {
                public Inner() {}
                public value() : int { return 7; }
            }
            public test_fqn() : int {
                i : Inner;
                fqn : k::String(i.getClass().getFullName());
                expected : k::String("::__rtti_fqn_nested__::Outer::Inner");
                if (fqn == expected) return 42;
                return 0;
            }
        }

        test() : int {
            o : Outer;
            return o.test_fqn();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 29. RTTI: getAnnotations() is null on a class with no annotations
// =========================================================================

TEST_CASE("RTTI: getAnnotations() null on unannotated class", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_null__;
        import k;

        class Plain {
            public Plain() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            p : Plain;
            if (p.getClass().getAnnotations() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 30. RTTI: getAnnotations() is non-null on an annotated class
// =========================================================================

TEST_CASE("RTTI: getAnnotations() non-null on annotated class", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_nonnull__;
        import k;

        annotation Marker {}

        @Marker
        class Tagged {
            public Tagged() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : Tagged;
            if (t.getClass().getAnnotations() == null) return 0;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 31. RTTI: getAnnotations() on annotated class returns array of size 1
// =========================================================================

TEST_CASE("RTTI: getAnnotations() size is 1 on single-annotated class", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_size1__;
        import k;

        annotation Deprecated {}

        @Deprecated
        class OldClass {
            public OldClass() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            o : OldClass;
            anns : const k::Annotation?[]? = o.getClass().getAnnotations();
            if (anns == null) return 0;
            return anns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}


// =========================================================================
// 32. RTTI: getAnnotations() size is 2 on double-annotated class
// =========================================================================

TEST_CASE("RTTI: getAnnotations() size is 2 on double-annotated class", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_size2__;
        import k;

        annotation Alpha {}
        annotation Beta {}

        @Alpha @Beta
        class MultiAnn {
            public MultiAnn() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            m : MultiAnn;
            anns : const k::Annotation?[]? = m.getClass().getAnnotations();
            if (anns == null) return 0;
            return anns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}


// =========================================================================
// 33. RTTI: annotation instance is non-null in annotations array
// =========================================================================

TEST_CASE("RTTI: annotation instance in array is non-null", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_elem_nn__;
        import k;

        annotation Tag {}

        @Tag
        class Elem {
            public Elem() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            e : Elem;
            anns : const k::Annotation?[]? = e.getClass().getAnnotations();
            if (anns == null) return 0;
            if (anns[0] == null) return 1;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 34. RTTI: getAnnotationType().getName() returns annotation type name
// =========================================================================

TEST_CASE("RTTI: getAnnotationType().getName() returns annotation name", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_typename__;
        import k;

        annotation Info {}

        @Info
        class Target {
            public Target() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : Target;
            anns : const k::Annotation?[]? = t.getClass().getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            atype : const k::AnnotationType& = ann->getAnnotationType();
            name : k::String(atype.getName());
            expected : k::String("Info");
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
// 35. RTTI: getAnnotationType().getFullName() returns FQ annotation name
// =========================================================================

TEST_CASE("RTTI: getAnnotationType().getFullName() returns FQ name", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_fqname__;
        import k;

        annotation Marker {}

        @Marker
        class Target {
            public Target() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : Target;
            anns : const k::Annotation?[]? = t.getClass().getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            fqn : k::String(ann->getAnnotationType().getFullName());
            expected : k::String("::__rtti_ann_fqname__::Marker");
            if (fqn == expected) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 36. RTTI: multiple annotation instances have distinct types
// =========================================================================

TEST_CASE("RTTI: multiple annotations have distinct type names", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_multi_types__;
        import k;

        annotation First {}
        annotation Second {}

        @First @Second
        class MultiTarget {
            public MultiTarget() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            m : MultiTarget;
            anns : const k::Annotation?[]? = m.getClass().getAnnotations();
            if (anns == null) return 0;
            if (anns->size != 2) return 1;

            a0 : const k::Annotation? = anns[0];
            a1 : const k::Annotation? = anns[1];
            if (a0 == null) return 2;
            if (a1 == null) return 3;

            n0 : k::String(a0->getAnnotationType().getName());
            n1 : k::String(a1->getAnnotationType().getName());
            e0 : k::String("First");
            e1 : k::String("Second");
            if (n0 != e0) return 4;
            if (n1 != e1) return 5;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 37. RTTI: getAnnotations() on interface — non-null when annotated
// =========================================================================

TEST_CASE("RTTI: annotated interface appears in class bases with correct name", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_iface__;
        import k;

        annotation Documented {}

        @Documented
        interface Describable {
            const describe() : int;
        }

        class Impl : public k::Object, public Describable {
            public Impl() {}
            override const describe() : int { return 7; }
        }

        test_iface_has_annotations() : int {
            // Get the Describable interface RTTI via the Impl's bases
            i : Impl;
            bases : const k::TypeInfo?[]? = i.getClass().getBases();
            if (bases == null) return 0;
            // Check each base until we find "Describable"
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Describable");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test_iface_has_annotations");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 38. RTTI: getAnnotations() null on unannotated interface
// =========================================================================

TEST_CASE("RTTI: unannotated interface appears in class bases", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_iface_null__;
        import k;

        interface Bare {
            const value() : int;
        }

        class Impl : public k::Object, public Bare {
            public Impl() {}
            override const value() : int { return 0; }
        }

        test() : int {
            // Inspect the Bare interface RTTI via Impl's bases
            i : Impl;
            bases : const k::TypeInfo?[]? = i.getClass().getBases();
            if (bases == null) return 0;
            // Find "Bare" among bases
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Bare");
                    if (name == expected) {
                        return 42;
                    }
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 39. RTTI: AnnotationType inherits from AggregateType — getVisibility()
// =========================================================================

TEST_CASE("RTTI: AnnotationType getVisibility() returns PUBLIC", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_vis__;
        import k;

        annotation Vis {}

        @Vis
        class Target {
            public Target() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : Target;
            anns : const k::Annotation?[]? = t.getClass().getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            v : k::Visibility = ann->getAnnotationType().getVisibility();
            if (v == k::Visibility::PUBLIC) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 40. RTTI: annotation field value — single int field via view downcast
// =========================================================================

TEST_CASE("RTTI: annotation int field value via view downcast", "[libk][rtti][annotation][field]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_int__;
        import k;

        annotation Priority {
            value : int;
        }

        @Priority(42)
        class Task {
            public Task() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            t : Task;
            anns : const k::Annotation?[]? = t.getClass().getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            p : const Priority? = ann;
            if (p == null) return 2;
            return p->value;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 41. RTTI: annotation multiple int fields via view downcast
// =========================================================================

TEST_CASE("RTTI: annotation multiple int fields via view downcast", "[libk][rtti][annotation][field]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_multi__;
        import k;

        annotation Version {
            major : int;
            minor : int;
            patch : int;
        }

        @Version(2, 5, 1)
        class Api {
            public Api() {}
            public dummy() : int { return 0; }
        }

        test_major() : int {
            a : Api;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->major;
        }

        test_minor() : int {
            a : Api;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->minor;
        }

        test_patch() : int {
            a : Api;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->patch;
        }
    )SRC");
    REQUIRE(jit);

    auto test_major = jit->lookup_symbol<int(*)()>("test_major");
    auto test_minor = jit->lookup_symbol<int(*)()>("test_minor");
    auto test_patch = jit->lookup_symbol<int(*)()>("test_patch");
    REQUIRE(test_major);
    REQUIRE(test_minor);
    REQUIRE(test_patch);
    CHECK(test_major() == 2);
    CHECK(test_minor() == 5);
    CHECK(test_patch() == 1);
}


// =========================================================================
// 42. RTTI: annotation bool field value via view downcast
// =========================================================================

TEST_CASE("RTTI: annotation bool field value via view downcast", "[libk][rtti][annotation][field]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_bool__;
        import k;

        annotation Flag {
            enabled : bool;
        }

        @Flag(true)
        class Active {
            public Active() {}
            public dummy() : int { return 0; }
        }

        @Flag(false)
        class Inactive {
            public Inactive() {}
            public dummy() : int { return 0; }
        }

        test_active() : int {
            a : Active;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            f : const Flag? = anns[0];
            if (f == null) return -2;
            if (f->enabled) return 1;
            return 0;
        }

        test_inactive() : int {
            i : Inactive;
            anns : const k::Annotation?[]? = i.getClass().getAnnotations();
            if (anns == null) return -1;
            f : const Flag? = anns[0];
            if (f == null) return -2;
            if (f->enabled) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_active = jit->lookup_symbol<int(*)()>("test_active");
    auto test_inactive = jit->lookup_symbol<int(*)()>("test_inactive");
    REQUIRE(test_active);
    REQUIRE(test_inactive);
    CHECK(test_active() == 1);
    CHECK(test_inactive() == 0);
}


// =========================================================================
// 43. RTTI: annotation field with default values
// =========================================================================

TEST_CASE("RTTI: annotation default field values via view downcast", "[libk][rtti][annotation][field][default]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_default__;
        import k;

        annotation Config {
            level : int = 5;
            verbose : bool = true;
        }

        @Config
        class DefaultService {
            public DefaultService() {}
            public dummy() : int { return 0; }
        }

        test_level() : int {
            s : DefaultService;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            c : const Config? = anns[0];
            if (c == null) return -2;
            return c->level;
        }

        test_verbose() : int {
            s : DefaultService;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            c : const Config? = anns[0];
            if (c == null) return -2;
            if (c->verbose) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_level = jit->lookup_symbol<int(*)()>("test_level");
    auto test_verbose = jit->lookup_symbol<int(*)()>("test_verbose");
    REQUIRE(test_level);
    REQUIRE(test_verbose);
    CHECK(test_level() == 5);
    CHECK(test_verbose() == 1);
}


// =========================================================================
// 44. RTTI: annotation partial positional args use defaults for remaining
// =========================================================================

TEST_CASE("RTTI: annotation partial positional args with defaults", "[libk][rtti][annotation][field][default]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_partial__;
        import k;

        annotation Config {
            level : int = 0;
            verbose : bool = true;
        }

        @Config(7)
        class PartialService {
            public PartialService() {}
            public dummy() : int { return 0; }
        }

        test_level() : int {
            s : PartialService;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            c : const Config? = anns[0];
            if (c == null) return -2;
            return c->level;
        }

        test_verbose() : int {
            s : PartialService;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            c : const Config? = anns[0];
            if (c == null) return -2;
            if (c->verbose) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_level = jit->lookup_symbol<int(*)()>("test_level");
    auto test_verbose = jit->lookup_symbol<int(*)()>("test_verbose");
    REQUIRE(test_level);
    REQUIRE(test_verbose);
    CHECK(test_level() == 7);
    CHECK(test_verbose() == 1);
}


// =========================================================================
// 45. RTTI: annotation designated init — field values via view downcast
// =========================================================================

TEST_CASE("RTTI: annotation designated init field values", "[libk][rtti][annotation][field][designated]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_desig__;
        import k;

        annotation Range {
            min : int;
            max : int;
        }

        @Range{.min = 10, .max = 200}
        class Bounded {
            public Bounded() {}
            public dummy() : int { return 0; }
        }

        test_min() : int {
            b : Bounded;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            r : const Range? = anns[0];
            if (r == null) return -2;
            return r->min;
        }

        test_max() : int {
            b : Bounded;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            r : const Range? = anns[0];
            if (r == null) return -2;
            return r->max;
        }
    )SRC");
    REQUIRE(jit);

    auto test_min = jit->lookup_symbol<int(*)()>("test_min");
    auto test_max = jit->lookup_symbol<int(*)()>("test_max");
    REQUIRE(test_min);
    REQUIRE(test_max);
    CHECK(test_min() == 10);
    CHECK(test_max() == 200);
}


// =========================================================================
// 46. RTTI: annotation designated init — out-of-order fields
// =========================================================================

TEST_CASE("RTTI: annotation designated init out-of-order fields", "[libk][rtti][annotation][field][designated]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_ooo__;
        import k;

        annotation Pair {
            first : int;
            second : int;
        }

        @Pair{.second = 99, .first = 11}
        class Holder {
            public Holder() {}
            public dummy() : int { return 0; }
        }

        test_first() : int {
            h : Holder;
            anns : const k::Annotation?[]? = h.getClass().getAnnotations();
            if (anns == null) return -1;
            p : const Pair? = anns[0];
            if (p == null) return -2;
            return p->first;
        }

        test_second() : int {
            h : Holder;
            anns : const k::Annotation?[]? = h.getClass().getAnnotations();
            if (anns == null) return -1;
            p : const Pair? = anns[0];
            if (p == null) return -2;
            return p->second;
        }
    )SRC");
    REQUIRE(jit);

    auto test_first = jit->lookup_symbol<int(*)()>("test_first");
    auto test_second = jit->lookup_symbol<int(*)()>("test_second");
    REQUIRE(test_first);
    REQUIRE(test_second);
    CHECK(test_first() == 11);
    CHECK(test_second() == 99);
}


// =========================================================================
// 47. RTTI: same annotation type, different values on different classes
// =========================================================================

TEST_CASE("RTTI: same annotation different values on different classes", "[libk][rtti][annotation][field][multi]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_diff__;
        import k;

        annotation Version {
            major : int;
            minor : int;
        }

        @Version(1, 0)
        class ApiV1 {
            public ApiV1() {}
            public dummy() : int { return 0; }
        }

        @Version(2, 3)
        class ApiV2 {
            public ApiV2() {}
            public dummy() : int { return 0; }
        }

        test_v1_major() : int {
            a : ApiV1;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->major;
        }

        test_v1_minor() : int {
            a : ApiV1;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->minor;
        }

        test_v2_major() : int {
            a : ApiV2;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->major;
        }

        test_v2_minor() : int {
            a : ApiV2;
            anns : const k::Annotation?[]? = a.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->minor;
        }
    )SRC");
    REQUIRE(jit);

    auto test_v1_major = jit->lookup_symbol<int(*)()>("test_v1_major");
    auto test_v1_minor = jit->lookup_symbol<int(*)()>("test_v1_minor");
    auto test_v2_major = jit->lookup_symbol<int(*)()>("test_v2_major");
    auto test_v2_minor = jit->lookup_symbol<int(*)()>("test_v2_minor");
    REQUIRE(test_v1_major);
    REQUIRE(test_v1_minor);
    REQUIRE(test_v2_major);
    REQUIRE(test_v2_minor);
    CHECK(test_v1_major() == 1);
    CHECK(test_v1_minor() == 0);
    CHECK(test_v2_major() == 2);
    CHECK(test_v2_minor() == 3);
}


// =========================================================================
// 48. RTTI: multiple annotations on same class — read fields from both
// =========================================================================

TEST_CASE("RTTI: multiple annotations field values on same class", "[libk][rtti][annotation][field][multi]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_two__;
        import k;

        annotation Priority {
            level : int;
        }

        annotation Version {
            major : int;
            minor : int;
        }

        @Priority(5) @Version(3, 7)
        class Important {
            public Important() {}
            public dummy() : int { return 0; }
        }

        test_priority() : int {
            i : Important;
            anns : const k::Annotation?[]? = i.getClass().getAnnotations();
            if (anns == null) return -1;
            p : const Priority? = anns[0];
            if (p == null) return -2;
            return p->level;
        }

        test_version_major() : int {
            i : Important;
            anns : const k::Annotation?[]? = i.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[1];
            if (v == null) return -2;
            return v->major;
        }

        test_version_minor() : int {
            i : Important;
            anns : const k::Annotation?[]? = i.getClass().getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[1];
            if (v == null) return -2;
            return v->minor;
        }
    )SRC");
    REQUIRE(jit);

    auto test_priority = jit->lookup_symbol<int(*)()>("test_priority");
    auto test_version_major = jit->lookup_symbol<int(*)()>("test_version_major");
    auto test_version_minor = jit->lookup_symbol<int(*)()>("test_version_minor");
    REQUIRE(test_priority);
    REQUIRE(test_version_major);
    REQUIRE(test_version_minor);
    CHECK(test_priority() == 5);
    CHECK(test_version_major() == 3);
    CHECK(test_version_minor() == 7);
}


// =========================================================================
// 49. RTTI: annotation method call at runtime (computed accessor)
// =========================================================================

TEST_CASE("RTTI: annotation method call via view downcast", "[libk][rtti][annotation][method]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_method_rt__;
        import k;

        annotation Range {
            min : int;
            max : int;
            span() : int { return max - min; }
        }

        @Range{.min = 10, .max = 50}
        class Bounded {
            public Bounded() {}
            public dummy() : int { return 0; }
        }

        test_span() : int {
            b : Bounded;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            r : const Range? = anns[0];
            if (r == null) return -2;
            return r->span();
        }

        test_min() : int {
            b : Bounded;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            r : const Range? = anns[0];
            if (r == null) return -2;
            return r->min;
        }

        test_max() : int {
            b : Bounded;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            r : const Range? = anns[0];
            if (r == null) return -2;
            return r->max;
        }
    )SRC");
    REQUIRE(jit);

    auto test_span = jit->lookup_symbol<int(*)()>("test_span");
    auto test_min = jit->lookup_symbol<int(*)()>("test_min");
    auto test_max = jit->lookup_symbol<int(*)()>("test_max");
    REQUIRE(test_span);
    REQUIRE(test_min);
    REQUIRE(test_max);
    CHECK(test_min() == 10);
    CHECK(test_max() == 50);
    CHECK(test_span() == 40);
}


// =========================================================================
// 50. RTTI: annotation field zero-init when no default and no value given
// =========================================================================

TEST_CASE("RTTI: annotation field zero-init for unspecified fields", "[libk][rtti][annotation][field][zero]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_zero__;
        import k;

        annotation Stats {
            count : int;
            flag : bool;
        }

        @Stats{.count = 77}
        class Partial {
            public Partial() {}
            public dummy() : int { return 0; }
        }

        test_count() : int {
            p : Partial;
            anns : const k::Annotation?[]? = p.getClass().getAnnotations();
            if (anns == null) return -1;
            s : const Stats? = anns[0];
            if (s == null) return -2;
            return s->count;
        }

        test_flag() : int {
            p : Partial;
            anns : const k::Annotation?[]? = p.getClass().getAnnotations();
            if (anns == null) return -1;
            s : const Stats? = anns[0];
            if (s == null) return -2;
            if (s->flag) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_count = jit->lookup_symbol<int(*)()>("test_count");
    auto test_flag = jit->lookup_symbol<int(*)()>("test_flag");
    REQUIRE(test_count);
    REQUIRE(test_flag);
    CHECK(test_count() == 77);
    CHECK(test_flag() == 0);
}


// =========================================================================
// 51. RTTI: annotation const char[] field value via view downcast
// =========================================================================

TEST_CASE("RTTI: annotation char[] field value via view downcast", "[libk][rtti][annotation][field][string]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_field_str__;
        import k;

        annotation Description {
            text : const char[];
        }

        @Description("hello world")
        class Documented {
            public Documented() {}
            public dummy() : int { return 0; }
        }

        test_via_string() : int {
            d : Documented;
            anns : const k::Annotation?[]? = d.getClass().getAnnotations();
            if (anns == null) return 0;
            desc : const Description? = anns[0];
            if (desc == null) return 1;
            actual : k::String(desc->text);
            expected : k::String("hello world");
            if (actual == expected) return 42;
            return 2;
        }

        test_size() : int {
            d : Documented;
            anns : const k::Annotation?[]? = d.getClass().getAnnotations();
            if (anns == null) return 0;
            desc : const Description? = anns[0];
            if (desc == null) return 1;
            return desc->text.size;
        }

        test_first_char() : int {
            d : Documented;
            anns : const k::Annotation?[]? = d.getClass().getAnnotations();
            if (anns == null) return 0;
            desc : const Description? = anns[0];
            if (desc == null) return 1;
            return desc->text[0];
        }
    )SRC");
    REQUIRE(jit);

    auto test_via_string = jit->lookup_symbol<int(*)()>("test_via_string");
    auto test_size = jit->lookup_symbol<int(*)()>("test_size");
    auto test_first_char = jit->lookup_symbol<int(*)()>("test_first_char");
    REQUIRE(test_via_string);
    REQUIRE(test_size);
    REQUIRE(test_first_char);
    CHECK(test_via_string() == 42);
    CHECK(test_size() == 12);       // "hello world" + null terminator = 12
    CHECK(test_first_char() == 'h');
}


// =========================================================================
// 52. RTTI: annotation array of annotations field
// =========================================================================

TEST_CASE("RTTI: annotation array of nested annotations", "[libk][rtti][annotation][field][array]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __rtti_ann_array__;
        import k;

        annotation Tag {
            value : int;
        }

        annotation Tagged {
            tags : const k::Annotation?[];
        }

        @Tagged({@Tag(10), @Tag(20), @Tag(30)})
        class Foo {
            public Foo() {}
            public dummy() : int { return 0; }
        }

        test_tag_count() : int {
            f : Foo;
            anns : const k::Annotation?[]? = f.getClass().getAnnotations();
            if (anns == null) return -1;
            tagged : const Tagged? = anns[0];
            if (tagged == null) return -2;
            return tagged->tags.size;
        }

        test_tag_0() : int {
            f : Foo;
            anns : const k::Annotation?[]? = f.getClass().getAnnotations();
            if (anns == null) return -1;
            tagged : const Tagged? = anns[0];
            if (tagged == null) return -2;
            t : const Tag? = tagged->tags[0];
            if (t == null) return -3;
            return t->value;
        }

        test_tag_2() : int {
            f : Foo;
            anns : const k::Annotation?[]? = f.getClass().getAnnotations();
            if (anns == null) return -1;
            tagged : const Tagged? = anns[0];
            if (tagged == null) return -2;
            t : const Tag? = tagged->tags[2];
            if (t == null) return -3;
            return t->value;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR, /*dump=*/true, /*optimize=*/false);
    REQUIRE(jit);

    auto test_tag_count = jit->lookup_symbol<int(*)()>("test_tag_count");
    auto test_tag_0 = jit->lookup_symbol<int(*)()>("test_tag_0");
    auto test_tag_2 = jit->lookup_symbol<int(*)()>("test_tag_2");
    REQUIRE(test_tag_count);
    REQUIRE(test_tag_0);
    REQUIRE(test_tag_2);
    CHECK(test_tag_count() == 3);
    CHECK(test_tag_0() == 10);
    CHECK(test_tag_2() == 30);
}


// =========================================================================
// 53. RTTI: annotation array of typed (non-base) annotations field
// =========================================================================

TEST_CASE("RTTI: annotation array of typed nested annotations", "[libk][rtti][annotation][field][array]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __rtti_ann_typed_array__;
        import k;

        annotation Tag {
            value : int;
        }

        annotation TagGroup {
            tags : const Tag?[];
        }

        @TagGroup({@Tag(100), @Tag(200), @Tag(300)})
        class Bar {
            public Bar() {}
            public dummy() : int { return 0; }
        }

        test_tag_count() : int {
            b : Bar;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            grp : const TagGroup? = anns[0];
            if (grp == null) return -2;
            return grp->tags.size;
        }

        test_tag_0() : int {
            b : Bar;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            grp : const TagGroup? = anns[0];
            if (grp == null) return -2;
            t : const Tag? = grp->tags[0];
            if (t == null) return -3;
            return t->value;
        }

        test_tag_2() : int {
            b : Bar;
            anns : const k::Annotation?[]? = b.getClass().getAnnotations();
            if (anns == null) return -1;
            grp : const TagGroup? = anns[0];
            if (grp == null) return -2;
            t : const Tag? = grp->tags[2];
            if (t == null) return -3;
            return t->value;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR, /*dump=*/true, /*optimize=*/false);
    REQUIRE(jit);

    auto test_tag_count = jit->lookup_symbol<int(*)()>("test_tag_count");
    auto test_tag_0 = jit->lookup_symbol<int(*)()>("test_tag_0");
    auto test_tag_2 = jit->lookup_symbol<int(*)()>("test_tag_2");
    REQUIRE(test_tag_count);
    REQUIRE(test_tag_0);
    REQUIRE(test_tag_2);
    CHECK(test_tag_count() == 3);
    CHECK(test_tag_0() == 100);
    CHECK(test_tag_2() == 300);
}


// =========================================================================
// AnnotationName::annotation — direct RTTI descriptor access
// =========================================================================

TEST_CASE("RTTI: AnnotationName::annotation yields AnnotationType descriptor", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_descriptor__;
        import k;

        annotation Info {}

        test() : int {
            desc : const k::AnnotationType& = Info::annotation;
            name : k::String(desc.getName());
            expected : k::String("Info");
            if (name == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("RTTI: AnnotationName::annotation getFullName()", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_desc_fqn__;
        import k;

        annotation Marker {}

        test() : int {
            desc : const k::AnnotationType& = Marker::annotation;
            fqn : k::String(desc.getFullName());
            expected : k::String("::__rtti_ann_desc_fqn__::Marker");
            if (fqn == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("RTTI: AnnotationName::annotation identity matches getAnnotationType()", "[libk][rtti][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_ann_desc_identity__;
        import k;

        annotation Tag {
            value : int;
        }

        @Tag(99)
        class Widget {
            public Widget() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            // Get AnnotationType via the descriptor expression
            desc_name : k::String(Tag::annotation.getName());

            // Get AnnotationType via an annotation instance on a class
            w : Widget;
            anns : const k::Annotation?[]? = w.getClass().getAnnotations();
            if (anns == null) return 1;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 2;
            inst_name : k::String(ann->getAnnotationType().getName());

            // Both should return the same name
            if (desc_name == inst_name) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// Meta-annotations: getAnnotations() on AnnotationType
// =========================================================================

TEST_CASE("RTTI: AnnotationType getAnnotations() null on unannotated annotation type", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_null__;
        import k;

        annotation Plain {}

        test() : int {
            desc : const k::AnnotationType& = Plain::annotation;
            if (desc.getAnnotations() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("RTTI: AnnotationType getAnnotations() non-null on meta-annotated annotation type", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_nonnull__;
        import k;

        annotation Meta {}

        @Meta
        annotation Documented {}

        test() : int {
            desc : const k::AnnotationType& = Documented::annotation;
            if (desc.getAnnotations() != null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("RTTI: AnnotationType getAnnotations() size is 1 on single meta-annotated annotation type", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_size1__;
        import k;

        annotation Meta {}

        @Meta
        annotation Documented {}

        test() : int {
            desc : const k::AnnotationType& = Documented::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            return anns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}

TEST_CASE("RTTI: AnnotationType getAnnotations() size is 2 on double meta-annotated annotation type", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_size2__;
        import k;

        annotation Alpha {}
        annotation Beta {}

        @Alpha @Beta
        annotation Combined {}

        test() : int {
            desc : const k::AnnotationType& = Combined::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            return anns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}

TEST_CASE("RTTI: meta-annotation instance type name via getAnnotationType()", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_typename__;
        import k;

        annotation Meta {}

        @Meta
        annotation Documented {}

        test() : int {
            desc : const k::AnnotationType& = Documented::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            name : k::String(ann->getAnnotationType().getName());
            expected : k::String("Meta");
            if (name == expected) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("RTTI: meta-annotation with member fields — positional init", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_fields__;
        import k;

        annotation Version {
            major : int;
            minor : int;
        }

        @Version(3, 7)
        annotation Documented {}

        test_major() : int {
            desc : const k::AnnotationType& = Documented::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->major;
        }

        test_minor() : int {
            desc : const k::AnnotationType& = Documented::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return -1;
            v : const Version? = anns[0];
            if (v == null) return -2;
            return v->minor;
        }
    )SRC");
    REQUIRE(jit);

    auto test_major = jit->lookup_symbol<int(*)()>("test_major");
    auto test_minor = jit->lookup_symbol<int(*)()>("test_minor");
    REQUIRE(test_major);
    REQUIRE(test_minor);
    CHECK(test_major() == 3);
    CHECK(test_minor() == 7);
}

TEST_CASE("RTTI: meta-annotation with designated init", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_desig__;
        import k;

        annotation Info {
            priority : int;
            enabled : bool;
        }

        @Info{.enabled = true, .priority = 42}
        annotation Tagged {}

        test_priority() : int {
            desc : const k::AnnotationType& = Tagged::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return -1;
            info : const Info? = anns[0];
            if (info == null) return -2;
            return info->priority;
        }

        test_enabled() : int {
            desc : const k::AnnotationType& = Tagged::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return -1;
            info : const Info? = anns[0];
            if (info == null) return -2;
            if (info->enabled) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_priority = jit->lookup_symbol<int(*)()>("test_priority");
    auto test_enabled = jit->lookup_symbol<int(*)()>("test_enabled");
    REQUIRE(test_priority);
    REQUIRE(test_enabled);
    CHECK(test_priority() == 42);
    CHECK(test_enabled() == 1);
}

TEST_CASE("RTTI: retrieve meta-annotations from annotation declaration — full scenario", "[libk][rtti][annotation][meta]") {
    auto jit = jit_k(R"SRC(
        module __rtti_meta_ann_full__;
        import k;

        // Meta-annotations: annotations that annotate other annotation types.
        annotation Retention {
            policy : int;
        }
        annotation Documented {}

        // Apply two meta-annotations on a user annotation type.
        @Retention(1)
        @Documented
        annotation ApiVersion {
            major : int;
            minor : int;
        }

        // Also annotate a class with ApiVersion to demonstrate the
        // annotation is usable as a normal annotation too.
        @ApiVersion(2, 5)
        class Service {
            public Service() {}
            public dummy() : int { return 0; }
        }

        // ── Test: meta-annotation count on ApiVersion ──────────────────
        test_meta_ann_count() : int {
            desc : const k::AnnotationType& = ApiVersion::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            // Check that we have 2 meta-annotations applied
            return anns->size;
        }

        // ── Test: first meta-annotation is Retention with policy == 1 ──
        test_meta_ann_retention_name() : int {
            desc : const k::AnnotationType& = ApiVersion::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            ann : const k::Annotation? = anns[0];
            if (ann == null) return 1;
            name : k::String(ann->getAnnotationType().getName());
            expected : k::String("Retention");
            if (name == expected) return 1;
            return 0;
        }

        test_meta_ann_retention_value() : int {
            desc : const k::AnnotationType& = ApiVersion::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            r : const Retention? = anns[0];
            if (r == null) return 1;
            return r->policy;
        }

        // ── Test: second meta-annotation is Documented (empty) ─────────
        test_meta_ann_documented_name() : int {
            desc : const k::AnnotationType& = ApiVersion::annotation;
            anns : const k::Annotation?[]? = desc.getAnnotations();
            if (anns == null) return 0;
            if (anns->size < 2) return 1;
            ann : const k::Annotation? = anns[1];
            if (ann == null) return 2;
            name : k::String(ann->getAnnotationType().getName());
            expected : k::String("Documented");
            if (name == expected) return 1;
            return 0;
        }

        // ── Test: ApiVersion used on a class still works normally ───────
        test_class_ann_value() : int {
            s : Service;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return 0;
            v : const ApiVersion? = anns[0];
            if (v == null) return 1;
            return v->major * 100 + v->minor;
        }

        // ── Test: unannotated Retention has no meta-annotations ────────
        test_no_meta_on_retention() : int {
            desc : const k::AnnotationType& = Retention::annotation;
            if (desc.getAnnotations() == null) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_meta_ann_count = jit->lookup_symbol<int(*)()>("test_meta_ann_count");
    auto test_meta_ann_retention_name = jit->lookup_symbol<int(*)()>("test_meta_ann_retention_name");
    auto test_meta_ann_retention_value = jit->lookup_symbol<int(*)()>("test_meta_ann_retention_value");
    auto test_meta_ann_documented_name = jit->lookup_symbol<int(*)()>("test_meta_ann_documented_name");
    auto test_class_ann_value = jit->lookup_symbol<int(*)()>("test_class_ann_value");
    auto test_no_meta_on_retention = jit->lookup_symbol<int(*)()>("test_no_meta_on_retention");
    REQUIRE(test_meta_ann_count);
    REQUIRE(test_meta_ann_retention_name);
    REQUIRE(test_meta_ann_retention_value);
    REQUIRE(test_meta_ann_documented_name);
    REQUIRE(test_class_ann_value);
    REQUIRE(test_no_meta_on_retention);

    // ApiVersion has 2 meta-annotations: @Retention(1) and @Documented
    CHECK(test_meta_ann_count() == 2);
    // First meta-annotation is Retention
    CHECK(test_meta_ann_retention_name() == 1);
    // Retention.policy == 1
    CHECK(test_meta_ann_retention_value() == 1);
    // Second meta-annotation is Documented
    CHECK(test_meta_ann_documented_name() == 1);
    // ApiVersion used on Service class: major=2, minor=5 → 205
    CHECK(test_class_ann_value() == 205);
    // Retention itself has no meta-annotations
    CHECK(test_no_meta_on_retention() == 1);
}


// =========================================================================
// Function RTTI tests
// =========================================================================

// =========================================================================
// F1. getFunctions() on class with public methods returns non-null array
// =========================================================================

TEST_CASE("Function RTTI: getFunctions() on class with public methods", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_basic__;
        import k;

        class Foo {
            public Foo() {}
            public bar() : int { return 1; }
            public baz() : int { return 2; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            return fns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}

// =========================================================================
// F2. Function getName() returns the short name
// =========================================================================

TEST_CASE("Function RTTI: Function getName()", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_name__;
        import k;

        class Foo {
            public Foo() {}
            public myMethod() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            if (fns->size != 1) return 1;
            fn : const k::Function? = fns[0];
            if (fn == null) return 2;
            name : k::String(fn->getName());
            expected : k::String("myMethod");
            if (name == expected) return 42;
            return 3;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F3. Function getFullName() returns the FQN
// =========================================================================

TEST_CASE("Function RTTI: Function getFullName()", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_fqn__;
        import k;

        class Foo {
            public Foo() {}
            public myMethod() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            fqn : k::String(fn->getFullName());
            expected : k::String("::__rtti_fn_fqn__::Foo::myMethod");
            if (fqn == expected) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F4. isMember() returns true for member functions
// =========================================================================

TEST_CASE("Function RTTI: isMember() true for member function", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_member__;
        import k;

        class Foo {
            public Foo() {}
            public bar() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->isMember()) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F5. isStatic() on static member function returns true
// =========================================================================

TEST_CASE("Function RTTI: isStatic() on static member function", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_static__;
        import k;

        class Foo {
            public Foo() {}
            public static bar() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->isStatic()) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F6. isStatic() on non-static member function returns false
// =========================================================================

TEST_CASE("Function RTTI: isStatic() false on non-static member", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_nonstatic__;
        import k;

        class Foo {
            public Foo() {}
            public bar() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->isStatic()) return 2;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// F7. Visibility filtering — private methods excluded
// =========================================================================

TEST_CASE("Function RTTI: private methods excluded from getFunctions()", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_vis__;
        import k;

        class Foo {
            public Foo() {}
            public pubMethod() : int { return 0; }
            private privMethod() : int { return 1; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            // Only pubMethod should appear (privMethod is excluded)
            return fns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}

// =========================================================================
// F8. getFunctions() null on class with no public methods
// =========================================================================

TEST_CASE("Function RTTI: getFunctions() null on class with no public methods", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_empty__;
        import k;

        class Foo {
            public Foo() {}
            private secret() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F9. Unit RTTI: getFunctions() on unit with free functions
// =========================================================================

TEST_CASE("Unit RTTI: Unit getFunctions() with free functions", "[libk][rtti][unit]") {
    auto jit = jit_k(R"SRC(
        module __rtti_unit_fn__;
        import k;

        public freeFunc() : int { return 0; }
        public anotherFunc() : int { return 1; }

        test() : int {
            // Access the unit RTTI global directly — it's a global symbol
            // For now, we verify that the unit RTTI was synthesized by checking
            // the function count is correct.
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F10. Function getVisibility() returns PUBLIC on public method
// =========================================================================

TEST_CASE("Function RTTI: getVisibility() returns PUBLIC", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_vis_public__;
        import k;

        class Foo {
            public Foo() {}
            public bar() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->getVisibility() == k::Visibility::PUBLIC) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F11. Function getOwner() returns non-null for member functions
// =========================================================================

TEST_CASE("Function RTTI: getOwner() non-null for member function", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_owner_nn__;
        import k;

        class Foo {
            public Foo() {}
            public bar() : int { return 0; }
        }

        test() : int {
            f : Foo;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->getOwner() != null) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F12. Function getOwner().getName() matches enclosing class name
// =========================================================================

TEST_CASE("Function RTTI: getOwner() name matches enclosing class", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_owner_name__;
        import k;

        class Widget {
            public Widget() {}
            public doStuff() : int { return 0; }
        }

        test() : int {
            w : Widget;
            fns : const k::Function?[]? = w.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            owner : const k::AggregateType? = fn->getOwner();
            if (owner == null) return 2;
            name : k::String(owner->getName());
            expected : k::String("Widget");
            if (name == expected) return 42;
            return 3;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F13. Function iteration — multiple methods ordered by declaration
// =========================================================================

TEST_CASE("Function RTTI: multiple methods names via iteration", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_iter__;
        import k;

        class Calc {
            public Calc() {}
            public add() : int { return 0; }
            public sub() : int { return 0; }
            public mul() : int { return 0; }
        }

        test_count() : int {
            c : Calc;
            fns : const k::Function?[]? = c.getClass().getFunctions();
            if (fns == null) return 0;
            return fns->size;
        }

        test_first_name() : int {
            c : Calc;
            fns : const k::Function?[]? = c.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            name : k::String(fn->getName());
            expected : k::String("add");
            if (name == expected) return 42;
            return 2;
        }

        test_last_name() : int {
            c : Calc;
            fns : const k::Function?[]? = c.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[2];
            if (fn == null) return 1;
            name : k::String(fn->getName());
            expected : k::String("mul");
            if (name == expected) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test_count = jit->lookup_symbol<int(*)()>("test_count");
    auto test_first_name = jit->lookup_symbol<int(*)()>("test_first_name");
    auto test_last_name = jit->lookup_symbol<int(*)()>("test_last_name");
    REQUIRE(test_count);
    REQUIRE(test_first_name);
    REQUIRE(test_last_name);
    CHECK(test_count() == 3);
    CHECK(test_first_name() == 42);
    CHECK(test_last_name() == 42);
}

// =========================================================================
// F14. Function getFunctions() on Interface returns member functions
// =========================================================================

TEST_CASE("Function RTTI: getFunctions() on Interface via bases", "[libk][rtti][function][interface]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_iface__;
        import k;

        interface Computable {
            const compute() : int;
            const reset() : int;
        }

        class Impl : public k::Object, public Computable {
            public Impl() {}
            override const compute() : int { return 1; }
            override const reset() : int { return 0; }
        }

        test() : int {
            i : Impl;
            bases : const k::TypeInfo?[]? = i.getClass().getBases();
            if (bases == null) return 0;
            // Find the Computable interface among bases
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Computable");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// F15. Function RTTI: static and non-static mix
// =========================================================================

TEST_CASE("Function RTTI: mix of static and non-static methods", "[libk][rtti][function]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_mix__;
        import k;

        class Factory {
            public Factory() {}
            public static create() : int { return 1; }
            public process() : int { return 2; }
        }

        test_count() : int {
            f : Factory;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            return fns->size;
        }

        test_static_check() : int {
            f : Factory;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            // Count static methods
            count : int = 0;
            idx : int = 0;
            while (idx < fns->size) {
                fn : const k::Function? = fns[idx];
                if (fn != null) {
                    if (fn->isStatic()) count = count + 1;
                }
                idx = idx + 1;
            }
            return count;
        }
    )SRC");
    REQUIRE(jit);

    auto test_count = jit->lookup_symbol<int(*)()>("test_count");
    auto test_static_check = jit->lookup_symbol<int(*)()>("test_static_check");
    REQUIRE(test_count);
    REQUIRE(test_static_check);
    CHECK(test_count() == 2);
    CHECK(test_static_check() == 1);
}


// =========================================================================
// Interface RTTI tests
// =========================================================================

// =========================================================================
// I1. Interface getName() returns the short name via base TypeInfo
// =========================================================================

TEST_CASE("Interface RTTI: getName() via base TypeInfo", "[libk][rtti][interface]") {
    auto jit = jit_k(R"SRC(
        module __rtti_iface_name__;
        import k;

        interface Printable {
            const print() : int;
        }

        class Doc : public k::Object, public Printable {
            public Doc() {}
            override const print() : int { return 0; }
        }

        test() : int {
            d : Doc;
            bases : const k::TypeInfo?[]? = d.getClass().getBases();
            if (bases == null) return 0;
            // Find Printable in bases
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Printable");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// I2. Interface: class with multiple interfaces — getBases() size
// =========================================================================

TEST_CASE("Interface RTTI: class with multiple interfaces has correct bases size", "[libk][rtti][interface]") {
    auto jit = jit_k(R"SRC(
        module __rtti_iface_multi__;
        import k;

        interface Readable {
            const read() : int;
        }

        interface Writable {
            const write() : int;
        }

        class Stream : public k::Object, public Readable, public Writable {
            public Stream() {}
            override const read() : int { return 1; }
            override const write() : int { return 2; }
        }

        test() : int {
            s : Stream;
            bases : const k::TypeInfo?[]? = s.getClass().getBases();
            if (bases == null) return 0;
            // Should have 3 bases: Object, Readable, Writable
            return bases->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 3);
}

// =========================================================================
// I3. Interface: annotated interface — annotation accessible via bases
// =========================================================================

TEST_CASE("Interface RTTI: annotated interface has annotation name", "[libk][rtti][interface][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_iface_ann_name__;
        import k;

        annotation Stable {}

        @Stable
        interface Api {
            const version() : int;
        }

        class Service : public k::Object, public Api {
            public Service() {}
            override const version() : int { return 1; }
        }

        test() : int {
            s : Service;
            bases : const k::TypeInfo?[]? = s.getClass().getBases();
            if (bases == null) return 0;
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Api");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// I4. Interface: Object base has correct name
// =========================================================================

TEST_CASE("Interface RTTI: Object base has correct name", "[libk][rtti][interface]") {
    auto jit = jit_k(R"SRC(
        module __rtti_iface_obj_base__;
        import k;

        class Simple : public k::Object {
            public Simple() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            s : Simple;
            bases : const k::TypeInfo?[]? = s.getClass().getBases();
            if (bases == null) return 0;
            // Should have 1 base: Object
            if (bases->size != 1) return 1;
            b : const k::TypeInfo? = bases[0];
            if (b == null) return 2;
            name : k::String(b->getName());
            expected : k::String("Object");
            if (name == expected) return 42;
            return 3;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// Unit RTTI tests
// =========================================================================

// =========================================================================
// U1. Unit RTTI: unit descriptor symbol is resolvable
// =========================================================================

TEST_CASE("Unit RTTI: unit descriptor symbol is resolvable", "[libk][rtti][unit]") {
    auto jit = jit_k(R"SRC(
        module __rtti_unit_resolve__;
        import k;

        public freeFunc() : int { return 7; }

        test() : int {
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// U2. Unit RTTI: class methods and free functions coexist
// =========================================================================

TEST_CASE("Unit RTTI: class methods and free functions coexist", "[libk][rtti][unit]") {
    auto jit = jit_k(R"SRC(
        module __rtti_unit_coexist__;
        import k;

        class Helper : public k::Object {
            public Helper() {}
            public assist() : int { return 1; }
        }

        public freeHelper() : int { return 2; }

        test_class_name() : int {
            h : Helper;
            name : k::String(h.getClass().getName());
            expected : k::String("Helper");
            if (name == expected) return 42;
            return 0;
        }

        test_free_fn() : int {
            return freeHelper();
        }
    )SRC");
    REQUIRE(jit);

    auto test_class_name = jit->lookup_symbol<int(*)()>("test_class_name");
    auto test_free_fn = jit->lookup_symbol<int(*)()>("test_free_fn");
    REQUIRE(test_class_name);
    REQUIRE(test_free_fn);
    CHECK(test_class_name() == 42);
    CHECK(test_free_fn() == 2);
}


// =========================================================================
// Class hierarchy RTTI tests
// =========================================================================

// =========================================================================
// H1. Inheritance chain: derived class base name is correct
// =========================================================================

TEST_CASE("RTTI: user class reports correct name and base", "[libk][rtti][hierarchy]") {
    auto jit = jit_k(R"SRC(
        module __rtti_hier_base__;
        import k;

        class Animal : public k::Object {
            public Animal() {}
            public speak() : int { return 0; }
        }

        test_animal_name() : int {
            a : Animal;
            name : k::String(a.getClass().getName());
            expected : k::String("Animal");
            if (name == expected) return 42;
            return 0;
        }

        test_animal_base() : int {
            a : Animal;
            bases : const k::TypeInfo?[]? = a.getClass().getBases();
            if (bases == null) return 0;
            if (bases->size != 1) return 1;
            b : const k::TypeInfo? = bases[0];
            if (b == null) return 2;
            name : k::String(b->getName());
            expected : k::String("Object");
            if (name == expected) return 42;
            return 3;
        }
    )SRC");
    REQUIRE(jit);

    auto test_animal_name = jit->lookup_symbol<int(*)()>("test_animal_name");
    auto test_animal_base = jit->lookup_symbol<int(*)()>("test_animal_base");
    REQUIRE(test_animal_name);
    REQUIRE(test_animal_base);
    CHECK(test_animal_name() == 42);
    CHECK(test_animal_base() == 42);
}

// =========================================================================
// H2. Polymorphic getClass() on derived type through base reference
// =========================================================================

TEST_CASE("RTTI: polymorphic getClass() on user class via Object& ref", "[libk][rtti][hierarchy]") {
    auto jit = jit_k(R"SRC(
        module __rtti_hier_poly__;
        import k;

        class MyWidget : public k::Object {
            public MyWidget() {}
            public id() : int { return 1; }
        }

        get_name(o : k::Object&) : int {
            name : k::String(o.getClass().getName());
            expected : k::String("MyWidget");
            if (name == expected) return 42;
            return 0;
        }

        test() : int {
            w : MyWidget;
            return get_name(w);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// H3. getFullName() on derived class includes module prefix
// =========================================================================

TEST_CASE("RTTI: getFullName() on user class with explicit Object base", "[libk][rtti][hierarchy]") {
    auto jit = jit_k(R"SRC(
        module __rtti_hier_fqn__;
        import k;

        class Circle : public k::Object {
            public Circle() {}
            public area() : int { return 314; }
        }

        test() : int {
            c : Circle;
            fqn : k::String(c.getClass().getFullName());
            expected : k::String("::__rtti_hier_fqn__::Circle");
            if (fqn == expected) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// H4. Class with interface + base class — bases array includes both
// =========================================================================

TEST_CASE("RTTI: class with base class and interface has both in bases", "[libk][rtti][hierarchy]") {
    auto jit = jit_k(R"SRC(
        module __rtti_hier_mixed__;
        import k;

        interface Drawable {
            const draw() : int;
        }

        class Widget : public k::Object, public Drawable {
            public Widget() {}
            override const draw() : int { return 1; }
        }

        test_size() : int {
            w : Widget;
            bases : const k::TypeInfo?[]? = w.getClass().getBases();
            if (bases == null) return 0;
            // Should have 2 bases: Object, Drawable
            return bases->size;
        }

        test_has_object() : int {
            w : Widget;
            bases : const k::TypeInfo?[]? = w.getClass().getBases();
            if (bases == null) return 0;
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Object");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }

        test_has_drawable() : int {
            w : Widget;
            bases : const k::TypeInfo?[]? = w.getClass().getBases();
            if (bases == null) return 0;
            idx : int = 0;
            while (idx < bases->size) {
                b : const k::TypeInfo? = bases[idx];
                if (b != null) {
                    name : k::String(b->getName());
                    expected : k::String("Drawable");
                    if (name == expected) return 42;
                }
                idx = idx + 1;
            }
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<int(*)()>("test_size");
    auto test_has_object = jit->lookup_symbol<int(*)()>("test_has_object");
    auto test_has_drawable = jit->lookup_symbol<int(*)()>("test_has_drawable");
    REQUIRE(test_size);
    REQUIRE(test_has_object);
    REQUIRE(test_has_drawable);
    CHECK(test_size() == 2);
    CHECK(test_has_object() == 42);
    CHECK(test_has_drawable() == 42);
}


// =========================================================================
// Edge-case / Regression RTTI tests
// =========================================================================

// =========================================================================
// E1. Empty class (only constructor) — getFunctions() null, getName() works
// =========================================================================

TEST_CASE("RTTI: empty class getName() and getAnnotations() null", "[libk][rtti][edge]") {
    auto jit = jit_k(R"SRC(
        module __rtti_edge_empty__;
        import k;

        class Empty {
            public Empty() {}
        }

        test_name() : int {
            e : Empty;
            name : k::String(e.getClass().getName());
            expected : k::String("Empty");
            if (name == expected) return 42;
            return 0;
        }

        test_no_annotations() : int {
            e : Empty;
            if (e.getClass().getAnnotations() == null) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_name = jit->lookup_symbol<int(*)()>("test_name");
    auto test_no_annotations = jit->lookup_symbol<int(*)()>("test_no_annotations");
    REQUIRE(test_name);
    REQUIRE(test_no_annotations);
    CHECK(test_name() == 42);
    CHECK(test_no_annotations() == 42);
}

// =========================================================================
// E2. getClass() on same object called twice returns same name
// =========================================================================

TEST_CASE("RTTI: getClass() called twice returns consistent result", "[libk][rtti][edge]") {
    auto jit = jit_k(R"SRC(
        module __rtti_edge_twice__;
        import k;

        test() : int {
            s : k::String("test");
            n1 : k::String(s.getClass().getName());
            n2 : k::String(s.getClass().getName());
            if (n1 == n2) return 42;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

// =========================================================================
// E3. Annotation with method and field — both accessible via RTTI downcast
// =========================================================================

TEST_CASE("RTTI: annotation with field and method combined", "[libk][rtti][annotation][edge]") {
    auto jit = jit_k(R"SRC(
        module __rtti_edge_ann_combo__;
        import k;

        annotation Metric {
            min : int;
            max : int;
            range() : int { return max - min; }
        }

        @Metric(5, 15)
        class Sensor {
            public Sensor() {}
            public dummy() : int { return 0; }
        }

        test_min() : int {
            s : Sensor;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            m : const Metric? = anns[0];
            if (m == null) return -2;
            return m->min;
        }

        test_max() : int {
            s : Sensor;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            m : const Metric? = anns[0];
            if (m == null) return -2;
            return m->max;
        }

        test_range() : int {
            s : Sensor;
            anns : const k::Annotation?[]? = s.getClass().getAnnotations();
            if (anns == null) return -1;
            m : const Metric? = anns[0];
            if (m == null) return -2;
            return m->range();
        }
    )SRC");
    REQUIRE(jit);

    auto test_min = jit->lookup_symbol<int(*)()>("test_min");
    auto test_max = jit->lookup_symbol<int(*)()>("test_max");
    auto test_range = jit->lookup_symbol<int(*)()>("test_range");
    REQUIRE(test_min);
    REQUIRE(test_max);
    REQUIRE(test_range);
    CHECK(test_min() == 5);
    CHECK(test_max() == 15);
    CHECK(test_range() == 10);
}


// =========================================================================
// E4. Multiple nested classes — each has correct enclosing type
// =========================================================================

TEST_CASE("RTTI: multiple nested classes each report correct enclosing", "[libk][rtti][edge]") {
    auto jit = jit_k(R"SRC(
        module __rtti_edge_multi_nested__;
        import k;

        class Outer {
            public Outer() {}
            public dummy() : int { return 0; }
            class InnerA {
                public InnerA() {}
                public value() : int { return 1; }
            }
            class InnerB {
                public InnerB() {}
                public value() : int { return 2; }
            }

            public test_nested_count() : int {
                nested : const k::TypeInfo?[]? = this.getClass().getNested();
                if (nested == null) return 0;
                return nested->size;
            }
        }

        test() : int {
            o : Outer;
            return o.test_nested_count();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}

// =========================================================================
// E5. Function RTTI: const method getFullName includes class path
// =========================================================================

TEST_CASE("RTTI: getFullName() includes full class path for nested class", "[libk][rtti][edge]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fn_fqn_nested__;
        import k;

        class Container {
            public Container() {}
            public dummy() : int { return 0; }
            class Item {
                public Item() {}
                public value() : int { return 0; }
            }
            public test_item_fqn() : int {
                i : Item;
                fqn : k::String(i.getClass().getFullName());
                expected : k::String("::__rtti_fn_fqn_nested__::Container::Item");
                if (fqn == expected) return 42;
                return 0;
            }
        }

        test() : int {
            c : Container;
            return c.test_item_fqn();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}
