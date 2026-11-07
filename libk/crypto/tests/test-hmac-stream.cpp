/*
 * K Language HMAC stream integration tests
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

#include "crypto_test_helpers.hpp"

#include <string>

namespace {

constexpr auto prelude = R"K(
    module __crypto_hmac_stream_test__;
    import k::io::crypto;

    nibble(c: unsigned byte) : unsigned int {
        if (c <= (unsigned byte) '9') return (unsigned int) c - (unsigned int) '0';
        return (unsigned int) c - (unsigned int) 'a' + 10u;
    }

    fromHex(text: const String&) : unsigned byte[]! {
        encoded : unsigned byte[]! = text.toUtf8();
        length : unsigned int = (encoded.size - 1u) / 2u;
        bytes : unsigned byte[]! = new unsigned byte[length];
        i : unsigned int = 0u;
        while (i < length) {
            bytes[i] = (unsigned byte) ((nibble(encoded[2u * i]) << 4u) |
                                       nibble(encoded[2u * i + 1u]));
            ++i;
        }
        delete encoded;
        return bytes;
    }

    newMac() : k::crypto::Hmac! throws(k::crypto::CryptoException) {
        raw : unsigned byte[]! = new unsigned byte[20u];
        i : unsigned int = 0u;
        while (i < raw.size) {
            raw[i] = (unsigned byte) 11;
            ++i;
        }
        key : k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
        return k::crypto::Hmac::SHA256(key);
    }

    message() : byte[]! {
        value : String("Hi There");
        text : unsigned byte[]! = value.toUtf8();
        result : byte[]! = new byte[8u];
        i : unsigned int = 0u;
        while (i < 8u) {
            result[i] = (byte) text[i];
            ++i;
        }
        delete text;
        return result;
    }

    class ShortInput : public k::io::FilterInputStream<byte> {
    public:
        ShortInput(input: k::io::InputStream<byte>&) : FilterInputStream(&input) {}
        override read(data: byte[], off: unsigned int, len: unsigned int)
                : Expected<unsigned int, k::io::StreamOutOfData> {
            if (len > 2u) len = 2u;
            return _in->read(data, off, len);
        }
    }
)K";

void check_jit(const std::string& source) {
    auto jit = gen_jit_with_crypto(source);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

} // namespace

TEST_CASE("HMAC output caches the full tag and enforces verification lengths",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        test() : int {
            try {
                data : byte[]! = message();
                expected : unsigned byte[]! = fromHex(String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
                prefix : unsigned byte[]! = fromHex(String("b0344c61d8db38535ca8afceaf0bf12b"));
                sink : k::io::ArrayOutputStream<byte>;
                mac : k::crypto::Hmac! = newMac();
                output : k::io::crypto::HmacOutputStream! =
                    new k::io::crypto::HmacOutputStream(sink, mac);
                if (mac != null || output.isComplete() || output.getTag() != null) return 1;
                rejected : bool = false;
                try {
                    output.verify(expected);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 2;
                output.write(data[0u]);
                output.write(data, 1u, 3u);
                rest : byte[]! = new byte[4u];
                i : unsigned int = 0u;
                while (i < 4u) {
                    rest[i] = data[4u + i];
                    ++i;
                }
                output.write(rest);
                output.flush();
                if (output.isComplete() || output.getTag() != null || sink.size() != 8) return 3;
                actual : byte[]! = sink.toArray();
                i = 0u;
                while (i < 8u) {
                    if (actual[i] != data[i]) return 4;
                    ++i;
                }
                output.close();
                output.close();
                if (!output.isComplete() || output.getTag() == null || !output.verify(expected))
                    return 5;
                if (output.verify(prefix) || !output.verifyTruncated(prefix, 16u) ||
                    output.verifyTruncated(prefix, 32u)) return 6;
                prefix[15u] = (unsigned byte) (prefix[15u] ^ 1u);
                if (output.verifyTruncated(prefix, 16u)) return 7;
                expected[31u] = (unsigned byte) (expected[31u] ^ 1u);
                if (output.verify(expected)) return 8;
                rejected = false;
                try {
                    output.verifyTruncated(prefix, 0u);
                } catch (e: k::crypto::CryptoInvalidInputException&) {
                    rejected = true;
                }
                if (!rejected) return 9;
                rejected = false;
                try {
                    output.verifyTruncated(prefix, 33u);
                } catch (e: k::crypto::CryptoInvalidInputException&) {
                    rejected = true;
                }
                if (!rejected) return 10;
                rejected = false;
                try {
                    output.write((byte) 1);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                return rejected ? 0 : 11;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC input authenticates single, bulk and skipped bytes at EOF",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        test() : int {
            try {
                data : byte[]! = message();
                expected : unsigned byte[]! = fromHex(String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
                prefix : unsigned byte[]! = fromHex(String("b0344c61d8db38535ca8afceaf0bf12b"));
                source : k::io::ArrayInputStream<byte>(data, (int) data.size);
                chunks : ShortInput(source);
                input : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(chunks, newMac());
                buffer : byte[]! = new byte[4u];
                buffer[0u] = (byte) 'x';
                buffer[3u] = (byte) 'y';
                zero : Expected<unsigned int, k::io::StreamOutOfData> = input.read(buffer, 1u, 0u);
                if (zero.hasError() || zero.getResult() != 0u || input.isComplete()) return 1;
                first : Optional<byte> = input.read();
                if (!first.hasValue() || first.getOr((byte) 0) != (byte) 'H') return 2;
                bulk : Expected<unsigned int, k::io::StreamOutOfData> = input.read(buffer, 1u, 2u);
                if (bulk.hasError() || bulk.getResult() != 2u ||
                    buffer[0u] != (byte) 'x' || buffer[1u] != (byte) 'i' ||
                    buffer[2u] != (byte) ' ' || buffer[3u] != (byte) 'y') return 3;
                if (input.skip(2uL) != 2uL || input.isComplete()) return 4;
                bulk = input.read(buffer);
                if (bulk.hasError() || bulk.getResult() != 2u ||
                    buffer[0u] != (byte) 'e' || buffer[1u] != (byte) 'r') return 5;
                last : Optional<byte> = input.read();
                if (!last.hasValue() || last.getOr((byte) 0) != (byte) 'e') return 6;
                if (input.isComplete() || input.getTag() != null) return 7;
                bulk = input.read(buffer);
                if (bulk.hasError() || bulk.getResult() != 0u || !input.isComplete() ||
                    input.getTag() == null || !input.verify(expected) ||
                    !input.verifyTruncated(prefix, 16u) || input.verify(prefix)) return 8;
                prefix[0u] = (unsigned byte) (prefix[0u] ^ 1u);
                if (input.verifyTruncated(prefix, 16u) ||
                    input.verifyTruncated(prefix, 32u)) return 12;
                if (input.read().hasValue() || input.skip(5uL) != 0uL) return 9;
                input.close();
                input.close();
                if (!input.isComplete() || !input.verify(expected)) return 10;
                bulk = input.read(buffer);
                if (!bulk.hasError() || bulk.getError() != k::io::StreamOutOfData::Closed)
                    return 11;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC input early close and source errors cannot publish a tag",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        inputCloses : int = 0;
        class BadInput : public k::io::FilterInputStream<byte> {
        public:
            BadInput(input: k::io::InputStream<byte>&) : FilterInputStream(&input) {}
            override read(buf: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                err : k::io::StreamOutOfData = k::io::StreamOutOfData::OutOfData;
                return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
            }
            override close() { ++inputCloses; _in->close(); }
        }
        class ThrowInput : public k::io::FilterInputStream<byte> {
        public:
            ThrowInput(input: k::io::InputStream<byte>&) : FilterInputStream(&input) {}
            override read(buf: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData>
                    throws(k::crypto::CryptoException) {
                throw k::crypto::CryptoOperationException();
            }
        }

        test() : int {
            try {
                data : byte[]! = message();
                expected : unsigned byte[]! = new unsigned byte[32u];
                source : k::io::ArrayInputStream<byte>(data, (int) data.size);
                early : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(source, newMac());
                if (!early.read().hasValue()) return 1;
                early.close();
                early.close();
                if (early.isComplete() || early.getTag() != null) return 2;
                rejected : bool = false;
                try { early.verify(expected); }
                catch (e: k::crypto::CryptoStateException&) { rejected = true; }
                if (!rejected) return 3;

                empty : byte[]! = new byte[0u];
                emptySource : k::io::ArrayInputStream<byte>(empty, 0);
                emptyInput : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(emptySource, newMac());
                resultZero : Expected<unsigned int, k::io::StreamOutOfData> =
                    emptyInput.read(empty);
                if (resultZero.hasError() || resultZero.getResult() != 0u ||
                    emptyInput.isComplete()) return 11;
                if (emptyInput.read().hasValue() || !emptyInput.isComplete()) return 12;
                emptyMac : k::crypto::Hmac! = newMac();
                emptyTag : unsigned byte[]! = emptyMac.finalize();
                if (!emptyInput.verify(emptyTag)) return 13;
                emptyInput.close();

                source3 : k::io::ArrayInputStream<byte>(data, (int) data.size);
                skipInput : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(source3, newMac());
                if (skipInput.skip(100uL) != 8uL || !skipInput.isComplete()) return 9;
                known : unsigned byte[]! = fromHex(String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
                if (!skipInput.verify(known)) return 10;
                skipInput.close();

                source2 : k::io::ArrayInputStream<byte>(data, (int) data.size);
                broken : BadInput(source2);
                input : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(broken, newMac());
                buf : byte[2];
                result : Expected<unsigned int, k::io::StreamOutOfData> = input.read(buf);
                if (!result.hasError() || result.getError() != k::io::StreamOutOfData::OutOfData)
                    return 4;
                result = input.read(buf);
                if (!result.hasError() || result.getError() != k::io::StreamOutOfData::Closed)
                    return 5;
                if (input.isComplete() || input.getTag() != null) return 6;
                rejected = false;
                try { input.verifyTruncated(expected, 16u); }
                catch (e: k::crypto::CryptoStateException&) { rejected = true; }
                if (!rejected) return 7;
                input.close();
                input.close();
                if (inputCloses != 1) return 8;

                source4 : k::io::ArrayInputStream<byte>(data, (int) data.size);
                throwing : ThrowInput(source4);
                input2 : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(throwing, newMac());
                rejected = false;
                try { input2.read(buf); }
                catch (e: k::crypto::CryptoOperationException&) { rejected = true; }
                if (!rejected || input2.isComplete() || input2.getTag() != null) return 14;
                result = input2.read(buf);
                if (!result.hasError() || result.getError() != k::io::StreamOutOfData::Closed)
                    return 15;
                input2.close();

                source5 : k::io::ArrayInputStream<byte>(data, (int) data.size);
                usedMac : k::crypto::Hmac! = newMac();
                usedTag : unsigned byte[]! = usedMac.finalize();
                input3 : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(source5, usedMac);
                rejected = false;
                try { input3.read(buf); }
                catch (e: k::crypto::CryptoStateException&) { rejected = true; }
                if (!rejected || input3.isComplete() || input3.getTag() != null) return 16;
                result = input3.read(buf);
                if (!result.hasError() || result.getError() != k::io::StreamOutOfData::Closed)
                    return 17;
                input3.close();
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC output poisons on MAC update or finalization failure",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        check(update: bool) : int throws(Throwable) {
            mac : k::crypto::Hmac! = newMac();
            consumed : unsigned byte[]! = mac.finalize();
            sink : k::io::ArrayOutputStream<byte>;
            output : k::io::crypto::HmacOutputStream! =
                new k::io::crypto::HmacOutputStream(sink, mac);
            failed : bool = false;
            try {
                if (update) output.write((byte) 'a');
                else output.close();
            } catch (e: k::crypto::CryptoStateException&) {
                failed = true;
            }
            if (!failed || output.isComplete() || output.getTag() != null) return 1;
            if (update && sink.size() != 1) return 2;
            output.close();
            output.close();
            return output.isComplete() ? 3 : 0;
        }

        test() : int {
            try {
                result : int = check(true);
                if (result != 0) return 10 + result;
                result = check(false);
                if (result != 0) return 20 + result;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC output poisons on write, flush and close failures",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        failureMode : int = 0;
        outputCloses : int = 0;
        outputFlushes : int = 0;
        sinkCloses : int = 0;
        class BadOutput : public k::io::FilterOutputStream<byte> {
        public:
            BadOutput(output: k::io::OutputStream<byte>&) : FilterOutputStream(&output) {}
            override write(buf: const byte[], off: unsigned int, len: unsigned int)
                    : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
                if (failureMode == 1) {
                    _out->write(buf, off, 1u);
                    throw k::crypto::CryptoOperationException();
                }
                return _out->write(buf, off, len);
            }
            override flush() : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
                ++outputFlushes;
                if ((failureMode == 2 && outputFlushes == 1) || failureMode == 4)
                    throw k::crypto::CryptoOperationException();
                return _out->flush();
            }
            override close() : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
                ++outputCloses;
                try {
                    flush();
                } catch (e: k::crypto::CryptoException&) {
                    ++sinkCloses;
                    _out->close();
                    throw;
                }
                ++sinkCloses;
                _out->close();
                if (failureMode == 3)
                    throw k::crypto::CryptoStateException();
                return this;
            }
        }

        check(mode: int) : int throws(Throwable) {
            failureMode = mode;
            outputCloses = 0;
            outputFlushes = 0;
            sinkCloses = 0;
            sink : k::io::ArrayOutputStream<byte>;
            broken : BadOutput(sink);
            output : k::io::crypto::HmacOutputStream! =
                new k::io::crypto::HmacOutputStream(broken, newMac());
            data : byte[]! = message();
            expected : unsigned byte[]! = new unsigned byte[32u];
            failed : bool = false;
            try {
                output.write(data);
                if (mode == 2) output.flush();
                output.close();
            } catch (e: k::crypto::CryptoOperationException&) {
                failed = mode == 1 || mode == 2 || mode == 4;
            } catch (e: k::crypto::CryptoStateException&) {
                failed = mode == 3;
            }
            if (!failed || output.isComplete() || output.getTag() != null) return 1;
            rejected : bool = false;
            try { output.verify(expected); }
            catch (e: k::crypto::CryptoStateException&) { rejected = true; }
            if (!rejected) return 2;
            rejected = false;
            try { output.write(data); }
            catch (e: k::crypto::CryptoStateException&) { rejected = true; }
            if (!rejected) return 3;
            output.close();
            output.close();
            if (outputCloses != 1 || sinkCloses != 1 || output.isComplete()) return 4;
            if (mode == 1 && sink.size() != 1) return 5;
            if (outputFlushes != (mode == 2 ? 2 : 1)) return 6;
            return 0;
        }

        checkSuccess() : int throws(Throwable) {
            failureMode = 0;
            outputCloses = 0;
            outputFlushes = 0;
            sinkCloses = 0;
            sink : k::io::ArrayOutputStream<byte>;
            wrapped : BadOutput(sink);
            output : k::io::crypto::HmacOutputStream! =
                new k::io::crypto::HmacOutputStream(wrapped, newMac());
            data : byte[]! = message();
            output.write(data);
            output.close();
            expected : unsigned byte[]! = fromHex(String(
                "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
            if (!output.verify(expected) || outputCloses != 1 ||
                outputFlushes != 1 || sinkCloses != 1) return 1;
            return 0;
        }

        test() : int {
            try {
                i : int = 1;
                while (i <= 4) {
                    result : int = check(i);
                    if (result != 0) return i * 10 + result;
                    ++i;
                }
                return checkSuccess() == 0 ? 0 : 50;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC streams include intentionally pre-updated prefixes",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        test() : int {
            try {
                data : byte[]! = message();
                expected : unsigned byte[]! = fromHex(String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
                outputMac : k::crypto::Hmac! = newMac();
                outputMac.update(data, 0u, 3u);
                sink : k::io::ArrayOutputStream<byte>;
                output : k::io::crypto::HmacOutputStream! =
                    new k::io::crypto::HmacOutputStream(sink, outputMac);
                output.write(data, 3u, 5u);
                output.close();
                if (sink.size() != 5 || !output.verify(expected)) return 1;

                tail : byte[]! = new byte[5u];
                i : unsigned int = 0u;
                while (i < 5u) {
                    tail[i] = data[3u + i];
                    ++i;
                }
                inputMac : k::crypto::Hmac! = newMac();
                inputMac.update(data, 0u, 3u);
                source : k::io::ArrayInputStream<byte>(tail, 5);
                input : k::io::crypto::HmacInputStream! =
                    new k::io::crypto::HmacInputStream(source, inputMac);
                if (input.skip(5uL) != 5uL || input.isComplete()) return 2;
                if (input.read().hasValue() || !input.verify(expected)) return 3;
                input.close();
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}

TEST_CASE("HMAC stream range failures poison the adapter",
          "[crypto][hmac][stream]") {
    check_jit(std::string(prelude) + R"K(
        check(off: unsigned int, len: unsigned int) : int throws(Throwable) {
            data : byte[]! = message();
            sink : k::io::ArrayOutputStream<byte>;
            output : k::io::crypto::HmacOutputStream! =
                new k::io::crypto::HmacOutputStream(sink, newMac());
            rejected : bool = false;
            try { output.write(data, off, len); }
            catch (e: IndexOutOfBoundsError&) { rejected = true; }
            if (!rejected || sink.size() != 0 || output.isComplete()) return 1;
            rejected = false;
            try { output.write((byte) 'a'); }
            catch (e: k::crypto::CryptoStateException&) { rejected = true; }
            if (!rejected) return 2;
            output.close();
            if (output.isComplete() || output.getTag() != null) return 3;
            expected : unsigned byte[]! = new unsigned byte[32u];
            rejected = false;
            try { output.verify(expected); }
            catch (e: k::crypto::CryptoStateException&) { rejected = true; }
            if (!rejected) return 4;

            source : k::io::ArrayInputStream<byte>(data, (int) data.size);
            input : k::io::crypto::HmacInputStream! =
                new k::io::crypto::HmacInputStream(source, newMac());
            buffer : byte[8];
            rejected = false;
            try { input.read(buffer, off, len); }
            catch (e: IndexOutOfBoundsError&) { rejected = true; }
            if (!rejected || source.available().getResultOr(0u) != 8u ||
                input.isComplete() || input.getTag() != null) return 5;
            readResult : Expected<unsigned int, k::io::StreamOutOfData> = input.read(buffer);
            if (!readResult.hasError() || readResult.getError() != k::io::StreamOutOfData::Closed)
                return 6;
            rejected = false;
            try { input.verify(expected); }
            catch (e: k::crypto::CryptoStateException&) { rejected = true; }
            if (!rejected) return 7;
            input.close();
            return 0;
        }

        test() : int {
            try {
                result : int = check(9u, 0u);
                if (result != 0) return 10 + result;
                result = check(7u, 2u);
                if (result != 0) return 20 + result;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
}
