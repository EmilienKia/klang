/*
 * K Language standard library — URI tests
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

#include "../../klang/tests/helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

TEST_CASE("Uri: parsing full hierarchical URI", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_full__;
        test() : int {
            u : k::io::Uri("https://alice:secret@example.com:8080/api/v1/users?role=admin&active=true#section1");
            res : int = 0;
            if (u.isAbsolute()) { res += 1; }
            if (u.isHierarchical()) { res += 2; }
            if (!u.isOpaque()) { res += 4; }
            if (u.scheme() == k::String("https")) { res += 8; }
            if (u.rawUserInfo() == k::String("alice:secret")) { res += 16; }
            if (u.host() == k::String("example.com")) { res += 32; }
            if (u.port() == 8080) { res += 64; }
            if (u.rawPath() == k::String("/api/v1/users")) { res += 128; }
            if (u.rawQuery() == k::String("role=admin&active=true")) { res += 256; }
            if (u.rawFragment() == k::String("section1")) { res += 512; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512));
}

TEST_CASE("Uri: parsing opaque URI", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_opaque__;
        test() : int {
            u : k::io::Uri("mailto:user@example.com?subject=Hello#frag");
            res : int = 0;
            if (u.isAbsolute()) { res += 1; }
            if (u.isOpaque()) { res += 2; }
            if (u.scheme() == k::String("mailto")) { res += 4; }
            if (u.rawSchemeSpecificPart() == k::String("user@example.com?subject=Hello")) { res += 8; }
            if (u.rawFragment() == k::String("frag")) { res += 16; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16));
}

TEST_CASE("Uri: percent decoding accessors", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_percent__;
        test() : int {
            u : k::io::Uri("http://example.com/hello%20world/caf%C3%A9?msg=bonjour%20monde#title%201");
            res : int = 0;
            if (u.rawPath() == k::String("/hello%20world/caf%C3%A9")) { res += 1; }
            if (u.path() == k::String("/hello world/café")) { res += 2; }
            if (u.rawQuery() == k::String("msg=bonjour%20monde")) { res += 4; }
            if (u.query() == k::String("msg=bonjour monde")) { res += 8; }
            if (u.rawFragment() == k::String("title%201")) { res += 16; }
            if (u.fragment() == k::String("title 1")) { res += 32; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32));
}

TEST_CASE("Uri: QueryParams inspection and modification", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_query_params__;
        test() : int {
            qp : k::io::QueryParams("tag=k&tag=compiler&filter=active+true&limit=10");
            res : int = 0;
            if (qp.size() == 4u) { res += 1; }
            if (qp.has("tag")) { res += 2; }
            if (qp.get("filter") == k::String("active true")) { res += 4; }
            if (qp.get("limit") == k::String("10")) { res += 8; }

            tags : k::Vector<k::String>! = qp.getAll("tag");
            if (tags->size() == 2u && tags->get(0u) == k::String("k") && tags->get(1u) == k::String("compiler")) { res += 16; }
            delete tags;

            qp2 : k::io::QueryParams! = qp.withParam("sort", "desc");
            if (qp2->has("sort") && qp2->get("sort") == k::String("desc")) { res += 32; }

            qp3 : k::io::QueryParams! = qp2->withoutParam("tag");
            if (!qp3->has("tag") && qp3->has("filter")) { res += 64; }

            delete qp2;
            delete qp3;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64));
}

TEST_CASE("Uri: with* immutable mutators", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_with__;
        test() : int {
            base : k::io::Uri("http://example.com/v1?page=1#top");
            res : int = 0;

            u1 : k::io::Uri! = base.withScheme("https");
            if (u1.scheme() == k::String("https") && base.scheme() == k::String("http")) { res += 1; }

            u2 : k::io::Uri! = u1.withHost("api.example.com");
            if (u2.host() == k::String("api.example.com")) { res += 2; }

            u3 : k::io::Uri! = u2.withPort(9000);
            if (u3.port() == 9000) { res += 4; }

            u4 : k::io::Uri! = u3.withPath("/v2/users");
            if (u4.rawPath() == k::String("/v2/users")) { res += 8; }

            u5 : k::io::Uri! = u4.withQueryParam("sort", "name");
            if (u5.hasQuery() && u5.queryParam("page") == k::String("1") && u5.queryParam("sort") == k::String("name")) { res += 16; }

            u6 : k::io::Uri! = u5.withoutFragment();
            if (!u6.hasFragment()) { res += 32; }

            delete u1;
            delete u2;
            delete u3;
            delete u4;
            delete u5;
            delete u6;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32));
}

TEST_CASE("Uri: normalize and resolve", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_resolve__;
        test() : int {
            u : k::io::Uri("http://example.com/a/b/c/./../../g");
            norm : k::io::Uri! = u.normalize();
            res : int = 0;
            if (norm.rawPath() == k::String("/a/g")) { res += 1; }

            base : k::io::Uri("http://example.com/dir/index.html?v=1");
            r1 : k::io::Uri! = base.resolve("page.html#section");
            if (r1.scheme() == k::String("http") && r1.host() == k::String("example.com") &&
                r1.rawPath() == k::String("/dir/page.html") && r1.rawFragment() == k::String("section")) {
                res += 2;
            }

            r2 : k::io::Uri! = base.resolve("/root/api?debug=true");
            if (r2.rawPath() == k::String("/root/api") && r2.rawQuery() == k::String("debug=true")) {
                res += 4;
            }

            delete norm;
            delete r1;
            delete r2;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4));
}

TEST_CASE("Uri: IPv6 host and fromParts factory", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_ipv6_parts__;
        test() : int {
            u : k::io::Uri("http://[2001:db8::1]:8080/index.html");
            res : int = 0;
            if (u.host() == k::String("[2001:db8::1]")) { res += 1; }
            if (u.port() == 8080) { res += 2; }
            if (u.rawPath() == k::String("/index.html")) { res += 4; }

            u2 : k::io::Uri! = k::io::Uri::fromParts("https", "user:pw", "example.org", 443, "/path", "a=1", "f");
            if (u2->scheme() == k::String("https") &&
                u2->userInfo() == k::String("user:pw") &&
                u2->host() == k::String("example.org") &&
                u2->port() == 443 &&
                u2->rawPath() == k::String("/path") &&
                u2->rawQuery() == k::String("a=1") &&
                u2->rawFragment() == k::String("f")) {
                res += 8;
            }

            delete u2;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8));
}

TEST_CASE("Uri: equality and relativize", "[libk][io][uri]") {
    auto jit = jit_k(R"SRC(
        module __uri_eq_rel__;
        test() : int {
            u1 : k::io::Uri("https://example.com:8080/a/b/c?q=1#frag");
            u2 : k::io::Uri("https://example.com:8080/a/b/c?q=1#frag");
            u3 : k::io::Uri("https://example.com:8080/a/b/d?q=1#frag");
            res : int = 0;
            if (u1 == u2) { res += 1; }
            if (u1 != u3) { res += 2; }

            base : k::io::Uri("http://example.com/docs/");
            target : k::io::Uri("http://example.com/docs/guide/index.html?ver=2#sec1");
            rel : k::io::Uri! = base.relativize(target);
            if (rel->rawPath() == k::String("guide/index.html") &&
                rel->rawQuery() == k::String("ver=2") &&
                rel->rawFragment() == k::String("sec1")) {
                res += 4;
            }

            delete rel;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4));
}
