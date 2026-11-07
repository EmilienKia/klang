/*
 * K Language private streaming CCM engine tests
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

#include <openssl/evp.h>

extern "C" {
#include "../src/ccm_engine.h"
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;
using Cipher = std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)>;
using EvpContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using CcmContext = std::unique_ptr<k_ccm_ctx, decltype(&k_ccm_free)>;

struct Sealed {
    Bytes ciphertext;
    Bytes tag;
};

Bytes pattern(std::size_t length, unsigned int seed) {
    Bytes result(length);
    for (std::size_t i = 0; i < length; ++i) {
        result[i] = static_cast<unsigned char>((i * 37 + seed) & 0xff);
    }
    return result;
}

Cipher fetch(const char* name) {
    return Cipher(EVP_CIPHER_fetch(nullptr, name, nullptr), EVP_CIPHER_free);
}

CcmContext make_context(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& nonce,
                        unsigned int tag_len, std::size_t message_len,
                        std::size_t aad_len, bool encrypt) {
    k_ccm_ctx* raw = nullptr;
    const int result = k_ccm_new(cipher, key.data(), static_cast<unsigned int>(key.size()),
                                 nonce.data(), static_cast<unsigned int>(nonce.size()),
                                 tag_len, message_len, aad_len, encrypt ? 1 : 0, &raw);
    REQUIRE(result == K_CCM_OK);
    CcmContext ctx(raw, k_ccm_free);
    REQUIRE(ctx);
    return ctx;
}

Sealed openssl_seal(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& nonce,
                    unsigned int tag_len, const Bytes& aad, const Bytes& plaintext) {
    EvpContext ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    REQUIRE(ctx);
    REQUIRE(EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) == 1);
    REQUIRE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_IVLEN,
                                static_cast<int>(nonce.size()), nullptr) == 1);
    REQUIRE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_TAG, tag_len, nullptr) == 1);
    REQUIRE(EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) == 1);
    int written = 0;
    REQUIRE(EVP_EncryptUpdate(ctx.get(), nullptr, &written, nullptr,
                              static_cast<int>(plaintext.size())) == 1);
    if (!aad.empty()) {
        REQUIRE(EVP_EncryptUpdate(ctx.get(), nullptr, &written, aad.data(),
                                  static_cast<int>(aad.size())) == 1);
    }
    Sealed result{Bytes(plaintext.size()), Bytes(tag_len)};
    unsigned char dummy = 0;
    const auto* input = plaintext.empty() ? &dummy : plaintext.data();
    auto* output = result.ciphertext.empty() ? &dummy : result.ciphertext.data();
    REQUIRE(EVP_EncryptUpdate(ctx.get(), output, &written, input,
                              static_cast<int>(plaintext.size())) == 1);
    REQUIRE(written == static_cast<int>(plaintext.size()));
    REQUIRE(EVP_EncryptFinal_ex(ctx.get(), &dummy, &written) == 1);
    REQUIRE(written == 0);
    REQUIRE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_GET_TAG, tag_len,
                                result.tag.data()) == 1);
    return result;
}

bool openssl_open(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& nonce,
                  const Bytes& aad, const Sealed& sealed, Bytes& plaintext) {
    EvpContext ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    REQUIRE(ctx);
    REQUIRE(EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) == 1);
    REQUIRE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_IVLEN,
                                static_cast<int>(nonce.size()), nullptr) == 1);
    Bytes tag = sealed.tag;
    REQUIRE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_TAG,
                                static_cast<int>(tag.size()), tag.data()) == 1);
    REQUIRE(EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) == 1);
    int written = 0;
    REQUIRE(EVP_DecryptUpdate(ctx.get(), nullptr, &written, nullptr,
                              static_cast<int>(sealed.ciphertext.size())) == 1);
    if (!aad.empty()) {
        REQUIRE(EVP_DecryptUpdate(ctx.get(), nullptr, &written, aad.data(),
                                  static_cast<int>(aad.size())) == 1);
    }
    plaintext.resize(sealed.ciphertext.size());
    unsigned char dummy = 0;
    const auto* input = sealed.ciphertext.empty() ? &dummy : sealed.ciphertext.data();
    auto* output = plaintext.empty() ? &dummy : plaintext.data();
    const int ok = EVP_DecryptUpdate(ctx.get(), output, &written, input,
                                     static_cast<int>(sealed.ciphertext.size()));
    if (ok == 1) {
        REQUIRE(written == static_cast<int>(sealed.ciphertext.size()));
    }
    return ok == 1;
}

void update_aad(k_ccm_ctx* ctx, const Bytes& aad, bool decrypt) {
    constexpr std::size_t encrypt_chunks[] = {1, 2, 13, 16, 5, 251};
    constexpr std::size_t decrypt_chunks[] = {17, 3, 1, 31, 7, 127};
    const auto* chunks = decrypt ? decrypt_chunks : encrypt_chunks;
    std::size_t offset = 0;
    std::size_t index = 0;
    while (offset < aad.size()) {
        const auto count = std::min(chunks[index++ % 6], aad.size() - offset);
        REQUIRE(k_ccm_aad_update(ctx, aad.data() + offset, count) == K_CCM_OK);
        offset += count;
    }
}

void update_message(k_ccm_ctx* ctx, const Bytes& input, Bytes& output,
                    bool in_place, bool decrypt) {
    constexpr std::size_t encrypt_chunks[] = {1, 15, 2, 17, 3, 7, 32};
    constexpr std::size_t decrypt_chunks[] = {16, 1, 19, 5, 2, 11, 23};
    const auto* chunks = decrypt ? decrypt_chunks : encrypt_chunks;
    output = in_place ? input : Bytes(input.size());
    std::size_t offset = 0;
    std::size_t index = 0;
    while (offset < input.size()) {
        const auto count = std::min(chunks[index++ % 7], input.size() - offset);
        const auto* source = in_place ? output.data() + offset : input.data() + offset;
        REQUIRE(k_ccm_update(ctx, source, output.data() + offset, count) == K_CCM_OK);
        offset += count;
    }
}

Sealed engine_seal(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& nonce,
                   unsigned int tag_len, const Bytes& aad, const Bytes& plaintext,
                   bool in_place = false) {
    auto ctx = make_context(cipher, key, nonce, tag_len, plaintext.size(), aad.size(), true);
    update_aad(ctx.get(), aad, false);
    Sealed result{Bytes(), Bytes(tag_len)};
    update_message(ctx.get(), plaintext, result.ciphertext, in_place, false);
    REQUIRE(k_ccm_final(ctx.get(), result.tag.data()) == K_CCM_OK);
    return result;
}

int engine_open(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& nonce,
                const Bytes& aad, const Sealed& sealed, Bytes& plaintext,
                bool in_place = false) {
    auto ctx = make_context(cipher, key, nonce, static_cast<unsigned int>(sealed.tag.size()),
                            sealed.ciphertext.size(), aad.size(), false);
    update_aad(ctx.get(), aad, true);
    update_message(ctx.get(), sealed.ciphertext, plaintext, in_place, true);
    Bytes tag = sealed.tag;
    return k_ccm_final(ctx.get(), tag.data());
}

void check_interop(const EVP_CIPHER* ecb, const EVP_CIPHER* ccm, const Bytes& key,
                   const Bytes& nonce, unsigned int tag_len, const Bytes& aad,
                   const Bytes& plaintext, bool in_place = false) {
    const auto expected = openssl_seal(ccm, key, nonce, tag_len, aad, plaintext);
    const auto actual = engine_seal(ecb, key, nonce, tag_len, aad, plaintext, in_place);
    CHECK(actual.ciphertext == expected.ciphertext);
    CHECK(actual.tag == expected.tag);

    Bytes recovered;
    REQUIRE(engine_open(ecb, key, nonce, aad, expected, recovered, in_place) == K_CCM_OK);
    CHECK(recovered == plaintext);
    REQUIRE(openssl_open(ccm, key, nonce, aad, actual, recovered));
    CHECK(recovered == plaintext);
}

} // namespace

TEST_CASE("Streaming CCM matches RFC 3610 packet vector 1", "[crypto][ccm][native]") {
    auto ecb = fetch("AES-128-ECB");
    auto ccm = fetch("AES-128-CCM");
    REQUIRE(ecb);
    REQUIRE(ccm);
    const Bytes key{0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
                    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf};
    const Bytes nonce{0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
                      0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5};
    const Bytes aad{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    const Bytes plaintext{0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                          0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                          0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e};
    const Bytes ciphertext{0x58, 0x8c, 0x97, 0x9a, 0x61, 0xc6, 0x63, 0xd2,
                           0xf0, 0x66, 0xd0, 0xc2, 0xc0, 0xf9, 0x89, 0x80,
                           0x6d, 0x5f, 0x6b, 0x61, 0xda, 0xc3, 0x84};
    const Bytes tag{0x17, 0xe8, 0xd1, 0x2c, 0xfd, 0xf9, 0x26, 0xe0};
    const auto sealed = engine_seal(ecb.get(), key, nonce, 8, aad, plaintext);
    CHECK(sealed.ciphertext == ciphertext);
    CHECK(sealed.tag == tag);
    Bytes recovered;
    REQUIRE(engine_open(ecb.get(), key, nonce, aad, {ciphertext, tag}, recovered) == K_CCM_OK);
    CHECK(recovered == plaintext);
    check_interop(ecb.get(), ccm.get(), key, nonce, 8, aad, plaintext);
}

TEST_CASE("Streaming CCM interoperates with provider AES-CCM", "[crypto][ccm][native]") {
    auto ecb = fetch("AES-128-ECB");
    auto ccm = fetch("AES-128-CCM");
    REQUIRE(ecb);
    REQUIRE(ccm);
    const auto key = pattern(16, 3);

    SECTION("empty message and AAD") {
        check_interop(ecb.get(), ccm.get(), key, pattern(12, 4), 16, {}, {});
    }
    SECTION("empty message with nonempty fragmented AAD") {
        check_interop(ecb.get(), ccm.get(), key, pattern(12, 4), 16,
                      pattern(29, 6), {});
    }
    SECTION("partial block with shortest nonce and tag") {
        check_interop(ecb.get(), ccm.get(), key, pattern(7, 5), 4, pattern(19, 6),
                      pattern(13, 7));
    }
    SECTION("multiple blocks with exact in-place input and output") {
        check_interop(ecb.get(), ccm.get(), key, pattern(11, 8), 12, pattern(33, 9),
                      pattern(83, 10), true);
    }
    SECTION("AAD length encoding changes at 0xff00") {
        for (const std::size_t length : {std::size_t{0xfeff}, std::size_t{0xff00},
                                         std::size_t{0xff01}}) {
            INFO("AAD length: " << length);
            check_interop(ecb.get(), ccm.get(), key, pattern(13, 11), 8,
                          pattern(length, 12), pattern(31, 13));
        }
    }
}

TEST_CASE("Streaming CCM interoperates with provider ARIA-CCM when available",
          "[crypto][ccm][native]") {
    auto ecb = fetch("ARIA-128-ECB");
    auto ccm = fetch("ARIA-128-CCM");
    if (!ecb || !ccm) {
        SKIP("ARIA ECB or CCM is not available from the active providers");
    }
    check_interop(ecb.get(), ccm.get(), pattern(16, 14), pattern(12, 15), 10,
                  pattern(27, 16), pattern(41, 17), true);
}

TEST_CASE("Streaming CCM rejects modified authentication data", "[crypto][ccm][native]") {
    auto ecb = fetch("AES-128-ECB");
    auto ccm = fetch("AES-128-CCM");
    REQUIRE(ecb);
    REQUIRE(ccm);
    const auto key = pattern(16, 18);
    const auto nonce = pattern(12, 19);
    const auto aad = pattern(23, 20);
    auto sealed = openssl_seal(ccm.get(), key, nonce, 12, aad, pattern(39, 21));

    SECTION("modified tag") {
        sealed.tag[5] ^= 0x80;
    }
    SECTION("modified ciphertext") {
        sealed.ciphertext[17] ^= 0x01;
    }
    SECTION("modified AAD") {
        auto modified_aad = aad;
        modified_aad[4] ^= 0x01;
        Bytes recovered;
        CHECK(engine_open(ecb.get(), key, nonce, modified_aad, sealed, recovered) == K_CCM_AUTH);
        CHECK_FALSE(openssl_open(ccm.get(), key, nonce, modified_aad, sealed, recovered));
        return;
    }
    SECTION("failed authentication is terminal") {
        sealed.tag[5] ^= 0x80;
        auto ctx = make_context(ecb.get(), key, nonce,
                                static_cast<unsigned int>(sealed.tag.size()),
                                sealed.ciphertext.size(), aad.size(), false);
        update_aad(ctx.get(), aad, true);
        Bytes recovered;
        update_message(ctx.get(), sealed.ciphertext, recovered, false, true);
        REQUIRE(k_ccm_final(ctx.get(), sealed.tag.data()) == K_CCM_AUTH);
        CHECK(k_ccm_final(ctx.get(), sealed.tag.data()) == K_CCM_STATE);
        CHECK(k_ccm_update(ctx.get(), sealed.ciphertext.data(), recovered.data(), 1)
              == K_CCM_STATE);
        CHECK(k_ccm_aad_update(ctx.get(), aad.data(), 1) == K_CCM_STATE);
        return;
    }
    Bytes recovered;
    CHECK(engine_open(ecb.get(), key, nonce, aad, sealed, recovered) == K_CCM_AUTH);
    CHECK_FALSE(openssl_open(ccm.get(), key, nonce, aad, sealed, recovered));
}

TEST_CASE("Streaming CCM rejects invalid parameters", "[crypto][ccm][native]") {
    auto ecb = fetch("AES-128-ECB");
    auto cbc = fetch("AES-128-CBC");
    REQUIRE(ecb);
    REQUIRE(cbc);
    const auto key = pattern(16, 22);
    const auto nonce = pattern(14, 23);
    auto rejected = [&](const EVP_CIPHER* cipher, const unsigned char* key_ptr,
                        unsigned int key_len, const unsigned char* nonce_ptr,
                        unsigned int nonce_len, unsigned int tag_len, uint64_t message_len) {
        k_ccm_ctx* raw = nullptr;
        const int result = k_ccm_new(cipher, key_ptr, key_len, nonce_ptr, nonce_len,
                                     tag_len, message_len, 0, 1, &raw);
        CcmContext ctx(result == K_CCM_OK ? raw : nullptr, k_ccm_free);
        CHECK(result == K_CCM_INVALID);
    };
    rejected(nullptr, key.data(), 16, nonce.data(), 12, 8, 0);
    rejected(ecb.get(), nullptr, 16, nonce.data(), 12, 8, 0);
    rejected(ecb.get(), key.data(), 15, nonce.data(), 12, 8, 0);
    rejected(ecb.get(), key.data(), 16, nullptr, 12, 8, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 6, 8, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 14, 8, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 12, 3, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 12, 5, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 12, 18, 0);
    rejected(ecb.get(), key.data(), 16, nonce.data(), 13, 8, 65536);
    auto maximum_q2 = make_context(ecb.get(), key, Bytes(nonce.begin(), nonce.begin() + 13),
                                   8, 65535, 0, true);
    REQUIRE(maximum_q2);
    rejected(cbc.get(), key.data(), 16, nonce.data(), 12, 8, 0);
    CHECK(k_ccm_new(ecb.get(), key.data(), 16, nonce.data(), 12, 8, 0, 0, 1, nullptr)
          == K_CCM_INVALID);
}

TEST_CASE("Streaming CCM enforces declared lengths and operation order",
          "[crypto][ccm][native]") {
    auto ecb = fetch("AES-128-ECB");
    REQUIRE(ecb);
    const auto key = pattern(16, 24);
    const auto nonce = pattern(12, 25);
    const auto data = pattern(4, 26);
    Bytes output(4);
    Bytes tag(8);

    SECTION("AAD cannot exceed its declared length") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 0, 2, true);
        CHECK(k_ccm_aad_update(ctx.get(), data.data(), 3) != K_CCM_OK);
    }
    SECTION("message cannot exceed its declared length") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 2, 0, true);
        CHECK(k_ccm_update(ctx.get(), data.data(), output.data(), 3) != K_CCM_OK);
    }
    SECTION("partially overlapping input and output are rejected") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 3, 0, true);
        auto overlapping = data;
        CHECK(k_ccm_update(ctx.get(), overlapping.data(), overlapping.data() + 1, 3)
              != K_CCM_OK);
    }
    SECTION("final rejects incomplete AAD") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 0, 2, true);
        REQUIRE(k_ccm_aad_update(ctx.get(), data.data(), 1) == K_CCM_OK);
        CHECK(k_ccm_final(ctx.get(), tag.data()) != K_CCM_OK);
    }
    SECTION("final rejects an incomplete message") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 2, 0, true);
        REQUIRE(k_ccm_update(ctx.get(), data.data(), output.data(), 1) == K_CCM_OK);
        CHECK(k_ccm_final(ctx.get(), tag.data()) != K_CCM_OK);
    }
    SECTION("message cannot start before all AAD is supplied") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 2, 2, true);
        REQUIRE(k_ccm_aad_update(ctx.get(), data.data(), 1) == K_CCM_OK);
        CHECK(k_ccm_update(ctx.get(), data.data(), output.data(), 1) != K_CCM_OK);
    }
    SECTION("AAD cannot follow message data") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 2, 1, true);
        REQUIRE(k_ccm_aad_update(ctx.get(), data.data(), 1) == K_CCM_OK);
        REQUIRE(k_ccm_update(ctx.get(), data.data(), output.data(), 1) == K_CCM_OK);
        CHECK(k_ccm_aad_update(ctx.get(), data.data(), 1) != K_CCM_OK);
    }
    SECTION("a finalized context cannot be reused") {
        auto ctx = make_context(ecb.get(), key, nonce, 8, 0, 0, true);
        REQUIRE(k_ccm_final(ctx.get(), tag.data()) == K_CCM_OK);
        CHECK(k_ccm_final(ctx.get(), tag.data()) == K_CCM_STATE);
        CHECK(k_ccm_update(ctx.get(), data.data(), output.data(), 1) == K_CCM_STATE);
        CHECK(k_ccm_aad_update(ctx.get(), data.data(), 1) == K_CCM_STATE);
    }
}
