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
 * Tests for the 'friend' declaration.
 *
 * Covered scenarios:
 *  - Friend aggregate accessing protected member variable via dot.
 *  - Friend aggregate accessing protected member variable via arrow.
 *  - Friend aggregate accessing protected member function (dot).
 *  - Friend aggregate accessing protected member function (arrow).
 *  - Friend aggregate accessing protected constructor.
 *  - Friend free function accessing protected members.
 *  - Friend with type filter (friend struct X, friend class X).
 *  - Type filter mismatch rejection.
 *  - Friendship does NOT inherit to subclass of friend.
 *  - Friendship does NOT propagate to subclass of declaring aggregate.
 *  - Friendship does NOT propagate to nested aggregates of friend.
 *  - Non-friend rejected (baseline).
 *  - Friend declaration outside aggregate body → error.
 *  - Friend with qualified name (namespace::Struct).
 *  - Friend aggregate with static member function.
 *  - Multiple friend declarations on same aggregate.
 *  - Friend of Base accesses inherited protected member via Derived reference.
 *  - Template friend: private member access via friend Getter<T>.
 *  - Template friend: friend declaration with struct filter.
 *  - Template friend: unparameterized friend grants access to all instantiations.
 *  - Friend declaration: access private (non-template) member.
 *  - Template friend: non-friend cannot access private member (negative test).
 *  - Non-template struct, explicit concrete friend arg grants access.
 *  - Non-template struct, wrong concrete arg rejects access (negative).
 *  - Free function template with inherited param as friend grants access.
 *  - Unparameterized free function template friend grants access to all instantiations.
 *  - Free function template wrong param rejects access (negative).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ── Friend aggregate — access protected member variable via dot ─────────────

TEST_CASE("Friend aggregate — access protected member variable via dot", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_01;
        struct Secret {
            friend Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            peek(s : Secret&) : int {
                return s.val;
            }
        }
        test() : int {
            s : Secret;
            b : Buddy;
            return b.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend aggregate — access protected member variable via arrow ───────────

TEST_CASE("Friend aggregate — access protected member variable via arrow", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_02;
        struct Secret {
            friend Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            peek(s : Secret*) : int {
                return s->val;
            }
        }
        test() : int {
            s : Secret;
            b : Buddy;
            return b.peek(&s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend aggregate — access protected member function ─────────────────────

TEST_CASE("Friend aggregate — access protected member function", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_03;
        struct Secret {
            friend Buddy;
        protected:
            compute() : int { return 42; }
        }
        struct Buddy {
            peek(s : Secret&) : int {
                return s.compute();
            }
        }
        test() : int {
            s : Secret;
            b : Buddy;
            return b.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend aggregate — access protected constructor ─────────────────────────

TEST_CASE("Friend aggregate — access protected constructor", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_04;
        struct Secret {
            friend Builder;
        protected:
            val : int;
            Secret(v : int) : val(v) {}
        public:
            get() : int { return this.val; }
        }
        struct Builder {
            build() : int {
                s : Secret(42);
                return s.get();
            }
        }
        test() : int {
            b : Builder;
            return b.build();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Base module friend aggregate can call a private constructor without imports",
          "[gen][friend][private][regression]") {
    auto comp = k::compiler::create();
    REQUIRE_NOTHROW(comp->parse_source("friend-private-ctor.k", R"SRC(
        module k;
        struct Secret {
        private:
            _value : int;
            Secret(value : int) : _value(value) {}
            friend Builder;
        public:
            get() : int { return _value; }
        }
        struct Builder {
            static build() : int {
                value : Secret(42);
                return value.get();
            }
        }
        test() : int { return Builder::build(); }
    )SRC", false));
    REQUIRE(comp->get_unit());
    REQUIRE(comp->get_unit()->get_imports().empty());
}

TEST_CASE("Later namespace friends decode private native constructors across sources",
          "[gen][friend][private][regression]") {
    auto jit = gen_jit_multi({
        {"address.k", R"SRC(
            module gen_friend_native_decode;
            namespace io {
                struct NativeValue { value : int; }
                class Address {
                private:
                    _value : int;
                    Address(native : NativeValue*) : _value(native->value) {}
                    friend Packet;
                    friend Socket;
                public:
                    Address() : _value(0) {}
                    Address(other : const Address&) : _value(other._value) {}
                    const value() : int { return _value; }
                }
            }
        )SRC"},
        {"packet.k", R"SRC(
            module gen_friend_native_decode;
            namespace io {
                class Packet {
                private:
                    _source : Address;
                    Packet(source : const Address&) : _source(source) {}
                    static decode(native : NativeValue*) : Packet {
                        source : Address(native);
                        return Packet(source);
                    }
                    friend Socket;
                public:
                    Packet() : _source() {}
                    Packet(other : const Packet&) : _source(other._source) {}
                    const value() : int { return _source.value(); }
                }
                class Socket {
                public:
                    static receive(native : NativeValue*) : Packet {
                        return Packet::decode(native);
                    }
                    static receiveCallback(native : NativeValue*) : Packet {
                        decoder : *(NativeValue*):Packet = Packet::decode;
                        return decoder(native);
                    }
                    static bound(native : NativeValue*) : Address {
                        return Address(native);
                    }
                }
            }
            test() : int {
                native : io::NativeValue;
                native.value = 42;
                packet : io::Packet = io::Socket::receive(&native);
                callbackPacket : io::Packet = io::Socket::receiveCallback(&native);
                address : io::Address = io::Socket::bound(&native);
                return packet.value() + callbackPacket.value() + address.value();
            }
        )SRC"}
    });
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 126);
}

TEST_CASE("Later namespace friend initializes private constructors in member defaults",
          "[gen][friend][private][regression]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_member_defaults;
        namespace io {
            struct Secret {
            private:
                _value : int;
                Secret(value : int) : _value(value) {}
                friend Builder;
            public:
                get() : int { return _value; }
            }
            struct Builder {
                value : Secret(42);
                Builder() {}
                Builder(v : int) : value(v) {}
                static make() : Secret { return Secret(21); }
            }
        }
        test() : int {
            defaultValue : io::Builder;
            explicitValue : io::Builder(7);
            returnedValue : io::Secret = io::Builder::make();
            return defaultValue.value.get() + explicitValue.value.get() + returnedValue.get();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 70);
}

TEST_CASE("Qualified constructor friends resolve from lexical and module roots",
          "[gen][friend][private][regression]") {
    const auto target = GENERATE(
        "helpers::Builder",
        "::gen_friend_qualified_ctor::nested::io::helpers::Builder",
        "gen_friend_qualified_ctor::nested::io::helpers::Builder");
    CAPTURE(target);
    const std::string source = R"SRC(
        module gen_friend_qualified_ctor::nested;
        namespace io {
            struct Secret {
            private:
                _value : int;
                Secret(value : int) : _value(value) {}
                friend )SRC" + std::string(target) + R"SRC(;
            public:
                get() : int { return _value; }
            }
            namespace helpers {
                struct Builder {
                    static build() : int {
                        value : Secret(42);
                        return value.get();
                    }
                }
            }
        }
        test() : int { return io::helpers::Builder::build(); }
    )SRC";
    auto jit = gen_jit(source);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Later namespace free friend calls private constructors and static functions",
          "[gen][friend][private][regression]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_free_decode;
        namespace io {
            struct Secret {
            private:
                _value : int;
                Secret(value : int) : _value(value) {}
                static decodedValue() : int { return 42; }
                friend decode;
            public:
                get() : int { return _value; }
            }
            decode() : Secret { return Secret(Secret::decodedValue()); }
        }
        test() : int {
            value : io::Secret = io::decode();
            return value.get();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Private constructors remain restricted to matching direct friends",
          "[gen][friend][private][error][regression]") {
    const auto declarations = GENERATE(
        R"SRC(
            struct Buddy {}
            struct Outsider {
                static steal() : Secret { return Secret(42); }
            }
        )SRC",
        R"SRC(
            struct Buddy {}
            struct Derived : public Buddy {
                static steal() : Secret { return Secret(42); }
            }
        )SRC",
        R"SRC(
            struct Buddy {
                struct Nested {
                    static steal() : Secret { return Secret(42); }
                }
            }
        )SRC",
        R"SRC(
            class Buddy {
            public:
                static steal() : Secret { return Secret(42); }
            }
        )SRC");
    CAPTURE(declarations);
    const std::string source = R"SRC(
        module gen_friend_ctor_denied;
        namespace io {
            struct Secret {
            private:
                Secret(value : int) {}
                friend struct Buddy;
            }
    )SRC" + std::string(declarations) + R"SRC(
        }
    )SRC";
    REQUIRE_THROWS_MATCHES(gen_jit_throws(source), k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const k::log::compiler_error& error) {
                return error.get_diagnostic().code == 0x080Cu;
            }, "private constructor visibility diagnostic"));
}

TEST_CASE("Namespace constructor friend does not grant access to same-named root aggregate",
          "[gen][friend][private][error][regression]") {
    REQUIRE_THROWS_MATCHES(gen_jit_throws(R"SRC(
        module gen_friend_ctor_shadow;
        namespace io {
            struct Secret {
            private:
                Secret(value : int) {}
                friend Buddy;
            }
            struct Buddy {}
        }
        struct Buddy {
            static steal() : io::Secret { return io::Secret(42); }
        }
    )SRC"), k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const k::log::compiler_error& error) {
                return error.get_diagnostic().code == 0x080Cu;
            }, "private constructor visibility diagnostic"));
}

// ── Friend free function — access protected members ─────────────────────────

TEST_CASE("Friend free function — access protected member variable", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_05;
        struct Secret {
            friend peek;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        peek(s : Secret&) : int {
            return s.val;
        }
        test() : int {
            s : Secret;
            return peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend with type filter — struct filter matches struct ──────────────────

TEST_CASE("Friend with struct filter — match correct type", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_06;
        struct Secret {
            friend struct Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            peek(s : Secret&) : int {
                return s.val;
            }
        }
        test() : int {
            s : Secret;
            b : Buddy;
            return b.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend with type filter — mismatch (friend struct X but X is a class) ───

TEST_CASE("Friend with struct filter — mismatch rejects access", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_07;
        struct Secret {
            friend struct Impostor;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        class Impostor {
        public:
            Impostor() {}
            peek(s : Secret&) : int {
                return s.val;
            }
        }
    )SRC"));
}

// ── Friendship does NOT inherit — subclass of friend has no access ──────────

TEST_CASE("Friendship does NOT inherit — subclass of friend denied", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_08;
        struct Secret {
            friend Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            peek(s : Secret&) : int {
                return s.val;
            }
        }
        struct SubBuddy : public Buddy {
            SubBuddy() {}
            steal(s : Secret&) : int {
                return s.val;
            }
        }
    )SRC"));
}

// ── Friendship does NOT propagate to subclass of declaring aggregate ────────

TEST_CASE("Subclass of declaring aggregate does NOT inherit friendship", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_09;
        struct Base {
            friend Buddy;
        protected:
            val : int;
        public:
            Base() : val(42) {}
        }
        struct Derived : public Base {
        protected:
            own_val : int;
        public:
            Derived() : own_val(99) {}
        }
        struct Buddy {
            steal(d : Derived&) : int {
                return d.own_val;
            }
        }
    )SRC"));
}

// ── Friendship does NOT propagate to nested aggregates ──────────────────────

TEST_CASE("Nested aggregate of friend has no access", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_10;
        struct Secret {
            friend Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            struct Inner {
                steal(s : Secret&) : int {
                    return s.val;
                }
            }
        }
    )SRC"));
}

// ── Non-friend rejected (baseline) ──────────────────────────────────────────

TEST_CASE("Non-friend struct rejected — baseline", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_11;
        struct Secret {
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Stranger {
            steal(s : Secret&) : int {
                return s.val;
            }
        }
    )SRC"));
}

// ── Friend with qualified name (namespace::Struct) ──────────────────────────

TEST_CASE("Friend with qualified name — namespace::Struct", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_12;
        namespace helpers {
            struct Buddy {
                peek(s : Secret&) : int {
                    return s.val;
                }
            }
        }
        struct Secret {
            friend helpers::Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        test() : int {
            s : Secret;
            b : helpers::Buddy;
            return b.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend with class filter — class keyword matches class ──────────────────

TEST_CASE("Friend with class filter — match correct type", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_13;
        struct Secret {
            friend class Ally;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        class Ally {
        public:
            Ally() {}
            peek(s : Secret&) : int {
                return s.val;
            }
        }
        test() : int {
            s : Secret;
            a : Ally;
            return a.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend static member function of friend aggregate ───────────────────────

TEST_CASE("Friend aggregate — static member accesses protected member", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_14;
        struct Secret {
            friend Buddy;
        protected:
            val : int;
        public:
            Secret() : val(42) {}
        }
        struct Buddy {
            static peek(s : Secret&) : int {
                return s.val;
            }
        }
        test() : int {
            s : Secret;
            return Buddy::peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Friend aggregate — access protected member function via arrow ───────────

TEST_CASE("Friend aggregate — access protected member function via arrow", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_15;
        struct Secret {
            friend Buddy;
        protected:
            compute() : int { return 42; }
        }
        struct Buddy {
            peek(s : Secret*) : int {
                return s->compute();
            }
        }
        test() : int {
            s : Secret;
            b : Buddy;
            return b.peek(&s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── Multiple friend declarations ────────────────────────────────────────────

TEST_CASE("Multiple friend declarations — both friends have access", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_16;
        struct Secret {
            friend AllyA;
            friend AllyB;
        protected:
            val : int;
        public:
            Secret() : val(21) {}
        }
        struct AllyA {
            peek(s : Secret&) : int {
                return s.val;
            }
        }
        struct AllyB {
            peek(s : Secret&) : int {
                return s.val * 2;
            }
        }
        test() : int {
            s : Secret;
            a : AllyA;
            b : AllyB;
            return a.peek(s) + b.peek(s);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // AllyA.peek returns 21, AllyB.peek returns 42
    REQUIRE(test() == 63);
}

// ── Friend at namespace scope — error ───────────────────────────────────────

TEST_CASE("Friend declaration at namespace scope — error", "[gen][friend][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_friend_17;
        friend SomeStruct;
        struct SomeStruct {
            val : int;
        }
    )SRC"));
}

// ── Friend accessing inherited protected member through derived reference ───

TEST_CASE("Friend of Base accesses inherited protected member via Derived ref", "[gen][friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_18;
        struct Base {
            friend Buddy;
        protected:
            val : int;
        public:
            Base() : val(42) {}
        }
        struct Derived : public Base {
        public:
            Derived() {}
        }
        struct Buddy {
            peek(d : Derived&) : int {
                return d.val;
            }
        }
        test() : int {
            d : Derived;
            b : Buddy;
            return b.peek(d);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}


// ── Template friend: private member access via friend<T> ────────────────────

TEST_CASE("Template friend — access private member via friend Getter<T>", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_19;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend Getter<T>;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        struct Getter {
            get(b : Box<T>&) : T { return b._val; }
        }
        test() : int {
            b : Box<int>(42);
            g : Getter<int>;
            if (g.get(b) != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — private constructor and methods through dot and arrow",
          "[gen][friend][template-friend][private-template-friend]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"SRC(
        module gen_friend_private_calls;

        template<typename T>
        struct Block {
            friend Owner<T>;
        private:
            _tag : T*;
            _value : int;
            Block(v : int) : _value(v) {}
            get() : int { return _value; }
        }

        template<typename T>
        struct Owner {
            read(v : int) : int {
                block : Block<T>(v);
                ptr : Block<T>* = &block;
                return block.get() + ptr->get();
            }
        }

        struct Tag {}

        test() : int {
            owner : Owner<Tag>;
            return owner.read(21);
        }
    )SRC", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Template friend — private method rejects a different template argument",
          "[gen][friend][template-friend][private-template-friend][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module gen_friend_private_wrong_arg;

        template<typename T>
        struct Block {
            friend Reader<int>;
        private:
            get() : int { return 42; }
        }

        template<typename T>
        struct Reader {
            read(block : Block<T>*) : int { return block->get(); }
        }

        test() : int {
            block : Block<short>;
            reader : Reader<short>;
            return reader.read(&block);
        }
    )SRC", nullptr));
}

TEST_CASE("Template friend — friend declaration with struct filter", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_20;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend struct Getter<T>;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        struct Getter {
            get(b : Box<T>&) : T { return b._val; }
        }
        test() : int {
            b : Box<int>(7);
            g : Getter<int>;
            if (g.get(b) != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — unparameterized friend grants access to all instantiations", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_21;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend Getter;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        struct Getter {
            get(b : Box<T>&) : T { return b._val; }
        }
        test() : int {
            b : Box<int>(99);
            g : Getter<int>;
            if (g.get(b) != 99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Friend declaration — access private (non-template) member", "[gen][friend][private]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_22;
        struct Secret {
            private:
            _x : int;
            friend Revealer;
        public:
            Secret(v : int) { _x = v; }
        }
        struct Revealer {
            peek(s : Secret&) : int { return s._x; }
        }
        test() : int {
            s : Secret(77);
            r : Revealer;
            if (r.peek(s) != 77) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — non-friend cannot access private member", "[gen][friend][template-friend]") {
    REQUIRE(compile_should_fail(R"SRC(
        module gen_friend_23;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend Getter<T>;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        struct Other {
            steal(b : Box<T>&) : T { return b._val; }
        }
        test() : int {
            b : Box<int>(1);
            o : Other<int>;
            return o.steal(b);
        }
    )SRC", nullptr));
}

// ── Gap 1: non-template struct with explicit template friend args ─────────────

TEST_CASE("Template friend — non-template struct, explicit concrete arg grants access", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_24;
        struct Secret {
            private:
            _x : int;
            friend Getter<int>;
        public:
            Secret(v : int) { _x = v; }
        }
        template<typename T>
        struct Getter {
            get(s : Secret&) : int { return s._x; }
        }
        test() : int {
            s : Secret(55);
            g : Getter<int>;
            if (g.get(s) != 55) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — non-template struct, wrong concrete arg rejects access", "[gen][friend][template-friend]") {
    REQUIRE(compile_should_fail(R"SRC(
        module gen_friend_25;
        struct Secret {
            private:
            _x : int;
            friend Getter<int>;
        public:
            Secret(v : int) { _x = v; }
        }
        template<typename T>
        struct Getter {
            get(s : Secret&) : int { return s._x; }
        }
        test() : int {
            s : Secret(1);
            g : Getter<double>;
            return g.get(s);
        }
    )SRC", nullptr));
}

// ── Gap 2: free function template as friend ──────────────────────────────────

TEST_CASE("Template friend — free function template with inherited param grants access", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_26;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend peek<T>;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        peek(b : Box<T>&) : T { return b._val; }
        test() : int {
            b : Box<int>(7);
            if (peek<int>(b) != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — unparameterized free function friend grants access to all instantiations", "[gen][friend][template-friend]") {
    auto jit = gen_jit(R"SRC(
        module gen_friend_27;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend peek;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        peek(b : Box<T>&) : T { return b._val; }
        test() : int {
            bi : Box<int>(3);
            if (peek<int>(bi) != 3) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Template friend — free function template wrong param rejects access", "[gen][friend][template-friend]") {
    // Only peek<int> is declared as friend; peek<long> must be rejected.
    // Use long (not double) to avoid cast syntax issues in constructor args.
    REQUIRE(compile_should_fail(R"SRC(
        module gen_friend_28;
        template<typename T>
        struct Box {
            private:
            _val : T;
            friend peek<int>;
        public:
            Box(v : T) { _val = v; }
        }
        template<typename T>
        peek(b : Box<T>&) : T { return b._val; }
        test() : int {
            v : long = 1;
            b : Box<long>(v);
            p : long = peek<long>(b);
            return 0;
        }
    )SRC", nullptr));
}
