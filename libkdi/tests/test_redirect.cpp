#include <catch2/catch_all.hpp>
#include "kdi.hpp"

#include <sstream>

using namespace kdi;

namespace {

kdi_type receiver_type(const std::string& name) {
    auto object = kdi_type{kdi_const_type{
        std::make_shared<kdi_type>(kdi_type::make_aggregate(name))}};
    return kdi_type{kdi_ref_type{std::make_shared<kdi_type>(object)}};
}

kdi_file redirect_file() {
    kdi_file file;
    file.header.module_name = "redirect";
    file.unit.name = "redirect";
    file.unit.root_ns.fq_name = "redirect";
    for (const auto* name : {"Base", "Derived"}) {
        kdi_aggregate aggregate;
        aggregate.name = name;
        aggregate.fq_name = std::string("redirect::") + name;
        aggregate.llvm_def = "{ i32 }";
        file.unit.root_ns.aggregates.push_back(aggregate);
    }

    kdi_method method;
    method.name = "read";
    method.fq_name = "redirect::Derived::read";
    method.is_const_member = true;
    method.return_type = kdi_type::make_int(32);
    method.mangled_name = "_source_member";
    method.llvm_def = "declare i32 @_source_member(ptr)";
    method.redirect_target = kdi_redirect_target{
        "_private_body", true, receiver_type("redirect::Base")};
    file.unit.root_ns.aggregates.back().methods.push_back(method);
    auto free_method = method;
    free_method.name = "readViaFree";
    free_method.fq_name = "redirect::Derived::readViaFree";
    free_method.mangled_name = "_source_member_free";
    free_method.llvm_def = "declare i32 @_source_member_free(ptr)";
    free_method.redirect_target->is_member = false;
    file.unit.root_ns.aggregates.back().methods.push_back(free_method);

    kdi_function function;
    function.name = "readFree";
    function.fq_name = "redirect::readFree";
    function.return_type = kdi_type::make_int(32);
    function.params.push_back({"object", receiver_type("redirect::Derived"), false});
    function.mangled_name = "_source_free";
    function.llvm_def = "declare i32 @_source_free(ptr)";
    function.redirect_target = method.redirect_target;
    file.unit.root_ns.functions.push_back(function);

    auto plain = function;
    plain.name = "plain";
    plain.fq_name = "redirect::plain";
    plain.params.clear();
    plain.mangled_name = "_source_plain";
    plain.llvm_def = "declare i32 @_source_plain()";
    plain.redirect_target = kdi_redirect_target{"_plain_body", false, std::nullopt};
    file.unit.root_ns.functions.push_back(plain);

    function.name = "freeChain";
    function.fq_name = "redirect::freeChain";
    function.mangled_name = "_source_chain";
    function.llvm_def = "declare i32 @_source_chain(ptr)";
    function.redirect_target->is_member = false;
    file.unit.root_ns.functions.push_back(function);
    return file;
}

void check_redirect_file(const kdi_file& file) {
    const auto& method = file.unit.root_ns.aggregates.back().methods.front();
    REQUIRE(method.redirect_target);
    CHECK(method.mangled_name == "_source_member");
    CHECK(method.llvm_def.find("@_source_member(") != std::string::npos);
    CHECK(method.redirect_target->symbol == "_private_body");
    CHECK(method.redirect_target->is_member);
    REQUIRE(method.redirect_target->receiver_type);
    CHECK(kdi_type_to_string(*method.redirect_target->receiver_type).find("redirect::Base")
          != std::string::npos);
    const auto& free_method = file.unit.root_ns.aggregates.back().methods.back();
    REQUIRE(free_method.redirect_target);
    CHECK_FALSE(free_method.redirect_target->is_member);
    REQUIRE(free_method.redirect_target->receiver_type);
    const auto& free = file.unit.root_ns.functions.front();
    REQUIRE(free.redirect_target);
    CHECK(free.mangled_name == "_source_free");
    CHECK(free.redirect_target->is_member);
    REQUIRE(free.redirect_target->receiver_type);
    const auto& chain = file.unit.root_ns.functions.back();
    REQUIRE(chain.redirect_target);
    CHECK_FALSE(chain.redirect_target->is_member);
    REQUIRE(chain.redirect_target->receiver_type);
    const auto& plain = file.unit.root_ns.functions[1];
    REQUIRE(plain.redirect_target);
    CHECK_FALSE(plain.redirect_target->is_member);
    CHECK_FALSE(plain.redirect_target->receiver_type);
    CHECK(kdi_validate(file).is_valid());
}

} // namespace

TEST_CASE("redirect provider contract survives JSON and CBOR without changing source symbols",
          "[redirect-contract][json][cbor][dto]") {
    auto file = redirect_file();
    check_redirect_file(file);
    std::stringstream json;
    kdi_write_json(file, json);
    check_redirect_file(kdi_read_json(json));
    std::stringstream cbor;
    kdi_write_cbor(file, cbor);
    check_redirect_file(kdi_read_cbor(cbor));

    for (auto& method : file.unit.root_ns.aggregates.back().methods) method.redirect_target.reset();
    for (auto& function : file.unit.root_ns.functions) function.redirect_target.reset();
    std::stringstream legacy;
    kdi_write_cbor(file, legacy);
    auto decoded = kdi_read_cbor(legacy);
    CHECK_FALSE(decoded.unit.root_ns.functions.front().redirect_target);
    CHECK_FALSE(decoded.unit.root_ns.aggregates.back().methods.front().redirect_target);
    CHECK(kdi_validate(decoded).is_valid());
    std::stringstream legacy_json;
    kdi_write_json(file, legacy_json);
    auto json_decoded = kdi_read_json(legacy_json);
    CHECK_FALSE(json_decoded.unit.root_ns.functions.front().redirect_target);
    CHECK(kdi_validate(json_decoded).is_valid());
}

TEST_CASE("redirect provider contract validation rejects incomplete receiver metadata",
          "[redirect-contract][validate]") {
    auto file = redirect_file();
    auto& method = file.unit.root_ns.aggregates.back().methods.front();
    SECTION("missing provider symbol") { method.redirect_target->symbol.clear(); }
    SECTION("missing provider receiver") { method.redirect_target->receiver_type.reset(); }
    SECTION("nonaggregate receiver") {
        method.redirect_target->receiver_type = kdi_type::make_int(32);
    }
    SECTION("missing explicit source receiver") {
        file.unit.root_ns.functions.front().params.clear();
    }
    CHECK_FALSE(kdi_validate(file).is_valid());
}

TEST_CASE("redirect provider kind is mandatory in both wire formats",
          "[redirect-contract][json][cbor]") {
    auto file = redirect_file();
    std::stringstream json;
    kdi_write_json(file, json);
    auto json_bytes = json.str();
    auto key = json_bytes.find("\"is_member\"");
    REQUIRE(key != std::string::npos);
    json_bytes.replace(key, 11, "\"no_member\"");
    std::istringstream invalid_json(json_bytes);
    CHECK_THROWS(kdi_read_json(invalid_json));

    std::stringstream cbor;
    kdi_write_cbor(file, cbor);
    auto cbor_bytes = cbor.str();
    key = cbor_bytes.find("is_member");
    REQUIRE(key != std::string::npos);
    cbor_bytes.replace(key, 9, "no_member");
    std::istringstream invalid_cbor(cbor_bytes);
    CHECK_THROWS(kdi_read_cbor(invalid_cbor));
}

TEST_CASE("redirect provider kind cannot silently decode a nonboolean value",
          "[redirect-contract][json][cbor]") {
    auto file = redirect_file();
    std::stringstream json;
    kdi_write_json(file, json);
    auto bytes = json.str();
    auto key = bytes.find("\"is_member\"");
    REQUIRE(key != std::string::npos);
    auto value = bytes.find_first_not_of(" \t\r\n", bytes.find(':', key) + 1);
    REQUIRE(value != std::string::npos);
    bytes.replace(value, bytes.compare(value, 4, "true") == 0 ? 4 : 5, "0");
    std::istringstream invalid_json(bytes);
    CHECK_THROWS(kdi_read_json(invalid_json));

    std::stringstream cbor;
    kdi_write_cbor(file, cbor);
    bytes = cbor.str();
    key = bytes.find("is_member");
    REQUIRE(key != std::string::npos);
    REQUIRE(key + 9 < bytes.size());
    bytes[key + 9] = '\0';
    std::istringstream invalid_cbor(bytes);
    CHECK_THROWS(kdi_read_cbor(invalid_cbor));
}

TEST_CASE("redirect dump and provider-symbol query retain the source declaration",
          "[redirect-contract][query][dump]") {
    auto file = redirect_file();
    std::ostringstream dump;
    kdi_dump(file, dump);
    CHECK(dump.str().find("redirect body: _private_body [member]") != std::string::npos);
    CHECK(dump.str().find("receiver:") != std::string::npos);
    auto rows = kdi_list_symbols(file, "_private_body");
    REQUIRE(rows.size() == 4);
    for (const auto& row : rows) {
        CHECK(row.mangled_name != "_private_body");
        CHECK(row.signature.find("_private_body") != std::string::npos);
        CHECK(row.signature.find("redirect::Base") != std::string::npos);
    }
}
