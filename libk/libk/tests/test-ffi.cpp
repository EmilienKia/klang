/*
 * K Language standard library — public runtime C FFI tests
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

#include "helpers.hpp"

#include <cerrno>
#include <cstdint>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined"
#endif
#ifndef LIBK_FFI_FIXTURE_PATH
#error "LIBK_FFI_FIXTURE_PATH must name the native FFI shared-library fixture"
#endif

namespace {
int fixture_calls = 0;

int run_ffi(std::string_view source) {
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto main = jit->lookup_main_entry_symbol<int (*)(int, char**)>();
    REQUIRE(main != nullptr);
    static const char* argv[] = {"ffi-test", nullptr};
    return main(1, const_cast<char**>(argv));
}
}

extern "C" int klang_ffi_add(int a, int b) {
    ++fixture_calls;
    return a + b;
}

extern "C" double klang_ffi_mixed(signed char a, unsigned short b, int c,
                                  long d, float e, double f) {
    ++fixture_calls;
    return a + b + c + d + static_cast<double>(e) + f;
}

extern "C" signed char klang_ffi_schar() {
    ++fixture_calls;
    return -113;
}

extern "C" short klang_ffi_short() {
    ++fixture_calls;
    return -30123;
}

extern "C" unsigned long long klang_ffi_unsigned() {
    ++fixture_calls;
    return UINT64_MAX;
}

extern "C" bool klang_ffi_bool(bool value) {
    ++fixture_calls;
    return value;
}

extern "C" float klang_ffi_float(float value) {
    ++fixture_calls;
    return value;
}

extern "C" void klang_ffi_void() {
    ++fixture_calls;
}

extern "C" int klang_ffi_errno() {
    ++fixture_calls;
    errno = EDOM;
    return -1;
}

extern "C" int klang_ffi_errno_code() {
    return EDOM;
}

extern "C" void* klang_ffi_pointer(void* value) {
    ++fixture_calls;
    return value;
}

extern "C" int klang_ffi_read_byte(const signed char* value) {
    return *value;
}

#if defined(__ELF__)
asm(".globl klang_ffi_zero\n"
    ".type klang_ffi_zero, @object\n"
    ".set klang_ffi_zero, 0\n");
#endif

TEST_CASE("runtime C FFI: imported aliases, type queries and immutable signatures",
          "[libk][ffi][signature][alias]") {
    CHECK(run_ffi(R"K(
        module __ffi_signatures__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            cchar : Char = -7;
            signedChar : SignedChar = cchar;
            uchar : UnsignedChar = 250;
            s : Short = -30000;
            us : UShort = 60000;
            i : Int = 42;
            ui : UInt = 42u;
            l : Long = 1000L;
            ul : ULong = 1000uL;
            ll : LongLong = l;
            ull : ULongLong = ul;
            size : SizeT = ull;
            diff : PtrdiffT = ll;
            sizeAlias : Size = size;
            diffAlias : Ptrdiff = diff;
            f : Float = 1.5;
            d : Double = 2.25;
            b : Bool = true;
            i8 : Int8 = signedChar;
            u8 : UInt8 = uchar;
            i16 : Int16 = s;
            u16 : UInt16 = us;
            i32 : Int32 = i;
            u32 : UInt32 = ui;
            i64 : Int64 = ll;
            u64 : UInt64 = ull;
            if (i8 != -7 || u8 != 250 || i16 != -30000 || u16 != 60000 ||
                i32 != 42 || u32 != 42u || i64 != 1000L || u64 != 1000uL ||
                sizeAlias != 1000uL || diffAlias != 1000L || !b || f != 1.5 || d != 2.25) return 1;
            if ((int) CType::CHAR != 12 || (int) CType::INT != 17 ||
                (int) CType::LLONG != 21 || (int) CType::BOOL != 25 || (int) CAbi::DEFAULT != 0) return 2;
            charSize : Expected<unsigned int, FfiError> = typeSize(CType::CHAR);
            longSize : Expected<unsigned int, FfiError> = typeSize(CType::LLONG);
            charType : Expected<CType, FfiError> = canonicalType(CType::CHAR);
            if (!charSize.hasResult() || charSize.getResult() != 1u ||
                !longSize.hasResult() || longSize.getResult() != 8u ||
                !charType.hasResult() || charType.getResult() != CType::SINT8) return 3;
            invalidSize : Expected<unsigned int, FfiError> = typeSize((CType) 99);
            invalidType : Expected<CType, FfiError> = canonicalType((CType) 99);
            if (!invalidSize.hasError() || !invalidType.hasError() ||
                invalidType.getError().code() != FfiErrorCode::INVALID_TYPE) return 4;
            parameters : CType[2] = {CType::INT, CType::INT};
            signature : CSignature(CType::INT, parameters);
            parameters[0] = CType::DOUBLE;
            if (signature.getParameterType(0u) != CType::INT) return 5;
            detached : Vector<CType> = signature.getParameterTypes();
            detached.clear();
            if (signature.getParameterCount() != 2u) return 6;
            builder : CSignatureBuilder;
            builder.returns(CType::INT).parameter(CType::INT).parameter(CType::INT)
                .callingConvention(CAbi::DEFAULT);
            built : CSignature = builder.build();
            preparedResult : Expected<CPreparedSignature, FfiError> = builder.prepare();
            if (!preparedResult.hasResult()) return 7;
            prepared : CPreparedSignature = preparedResult.getResult();
            copy : CPreparedSignature(prepared);
            builder.reset();
            builder.setReturnType(CType::DOUBLE);
            builder.setAbi(CAbi::DEFAULT);
            builder.addParameter(CType::FLOAT);
            if (built.getReturnType() != CType::INT || built.getParameterCount() != 2u ||
                prepared.getReturnType() != CType::INT || prepared.getParameterCount() != 2u ||
                copy.getParameterType(1u) != CType::INT || !copy.isPrepared()) return 8;
            exposed : Vector<CType> = prepared.getParameterTypes();
            exposed.clear();
            snapshot : CSignature = prepared.getSignature();
            snapshot = CSignature();
            if (prepared.getParameterCount() != 2u || copy.getParameterCount() != 2u) return 9;
            built = built;
            if (built.getParameterCount() != 2u) return 10;
            return 0;
        }
    )K") == 0);
}

TEST_CASE("runtime C FFI: direct and fluent calls retain signatures and scalar results",
          "[libk][ffi][call][copy]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_calls__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            function : CFunction;
            {
                image : Shared<ApplicationModule> = applicationModule();
                lookup : Expected<NativeSymbol, SymbolLookupError> =
                    image.get()->lookupSymbol(String("klang_ffi_add"));
                if (!lookup.hasResult()) return 1;
                symbol : NativeSymbol = lookup.getResult();
                builder : CSignatureBuilder;
                preparation : Expected<CPreparedSignature, FfiError> =
                    builder.returns(CType::INT).parameter(CType::INT).parameter(CType::INT).prepare();
                if (!preparation.hasResult()) return 2;
                prepared : CPreparedSignature = preparation.getResult();
                binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                if (!binding.hasResult()) return 3;
                function = binding.getResult();
                builder.reset();
                prepared = CPreparedSignature();
                symbol = NativeSymbol();
                image.reset();
            }
            if (function.getSymbol().isEmpty() || function.getSignature().getReturnType() != CType::INT ||
                function.getPreparedSignature().getParameterCount() != 2u || function.capturesErrno()) return 4;
            args : CValue[2];
            args[0] = CValue(19);
            args[1] = CValue(23);
            direct : Expected<CallResult, FfiError> = function.invoke(args);
            fluent : Expected<CallResult, FfiError> =
                function.call().argument(CValue(19)).argument(CValue(23)).invoke();
            if (!direct.hasResult() || !fluent.hasResult()) return 5;
            first : CallResult = direct.getResult();
            second : CallResult(fluent.getResult());
            assigned : CallResult;
            assigned = first;
            signedResult : Expected<long, FfiError> = assigned.asSigned();
            if (!signedResult.hasResult() || signedResult.getResult() != 42L ||
                second.asSigned().getResult() != 42L || first.kind() != ResultKind::SIGNED ||
                !first.hasReturnValue() || first.isVoid()) return 6;
            if (!first.asUnsigned().hasError() || !first.asFloating().hasError() ||
                !first.asPointer().hasError() || first.errnoCaptured() || !first.getErrno().hasError()) return 7;
            savedCall : CCallBuilder = function.call();
            savedCall.addArgument(CValue(20));
            savedCall.argument(CValue(22));
            copiedCall : CCallBuilder(savedCall);
            savedCall.reset();
            exposed : Vector<CValue> = copiedCall.getArguments();
            exposed.clear();
            if (copiedCall.getArgumentCount() != 2u || savedCall.getArgumentCount() != 0u) return 8;
            last : Expected<CallResult, FfiError> = copiedCall.invoke();
            if (!last.hasResult() || last.getResult().asSigned().getResult() != 42L ||
                first.asSigned().getResult() != 42L) return 9;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 3);
}

TEST_CASE("runtime C FFI: validation rejects calls before foreign code executes",
          "[libk][ffi][validation]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_validation__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            image : Shared<ApplicationModule> = applicationModule();
            lookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_add"));
            if (!lookup.hasResult()) return 1;
            builder : CSignatureBuilder;
            preparation : Expected<CPreparedSignature, FfiError> =
                builder.returns(CType::INT).parameter(CType::INT).parameter(CType::INT).prepare();
            if (!preparation.hasResult()) return 2;
            prepared : CPreparedSignature = preparation.getResult();
            symbol : NativeSymbol = lookup.getResult();
            binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
            if (!binding.hasResult()) return 3;
            function : CFunction = binding.getResult();
            args : CValue[2];
            args[0] = CValue(1);
            args[1] = CValue(2.0);
            mismatch : Expected<CallResult, FfiError> = function.invoke(args);
            if (!mismatch.hasError() || mismatch.getError().code() != FfiErrorCode::ARGUMENT_TYPE ||
                mismatch.getError().argumentIndex() != 1 || mismatch.getError().status() != 5) return 4;
            tooFew : Expected<CallResult, FfiError> = function.call().argument(CValue(1)).invoke();
            tooMany : Expected<CallResult, FfiError> =
                function.call().argument(CValue(1)).argument(CValue(2)).argument(CValue(3)).invoke();
            one : CValue[1];
            one[0] = CValue(1);
            directCount : Expected<CallResult, FfiError> = function.invoke(one);
            if (!tooFew.hasError() || !tooMany.hasError() || !directCount.hasError() ||
                tooFew.getError().code() != FfiErrorCode::ARGUMENT_COUNT ||
                directCount.getError().argumentIndex() != -1) return 5;
            args[1] = CValue::signedValue(CType::INT, 2147483648L);
            range : Expected<CallResult, FfiError> = function.invoke(args);
            if (!range.hasError() || range.getError().code() != FfiErrorCode::OUT_OF_RANGE ||
                range.getError().argumentIndex() != 1) return 6;
            args[1] = CValue::unsignedValue(CType::INT, 2uL);
            representation : Expected<CallResult, FfiError> = function.invoke(args);
            if (!representation.hasError() || representation.getError().code() != FfiErrorCode::ARGUMENT_TYPE) return 7;
            args[1] = CValue();
            empty : Expected<CallResult, FfiError> = function.invoke(args);
            if (!empty.hasError() || empty.getError().argumentIndex() != 1) return 8;
            invalid : CSignatureBuilder;
            invalid.parameter(CType::VOID);
            voidParam : Expected<CPreparedSignature, FfiError> = invalid.prepare();
            invalid.reset();
            invalid.returns((CType) 99);
            badType : Expected<CPreparedSignature, FfiError> = invalid.prepare();
            invalid.reset();
            invalid.callingConvention((CAbi) 99);
            badAbi : Expected<CPreparedSignature, FfiError> = invalid.prepare();
            if (!voidParam.hasError() || voidParam.getError().code() != FfiErrorCode::INVALID_SIGNATURE ||
                !badType.hasError() || badType.getError().code() != FfiErrorCode::INVALID_TYPE ||
                !badAbi.hasError() || badAbi.getError().code() != FfiErrorCode::UNSUPPORTED_ABI) return 9;
            error : FfiError = range.getError();
            copied : FfiError(error);
            copied = copied;
            if (copied.status() != 6 || copied.argumentIndex() != 1 || copied.message().size() == 0u) return 10;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 0);
}

TEST_CASE("runtime C FFI: mixed native integer and floating parameter slots",
          "[libk][ffi][arithmetic]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_mixed__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            image : Shared<ApplicationModule> = applicationModule();
            lookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_mixed"));
            if (!lookup.hasResult()) return 1;
            symbol : NativeSymbol = lookup.getResult();
            builder : CSignatureBuilder;
            preparation : Expected<CPreparedSignature, FfiError> =
                builder.returns(CType::DOUBLE).parameter(CType::CHAR).parameter(CType::USHORT)
                    .parameter(CType::INT).parameter(CType::LONG).parameter(CType::FLOAT)
                    .parameter(CType::DOUBLE).prepare();
            if (!preparation.hasResult()) return 2;
            prepared : CPreparedSignature = preparation.getResult();
            binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
            if (!binding.hasResult()) return 3;
            function : CFunction = binding.getResult();
            a : Char = -7;
            b : UShort = 60000;
            c : Int = 17;
            d : Long = 1000L;
            e : Float = 1.5;
            f : Double = 2.25;
            args : CValue[6];
            args[0] = CValue(a);
            args[1] = CValue(b);
            args[2] = CValue(c);
            args[3] = CValue(d);
            args[4] = CValue(e);
            args[5] = CValue(f);
            direct : Expected<CallResult, FfiError> = function.invoke(args);
            fluent : Expected<CallResult, FfiError> =
                function.call().argument(CValue::signedValue(CType::SINT8, -7L))
                    .argument(CValue::unsignedValue(CType::USHORT, 60000uL))
                    .argument(CValue::signedValue(CType::INT, 17L))
                    .argument(CValue::signedValue(CType::LONG, 1000L))
                    .argument(CValue::floatingValue(CType::FLOAT, 1.5))
                    .argument(CValue::floatingValue(CType::DOUBLE, 2.25)).invoke();
            if (!direct.hasResult() || !fluent.hasResult()) return 4;
            result : CallResult = direct.getResult();
            if (!result.asFloating().hasResult() || result.asFloating().getResult() != 61013.75 ||
                fluent.getResult().asFloating().getResult() != 61013.75 ||
                !result.asSigned().hasError() || !result.asUnsigned().hasError()) return 5;
            args[0] = CValue::signedValue(CType::CHAR, 128L);
            rejected : Expected<CallResult, FfiError> = function.invoke(args);
            if (!rejected.hasError() || rejected.getError().code() != FfiErrorCode::OUT_OF_RANGE ||
                rejected.getError().argumentIndex() != 0) return 6;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 2);
}

TEST_CASE("runtime C FFI: narrow returns, unsigned width, bool and void categories",
          "[libk][ffi][result]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_result_categories__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            image : Shared<ApplicationModule> = applicationModule();
            builder : CSignatureBuilder;
            scharPrep : Expected<CPreparedSignature, FfiError> = builder.returns(CType::SCHAR).prepare();
            shortPrep : Expected<CPreparedSignature, FfiError> = builder.returns(CType::SHORT).prepare();
            unsignedPrep : Expected<CPreparedSignature, FfiError> = builder.returns(CType::ULLONG).prepare();
            if (!scharPrep.hasResult() || !shortPrep.hasResult() || !unsignedPrep.hasResult()) return 1;
            aLookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_schar"));
            bLookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_short"));
            uLookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_unsigned"));
            if (!aLookup.hasResult() || !bLookup.hasResult() || !uLookup.hasResult()) return 2;
            aSymbol : NativeSymbol = aLookup.getResult();
            bSymbol : NativeSymbol = bLookup.getResult();
            uSymbol : NativeSymbol = uLookup.getResult();
            aPrepared : CPreparedSignature = scharPrep.getResult();
            bPrepared : CPreparedSignature = shortPrep.getResult();
            uPrepared : CPreparedSignature = unsignedPrep.getResult();
            aBind : Expected<CFunction, FfiError> = CFunction::bind(aSymbol, aPrepared);
            bBind : Expected<CFunction, FfiError> = CFunction::bind(bSymbol, bPrepared);
            uBind : Expected<CFunction, FfiError> = CFunction::bind(uSymbol, uPrepared);
            if (!aBind.hasResult() || !bBind.hasResult() || !uBind.hasResult()) return 3;
            aFunction : CFunction = aBind.getResult();
            bFunction : CFunction = bBind.getResult();
            uFunction : CFunction = uBind.getResult();
            a : Expected<CallResult, FfiError> = aFunction.call().invoke();
            b : Expected<CallResult, FfiError> = bFunction.call().invoke();
            u : Expected<CallResult, FfiError> = uFunction.call().invoke();
            if (!a.hasResult() || !b.hasResult() || !u.hasResult() ||
                a.getResult().asSigned().getResult() != -113L ||
                b.getResult().asSigned().getResult() != -30123L ||
                u.getResult().asUnsigned().getResult() != 18446744073709551615uL ||
                !u.getResult().asSigned().hasError()) return 4;
            builder.reset();
            boolPrep : Expected<CPreparedSignature, FfiError> =
                builder.returns(CType::BOOL).parameter(CType::BOOL).prepare();
            boolLookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_bool"));
            if (!boolPrep.hasResult() || !boolLookup.hasResult()) return 5;
            boolPrepared : CPreparedSignature = boolPrep.getResult();
            boolSymbol : NativeSymbol = boolLookup.getResult();
            boolBind : Expected<CFunction, FfiError> = CFunction::bind(boolSymbol, boolPrepared);
            if (!boolBind.hasResult()) return 6;
            boolFunction : CFunction = boolBind.getResult();
            boolean : Expected<CallResult, FfiError> = boolFunction.call().argument(CValue(true)).invoke();
            badBool : Expected<CallResult, FfiError> =
                boolFunction.call().argument(CValue::unsignedValue(CType::BOOL, 2uL)).invoke();
            signedBool : Expected<CallResult, FfiError> =
                boolFunction.call().argument(CValue::signedValue(CType::BOOL, 1L)).invoke();
            byteBool : Expected<CallResult, FfiError> =
                boolFunction.call().argument(CValue::unsignedValue(CType::UINT8, 1uL)).invoke();
            if (!boolean.hasResult() || boolean.getResult().asUnsigned().getResult() != 1uL ||
                !badBool.hasError() || badBool.getError().code() != FfiErrorCode::OUT_OF_RANGE ||
                !signedBool.hasError() || signedBool.getError().code() != FfiErrorCode::ARGUMENT_TYPE ||
                !byteBool.hasError()) return 7;
            builder.reset();
            voidPrep : Expected<CPreparedSignature, FfiError> = builder.prepare();
            voidLookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_void"));
            if (!voidPrep.hasResult() || !voidLookup.hasResult()) return 8;
            voidPrepared : CPreparedSignature = voidPrep.getResult();
            voidSymbol : NativeSymbol = voidLookup.getResult();
            voidBind : Expected<CFunction, FfiError> = CFunction::bind(voidSymbol, voidPrepared);
            if (!voidBind.hasResult()) return 9;
            voidFunction : CFunction = voidBind.getResult();
            noReturn : Expected<CallResult, FfiError> = voidFunction.call().invoke();
            if (!noReturn.hasResult()) return 10;
            result : CallResult = noReturn.getResult();
            if (!result.isVoid() || result.hasReturnValue() || result.kind() != ResultKind::VOID ||
                !result.asSigned().hasError() || !result.asUnsigned().hasError() ||
                !result.asFloating().hasError() || !result.asPointer().hasError()) return 11;
            unavailable : CallResult;
            if (unavailable.isVoid() || unavailable.hasReturnValue() ||
                unavailable.kind() != ResultKind::UNAVAILABLE || !unavailable.asSigned().hasError()) return 12;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 5);
}

TEST_CASE("runtime C FFI: float conversion checks and explicit errno policy",
          "[libk][ffi][float][errno]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_float_errno__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        @ffi::Extern("C") private klang_ffi_errno_code() : int;
        main() : int {
            image : Shared<ApplicationModule> = applicationModule();
            builder : CSignatureBuilder;
            fp : Expected<CPreparedSignature, FfiError> =
                builder.returns(CType::FLOAT).parameter(CType::FLOAT).prepare();
            fl : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_float"));
            if (!fp.hasResult() || !fl.hasResult()) return 1;
            fPrepared : CPreparedSignature = fp.getResult();
            fSymbol : NativeSymbol = fl.getResult();
            fb : Expected<CFunction, FfiError> = CFunction::bind(fSymbol, fPrepared);
            if (!fb.hasResult()) return 2;
            function : CFunction = fb.getResult();
            ok : Expected<CallResult, FfiError> =
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.5)).invoke();
            overflow : Expected<CallResult, FfiError> =
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.0e300)).invoke();
            underflow : Expected<CallResult, FfiError> =
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.0e-300)).invoke();
            if (!ok.hasResult() || ok.getResult().asFloating().getResult() != 1.5 ||
                !overflow.hasError() || overflow.getError().code() != FfiErrorCode::OUT_OF_RANGE ||
                !underflow.hasError() || underflow.getError().code() != FfiErrorCode::OUT_OF_RANGE) return 3;
            builder.reset();
            ep : Expected<CPreparedSignature, FfiError> = builder.returns(CType::INT).prepare();
            el : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_errno"));
            if (!ep.hasResult() || !el.hasResult()) return 4;
            ePrepared : CPreparedSignature = ep.getResult();
            eSymbol : NativeSymbol = el.getResult();
            capturedBind : Expected<CFunction, FfiError> = CFunction::bind(eSymbol, ePrepared, true);
            normalBind : Expected<CFunction, FfiError> = CFunction::bind(eSymbol, ePrepared, false);
            if (!capturedBind.hasResult() || !normalBind.hasResult()) return 5;
            capturedFunction : CFunction = capturedBind.getResult();
            normalFunction : CFunction = normalBind.getResult();
            if (!capturedFunction.capturesErrno() || normalFunction.capturesErrno()) return 6;
            captured : Expected<CallResult, FfiError> = capturedFunction.call().invoke();
            normal : Expected<CallResult, FfiError> = normalFunction.call().invoke();
            if (!captured.hasResult() || !normal.hasResult()) return 7;
            first : CallResult = captured.getResult();
            second : CallResult = normal.getResult();
            copy : CallResult(first);
            if (!first.errnoCaptured() || !copy.getErrno().hasResult() ||
                copy.getErrno().getResult() != klang_ffi_errno_code() ||
                first.asSigned().getResult() != -1L || second.errnoCaptured() ||
                !second.getErrno().hasError()) return 8;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 3);
}

TEST_CASE("runtime C FFI: returned pointers retain argument owners and module symbols",
          "[libk][ffi][pointer][lifetime]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_pointer_lifetimes__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        released : int = 0;
        @ffi::Extern("C") private klang_ffi_read_byte(value : byte*) : int;
        class Payload : public Object {
            _value : byte;
        public:
            Payload() : _value(42) {}
            ~Payload() { ++released; }
            address() : byte* { return &_value; }
        }
        main() : int {
            saved : CallResult;
            {
                image : Shared<ApplicationModule> = applicationModule();
                lookup : Expected<NativeSymbol, SymbolLookupError> =
                    image.get()->lookupSymbol(String("klang_ffi_pointer"));
                if (!lookup.hasResult()) return 1;
                symbol : NativeSymbol = lookup.getResult();
                moduleOwner : Shared<Module> = symbol.owner();
                owners : int = moduleOwner.useCount();
                symbolPointer : NativePointer = NativePointer::fromSymbol(symbol);
                if (symbolPointer.isNull() || symbolPointer.rawAddress() != symbol.rawAddress() ||
                    moduleOwner.useCount() <= owners) return 2;
                builder : CSignatureBuilder;
                preparation : Expected<CPreparedSignature, FfiError> =
                    builder.returns(CType::POINTER).parameter(CType::POINTER).prepare();
                if (!preparation.hasResult()) return 3;
                prepared : CPreparedSignature = preparation.getResult();
                binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                if (!binding.hasResult()) return 4;
                function : CFunction = binding.getResult();
                nullPointer : NativePointer = NativePointer::borrowed(null);
                nullResult : Expected<CallResult, FfiError> =
                    function.call().argument(CValue::pointerValue(nullPointer)).invoke();
                if (!nullResult.hasResult() || !nullResult.getResult().asPointer().hasResult() ||
                    !nullResult.getResult().asPointer().getResult().isNull()) return 14;
                owner : Shared<Payload>(new Payload());
                erased : Shared<Object>(owner);
                pointer : NativePointer = NativePointer::retained(owner.get()->address(), erased);
                call : CCallBuilder = function.call().argument(CValue::pointerValue(pointer));
                owner.reset();
                erased.reset();
                pointer = NativePointer();
                if (released != 0) return 5;
                result : Expected<CallResult, FfiError> = call.invoke();
                if (!result.hasResult()) return 6;
                saved = result.getResult();
                call.reset();
                if (!saved.asPointer().hasResult() || !saved.asSigned().hasError()) return 7;
            }
            if (released != 0) return 8;
            pointer : NativePointer;
            {
                extracted : Expected<NativePointer, FfiError> = saved.asPointer();
                if (!extracted.hasResult()) return 9;
                pointer = extracted.getResult();
            }
            saved = CallResult();
            if (released != 0 || pointer.isNull() || !pointer.keepalive() ||
                klang_ffi_read_byte(pointer.rawAddress()) != 42) return 10;
            copy : NativePointer(pointer);
            pointer = NativePointer();
            if (released != 0 || klang_ffi_read_byte(copy.rawAddress()) != 42) return 11;
            copy = NativePointer();
            if (released != 1) return 12;
            nullPointer : NativePointer = NativePointer::borrowed(null);
            if (!nullPointer.isNull() || nullPointer.rawAddress() != null || nullPointer.keepalive()) return 13;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 2);
}

TEST_CASE("runtime C FFI: empty symbols and unprepared signatures cannot bind",
          "[libk][ffi][symbol]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_empty_symbols__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            builder : CSignatureBuilder;
            preparation : Expected<CPreparedSignature, FfiError> = builder.returns(CType::INT).prepare();
            if (!preparation.hasResult()) return 1;
            prepared : CPreparedSignature = preparation.getResult();
            empty : NativeSymbol;
            rejected : Expected<CFunction, FfiError> = CFunction::bind(empty, prepared);
            if (!rejected.hasError() || rejected.getError().code() != FfiErrorCode::NULL_FUNCTION) return 2;
            image : Shared<ApplicationModule> = applicationModule();
            lookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_schar"));
            if (!lookup.hasResult()) return 3;
            symbol : NativeSymbol = lookup.getResult();
            unprepared : CPreparedSignature;
            invalid : Expected<CFunction, FfiError> = CFunction::bind(symbol, unprepared);
            if (!invalid.hasError() || invalid.getError().code() != FfiErrorCode::INVALID_SIGNATURE) return 4;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 0);
}

TEST_CASE("runtime C FFI: loaded native library exposes typed scalar and pointer calls",
          "[libk][ffi][loaded-library]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_loaded_library__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            function : CFunction;
            savedPointer : CallResult;
            output : long = 12L;
            {
                loaded : Expected<Shared<Module>, LoadModuleError> =
                    loadModule(String(")K" LIBK_FFI_FIXTURE_PATH R"K("));
                if (!loaded.hasResult()) return 1;
                image : Shared<Module> = loaded.getResult();
                if (!image || image.get()->kind() != ModuleKind::NATIVE_LIBRARY) return 2;
                {
                    lookup : Expected<NativeSymbol, SymbolLookupError> =
                        image.get()->lookupSymbol(String("klang_ffi_fixture_int"));
                    if (!lookup.hasResult()) return 3;
                    symbol : NativeSymbol = lookup.getResult();
                    builder : CSignatureBuilder;
                    preparation : Expected<CPreparedSignature, FfiError> =
                        builder.returns(CType::INT).parameter(CType::INT).prepare();
                    if (!preparation.hasResult()) return 4;
                    prepared : CPreparedSignature = preparation.getResult();
                    binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                    if (!binding.hasResult()) return 5;
                    function = binding.getResult();
                }
                {
                    lookup : Expected<NativeSymbol, SymbolLookupError> =
                        image.get()->lookupSymbol(String("klang_ffi_fixture_mixed"));
                    if (!lookup.hasResult()) return 6;
                    symbol : NativeSymbol = lookup.getResult();
                    builder : CSignatureBuilder;
                    preparation : Expected<CPreparedSignature, FfiError> =
                        builder.returns(CType::DOUBLE).parameter(CType::SINT32).parameter(CType::DOUBLE)
                            .parameter(CType::UINT64).parameter(CType::FLOAT).prepare();
                    if (!preparation.hasResult()) return 7;
                    prepared : CPreparedSignature = preparation.getResult();
                    binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                    if (!binding.hasResult()) return 8;
                    mixed : CFunction = binding.getResult();
                    result : Expected<CallResult, FfiError> =
                        mixed.call().argument(CValue::signedValue(CType::SINT32, -3L))
                            .argument(CValue::floatingValue(CType::DOUBLE, 2.5))
                            .argument(CValue::unsignedValue(CType::UINT64, 10uL))
                            .argument(CValue::floatingValue(CType::FLOAT, 1.25)).invoke();
                    if (!result.hasResult() || !result.getResult().asFloating().hasResult() ||
                        result.getResult().asFloating().getResult() != 10.75) return 9;
                }
                {
                    lookup : Expected<NativeSymbol, SymbolLookupError> =
                        image.get()->lookupSymbol(String("klang_ffi_fixture_out"));
                    if (!lookup.hasResult()) return 10;
                    symbol : NativeSymbol = lookup.getResult();
                    builder : CSignatureBuilder;
                    preparation : Expected<CPreparedSignature, FfiError> =
                        builder.parameter(CType::POINTER).parameter(CType::SINT64).prepare();
                    if (!preparation.hasResult()) return 11;
                    prepared : CPreparedSignature = preparation.getResult();
                    binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                    if (!binding.hasResult()) return 12;
                    write : CFunction = binding.getResult();
                    pointer : NativePointer = NativePointer::borrowed((byte*) &output);
                    arguments : CValue[2];
                    arguments[0] = CValue::pointerValue(pointer);
                    arguments[1] = CValue::signedValue(CType::SINT64, 37L);
                    result : Expected<CallResult, FfiError> = write.invoke(arguments);
                    if (!result.hasResult() || !result.getResult().isVoid() || output != 37L) return 13;
                }
                {
                    lookup : Expected<NativeSymbol, SymbolLookupError> =
                        image.get()->lookupSymbol(String("klang_ffi_fixture_pointer"));
                    if (!lookup.hasResult()) return 14;
                    symbol : NativeSymbol = lookup.getResult();
                    builder : CSignatureBuilder;
                    preparation : Expected<CPreparedSignature, FfiError> =
                        builder.returns(CType::POINTER).parameter(CType::POINTER).prepare();
                    if (!preparation.hasResult()) return 15;
                    prepared : CPreparedSignature = preparation.getResult();
                    binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
                    if (!binding.hasResult()) return 16;
                    echo : CFunction = binding.getResult();
                    pointer : NativePointer = NativePointer::borrowed((byte*) &output);
                    result : Expected<CallResult, FfiError> =
                        echo.call().argument(CValue::pointerValue(pointer)).invoke();
                    if (!result.hasResult()) return 17;
                    savedPointer = result.getResult();
                }
                {
                    lookup : Expected<NativeSymbol, SymbolLookupError> =
                        image.get()->lookupSymbol(String("klang_ffi_fixture_set_errno"));
                    if (!lookup.hasResult()) return 18;
                    symbol : NativeSymbol = lookup.getResult();
                    builder : CSignatureBuilder;
                    preparation : Expected<CPreparedSignature, FfiError> =
                        builder.returns(CType::INT).parameter(CType::INT).prepare();
                    if (!preparation.hasResult()) return 19;
                    prepared : CPreparedSignature = preparation.getResult();
                    binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared, true);
                    if (!binding.hasResult()) return 20;
                    setError : CFunction = binding.getResult();
                    result : Expected<CallResult, FfiError> =
                        setError.call().argument(CValue(42)).invoke();
                    if (!result.hasResult() || !result.getResult().errnoCaptured() ||
                        !result.getResult().getErrno().hasResult() ||
                        result.getResult().getErrno().getResult() != 42 ||
                        result.getResult().asSigned().getResult() != -1L) return 21;
                }
            }
            result : Expected<CallResult, FfiError> = function.call().argument(CValue(42)).invoke();
            if (!result.hasResult() || !result.getResult().asSigned().hasResult() ||
                result.getResult().asSigned().getResult() != 127L) return 22;
            owner : Shared<Module> = function.getSymbol().owner();
            if (!owner || owner.get()->kind() != ModuleKind::NATIVE_LIBRARY) return 23;
            extracted : Expected<NativePointer, FfiError> = savedPointer.asPointer();
            if (!extracted.hasResult()) return 24;
            pointer : NativePointer = extracted.getResult();
            if (pointer.isNull() || pointer.rawAddress() != (byte*) &output || !pointer.keepalive()) return 25;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 0);
}

#if defined(__ELF__)
TEST_CASE("runtime C FFI: a successful zero-address lookup is not callable",
          "[libk][ffi][symbol][zero-address]") {
    fixture_calls = 0;
    CHECK(run_ffi(R"K(
        module __ffi_zero_symbol__;
        using namespace k::ffi;
        using namespace k::ffi::c;
        main() : int {
            image : Shared<ApplicationModule> = applicationModule();
            lookup : Expected<NativeSymbol, SymbolLookupError> =
                image.get()->lookupSymbol(String("klang_ffi_zero"));
            if (!lookup.hasResult()) return 1;
            symbol : NativeSymbol = lookup.getResult();
            if (symbol.isEmpty() || symbol.hasAddress()) return 2;
            builder : CSignatureBuilder;
            preparation : Expected<CPreparedSignature, FfiError> = builder.returns(CType::INT).prepare();
            if (!preparation.hasResult()) return 3;
            prepared : CPreparedSignature = preparation.getResult();
            binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
            if (!binding.hasError() || binding.getError().code() != FfiErrorCode::NULL_FUNCTION) return 4;
            pointer : NativePointer = NativePointer::fromSymbol(symbol);
            if (!pointer.isNull()) return 5;
            return 0;
        }
    )K") == 0);
    CHECK(fixture_calls == 0);
}
#endif
