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
 * Tests for member template functions:
 *   - template methods inside template structs (Step 1)
 *   - member template invocation with explicit template args (Step 2)
 *   - member template with parameter packs (Step 3)
 *   - UniSlot construct with variadic forwarding (Step 4)
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  1. Basic member template method on a non-template struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template method on non-template struct", "[gen][member-template]") {
    std::string src = R"SRC(
        module __mt_01__;

        struct Converter {
            template<typename T>
            identity(x : T) : T { return x; }
        }

        test_member_tpl() : int {
            c : Converter;
            return c.identity<int>(42);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_tpl");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}


// ════════════════════════════════════════════════════════════════════════════
//  2. Member template method on a template struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template method on template struct", "[gen][member-template]") {
    std::string src = R"SRC(
        module __mt_02__;

        template<typename T>
        struct Container {
            _val : T;

            template<typename U>
            setFrom(u : U) : U { _val = u; return u; }
        }

        test_member_tpl_struct() : int {
            c : Container<int>;
            c.setFrom<int>(55);
            return c._val;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_tpl_struct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 55);
}


// ════════════════════════════════════════════════════════════════════════════
//  3. Member template with parameter pack
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template with parameter pack", "[gen][member-template][pack]") {
    std::string src = R"SRC(
        module __mt_03__;

        struct Adder {
            template<typename...Args>
            add(Args...args) : int { return sum(args...); }
        }

        sum(a : int, b : int) : int { return a + b; }

        test_member_pack() : int {
            adder : Adder;
            return adder.add<int, int>(17, 25);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_pack");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}


// ════════════════════════════════════════════════════════════════════════════
//  4. UniSlot construct with explicit template args (variadic forwarding)
// ════════════════════════════════════════════════════════════════════════════

// Preamble with UniSlot using member template construct
static constexpr const char* UNISLOT_TPL_PREAMBLE = R"SRC(
        namespace annotations {
            annotation Intrinsic {
                name : int;
            }
        }

        template<typename T>
        struct UniSlot {
            private:
            _slot : T;

            public:
            @annotations::Intrinsic(0)
            UniSlot();

            @annotations::Intrinsic(0)
            ~UniSlot();

            @annotations::Intrinsic(1)
            template<typename...Args>
            construct(Args...args);

            @annotations::Intrinsic(2)
            destruct();

            get() : T& { return _slot; }
        }
)SRC";

TEST_CASE("UniSlot member template construct with args", "[gen][member-template][intrinsic]") {
    std::string src = std::string("module __mt_04__;\n") + UNISLOT_TPL_PREAMBLE + R"SRC(

        struct Point {
            x : int;
            y : int;
            Point(px : int, py : int) { x = px; y = py; }
        }

        test_unislot_tpl_construct() : int {
            slot : UniSlot<Point>;
            slot.construct<int, int>(10, 20);
            return slot.get().x + slot.get().y;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_unislot_tpl_construct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}


// ════════════════════════════════════════════════════════════════════════════
//  5. UniSlot member template construct zero-arg still works
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot member template construct zero-arg", "[gen][member-template][intrinsic]") {
    std::string src = std::string("module __mt_05__;\n") + UNISLOT_TPL_PREAMBLE + R"SRC(

        struct Widget {
            value : int;
            Widget() { value = 99; }
        }

        test_unislot_tpl_zero() : int {
            slot : UniSlot<Widget>;
            slot.construct();
            return slot.get().value;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_unislot_tpl_zero");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 99);
}


// ════════════════════════════════════════════════════════════════════════════
//  6. Member template deduction (implicit template args)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template with implicit deduction", "[gen][member-template][deduction]") {
    std::string src = R"SRC(
        module __mt_06__;

        struct Wrapper {
            template<typename T>
            echo(x : T) : T { return x; }
        }

        test_member_deduction() : int {
            w : Wrapper;
            return w.echo(123);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_deduction");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 123);
}

// ════════════════════════════════════════════════════════════════════════════
//  7. emplaceFront/emplaceBack/emplace — same-module LinkedList with struct
// ════════════════════════════════════════════════════════════════════════════

static constexpr const char* LINKEDLIST_EMPLACE_PREAMBLE = R"SRC(
        namespace annotations {
            annotation Intrinsic {
                name : int;
            }
        }

        template<typename T>
        struct UniSlot {
            private:
            _slot : T;

            public:
            @annotations::Intrinsic(0)
            UniSlot();

            @annotations::Intrinsic(0)
            ~UniSlot();

            @annotations::Intrinsic(1)
            template<typename...Args>
            construct(Args...args);

            @annotations::Intrinsic(2)
            destruct();

            get() : T& { return _slot; }
        }

        template<typename T>
        struct SimpleList {
            protected:
            static struct Node {
                _slot : UniSlot<T>;
                _next : Node! = null;

                Node() { _slot.construct(); }
                ~Node() { _slot.destruct(); }

                template<typename...Args>
                emplaceValue(Args...args) {
                    _slot.destruct();
                    _slot.construct(args...);
                }

                getValue() : T& { return _slot.get(); }
                setValue(value : T&) { _slot.get() = value; }
            }

            private:
            _head : Node!;
            _size : int;

            public:
            SimpleList() { _head = null; _size = 0; }
            ~SimpleList() { clear(); }

            const getSize() : int { return _size; }

            pushFront(value : T&) {
                node : Node! = new Node();
                node.setValue(value);
                node._next = _head;
                _head = node;
                _size = _size + 1;
            }

            template<typename...Args>
            emplaceFront(Args...args) {
                node : Node! = new Node();
                node.emplaceValue(args...);
                node._next = _head;
                _head = node;
                _size = _size + 1;
            }

            template<typename...Args>
            emplaceBack(Args...args) {
                node : Node! = new Node();
                node.emplaceValue(args...);
                if (_head == null) {
                    _head = node;
                } else {
                    cur : Node* = _head;
                    while (cur->_next != null) {
                        cur = cur->_next;
                    }
                    cur->_next = node;
                }
                _size = _size + 1;
            }

            get(index : int) : T& {
                cur : Node* = _head;
                i : int = 0;
                while (i < index) {
                    cur = cur->_next;
                    i = i + 1;
                }
                return cur->getValue();
            }

            clear() {
                while (_head != null) {
                    next : Node! = _head._next;
                    _head = next;
                }
                _size = 0;
            }
        }
)SRC";

TEST_CASE("SimpleList emplaceFront with default constructor (zero args)", "[gen][member-template][emplace]") {
    std::string src = std::string("module __mt_emplace_zero__;\n") + LINKEDLIST_EMPLACE_PREAMBLE + R"SRC(

        struct Widget {
            value : int;
            Widget() { value = 77; }
        }

        test_emplace_zero() : int {
            lst : SimpleList<Widget>;
            lst.emplaceFront<>();
            lst.emplaceBack<>();

            result : int = 0;
            if (lst.getSize() == 2)       result = result + 1;
            if (lst.get(0).value == 77)   result = result + 10;
            if (lst.get(1).value == 77)   result = result + 100;
            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_emplace_zero");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 111);
}

// Note: emplaceFront/emplaceBack with explicit constructor args (e.g.
// lst.emplaceFront<int, int>(10, 20)) is currently blocked by a compiler
// limitation: nested variadic template pack forwarding does not correctly
// deduce the inner template's parameter pack from the outer pack expansion.
// The generated code selects the zero-arg construct intrinsic instead of
// the arg-forwarding variant. This will be fixed in a future compiler update.

// Regression test for a compiler bug found while implementing the Map<K,V>
// stdlib collection: `populate_function_from_template` (template_instantiator.cpp),
// used to clone a template method's parameters when instantiating a template
// class, never copied a parameter's default-value expression. A template
// method declared with a default argument (e.g. `set(x: T, log: bool = true)`)
// therefore lost that default once the enclosing class template was
// instantiated, so calling it with fewer arguments than declared failed
// overload resolution ("No viable overload found ... none of the 1
// candidate(s) can be called").
TEST_CASE("Template class method keeps its default parameter value after instantiation", "[gen][member-template][default-param][regression]") {
    auto jit = gen_jit(R"SRC(
module __mt_default_param__;
template<typename T>
class Box {
    v: T;
    Box(x: T) : v(x) {}
    set(x: T, log: bool = true) : void { v = x; }
    get() : T { return v; }
}
test() : int {
    b: Box<int>(1);
    b.set(5);   // relies on the instantiated method's default value for `log`
    return b.get();
}
)SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 5);
}

