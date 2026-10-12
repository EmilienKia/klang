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
#include <string>

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

int run_adapter(std::string_view module, std::string_view source) {
    std::string program = "module " + std::string(module) + ";\n";
    program += R"K(
        using namespace k::ffi;
        using namespace k::ffi::c;
        private adapterImage() : Shared<Module> {
            application : OptionalRef<k::Application> = k::Application::getApplication();
            if (!application.hasValue()) throw FatalError(999);
            loaded : Expected<Shared<Module>, LoadModuleError> =
                application.get().loadModule(String(")K" LIBK_FFI_FIXTURE_PATH R"K("));
            if (!loaded.hasResult()) throw FatalError(1000);
            return loaded.getResult();
        }
        private adapterFunction(image : const Shared<Module>&, name : const String&,
                                signature : const CSignature&) : CFunction {
            lookup : Expected<NativeSymbol, SymbolLookupError> = image.get()->lookupSymbol(name);
            if (!lookup.hasResult()) throw FatalError(1001);
            symbol : NativeSymbol = lookup.getResult();
            preparation : Expected<CPreparedSignature, FfiError> = signature.prepare();
            if (!preparation.hasResult()) throw FatalError(1002);
            prepared : CPreparedSignature = preparation.getResult();
            binding : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
            if (!binding.hasResult()) throw FatalError(1003);
            return binding.getResult();
        }
    )K";
    program += source;
    return run_ffi(program);
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
            parameters : CType[2] {CType::INT, CType::INT};
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
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.5d)).invoke();
            overflow : Expected<CallResult, FfiError> =
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.0e300d)).invoke();
            underflow : Expected<CallResult, FfiError> =
                function.call().argument(CValue::floatingValue(CType::FLOAT, 1.0e-300d)).invoke();
            if (!ok.hasResult() || ok.getResult().asFloating().getResult() != 1.5d) return 3;
            if (!overflow.hasError() || overflow.getError().code() != FfiErrorCode::OUT_OF_RANGE) return 9;
            if (!underflow.hasError() || underflow.getError().code() != FfiErrorCode::OUT_OF_RANGE) return 10;
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
                {
                    source : const Shared<ApplicationModule>& = image;
                    owners : int = image.useCount();
                    base : Shared<Module>(source);
                    baseSource : const Shared<Module>& = base;
                    object : Shared<Object>(baseSource);
                    weak : Weak<Module>(source);
                    if (!base || !object || image.useCount() != owners + 2 ||
                        weak.useCount() != owners + 2 ||
                        base.get() != (Module*) image.get() ||
                        object.get() != (Object*) base.get()) return 15;
                }
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

TEST_CASE("runtime C FFI: transparent arrays use aligned element payloads, not headers",
          "[libk][ffi][array][adapter]") {
    CHECK(run_adapter("__ffi_array_adapters__", R"K(
        main() : int {
            image : Shared<Module> = adapterImage();
            builder : CSignatureBuilder;
            intSignature : CSignature =
                builder.returns(CType::SINT64).parameter(CType::POINTER).parameter(CType::SIZE_T).build();
            sumInts : CFunction = adapterFunction(image, String("klang_ffi_fixture_sum_ints"), intSignature);
            values : Int[3] {-3, 5, 8};
            call : CCallBuilder = sumInts.call().argument(values)
                .argument(CValue::unsignedValue(CType::SIZE_T, 3uL));
            values[1] = 20;
            result : Expected<CallResult, FfiError> = call.invoke();
            if (!result.hasResult() || result.getResult().asSigned().getResult() != 25L) return 1;
            payload : NativePointer = NativePointer::fromArray(values);
            if (payload.rawAddress() != (byte*) &values[0u] || payload.keepalive()) return 2;
            constValues : const Int[2] {7, 9};
            readOnly : Expected<CallResult, FfiError> = sumInts.call().argument(constValues)
                .argument(CValue::unsignedValue(CType::SIZE_T, 2uL)).invoke();
            if (!readOnly.hasResult() || readOnly.getResult().asSigned().getResult() != 16L) return 3;
            empty : Int[]! = new Int[0u];
            if (!NativePointer::fromArray(empty).isNull()) return 4;
            emptyCall : CCallBuilder = sumInts.call();
            emptyCall.addArgument(empty);
            emptyCall.addArgument(CValue::unsignedValue(CType::SIZE_T, 0uL));
            zero : Expected<CallResult, FfiError> = emptyCall.invoke();
            if (!zero.hasResult() || zero.getResult().asSigned().getResult() != 0L || empty == null) return 5;
            builder.reset();
            doubleSignature : CSignature =
                builder.returns(CType::DOUBLE).parameter(CType::POINTER).parameter(CType::SIZE_T).build();
            sumDoubles : CFunction = adapterFunction(image, String("klang_ffi_fixture_sum_doubles"), doubleSignature);
            doubles : Double[3] {1.5, 2.25, -0.75};
            doublePayload : NativePointer = NativePointer::fromArray(doubles);
            if (doublePayload.rawAddress() != (byte*) &doubles[0u]) return 6;
            args : CValue[2];
            args[0] = CValue::array(doubles);
            args[1] = CValue::unsignedValue(CType::SIZE_T, 3uL);
            floating : Expected<CallResult, FfiError> = sumDoubles.invoke(args);
            if (!floating.hasResult() || floating.getResult().asFloating().getResult() != 3.0) return 7;
            builder.reset();
            pointerSignature : CSignature =
                builder.returns(CType::POINTER).parameter(CType::POINTER).parameter(CType::SIZE_T).build();
            pick : CFunction = adapterFunction(image, String("klang_ffi_fixture_array_pointer"), pointerSignature);
            first : byte = 11;
            second : byte = 22;
            pointers : byte*[2] {&first, &second};
            pointerPayload : NativePointer = NativePointer::fromArray(pointers);
            if (pointerPayload.rawAddress() != (byte*) &pointers[0u]) return 8;
            selected : Expected<CallResult, FfiError> = pick.call().argument(pointers)
                .argument(CValue::unsignedValue(CType::SIZE_T, 1uL)).invoke();
            if (!selected.hasResult() || !selected.getResult().asPointer().hasResult() ||
                selected.getResult().asPointer().getResult().rawAddress() != &second) return 9;
            builder.reset();
            fillSignature : CSignature = builder.returns(CType::SIZE_T).parameter(CType::POINTER)
                .parameter(CType::SIZE_T).parameter(CType::UCHAR).build();
            fill : CFunction = adapterFunction(image, String("klang_ffi_fixture_fill"), fillSignature);
            bytes : UnsignedChar[4] {0, 1, 2, 3};
            changed : Expected<CallResult, FfiError> = fill.call().argument(bytes)
                .argument(CValue::unsignedValue(CType::SIZE_T, 4uL))
                .argument(CValue::unsignedValue(CType::UCHAR, 127uL)).invoke();
            if (!changed.hasResult() || changed.getResult().asUnsigned().getResult() != 4uL ||
                bytes[0u] != 127 || bytes[3u] != 127) return 10;
            builder.reset();
            lengthSignature : CSignature =
                builder.returns(CType::SIZE_T).parameter(CType::POINTER).build();
            length : CFunction = adapterFunction(image, String("klang_ffi_fixture_strlen"), lengthSignature);
            chars : Char[3] {65, 66, 0};
            narrow : Expected<CallResult, FfiError> = length.call().argument(chars).invoke();
            if (!narrow.hasResult() || narrow.getResult().asUnsigned().getResult() != 2uL) return 11;
            signedBytes : SignedChar[1] {-7};
            shorts : Short[1] {-32000};
            unsignedShorts : UShort[1] {65000};
            unsignedInts : UInt[1] {4000000000u};
            longs : Long[1] {-9000000000L};
            unsignedLongs : ULong[1] {18000000000uL};
            floats : Float[1] {1.25};
            if (NativePointer::fromArray(signedBytes).rawAddress() != (byte*) &signedBytes[0u] ||
                NativePointer::fromArray(shorts).rawAddress() != (byte*) &shorts[0u] ||
                NativePointer::fromArray(unsignedShorts).rawAddress() != (byte*) &unsignedShorts[0u] ||
                NativePointer::fromArray(unsignedInts).rawAddress() != (byte*) &unsignedInts[0u] ||
                NativePointer::fromArray(longs).rawAddress() != (byte*) &longs[0u] ||
                NativePointer::fromArray(unsignedLongs).rawAddress() != (byte*) &unsignedLongs[0u] ||
                NativePointer::fromArray(floats).rawAddress() != (byte*) &floats[0u]) return 12;
            all : CCallBuilder = sumInts.call();
            all.argument(signedBytes).argument(bytes).argument(shorts).argument(unsignedShorts)
                .argument(values).argument(unsignedInts).argument(longs).argument(unsignedLongs)
                .argument(floats).argument(doubles).argument(pointers);
            if (all.getArgumentCount() != 11u) return 13;
            all.reset();
            all.addArgument(signedBytes);
            all.addArgument(bytes);
            all.addArgument(shorts);
            all.addArgument(unsignedShorts);
            all.addArgument(values);
            all.addArgument(unsignedInts);
            all.addArgument(longs);
            all.addArgument(unsignedLongs);
            all.addArgument(floats);
            all.addArgument(doubles);
            all.addArgument(pointers);
            explicitValues : CValue[11];
            explicitValues[0u] = CValue::array(signedBytes);
            explicitValues[1u] = CValue::array(bytes);
            explicitValues[2u] = CValue::array(shorts);
            explicitValues[3u] = CValue::array(unsignedShorts);
            explicitValues[4u] = CValue::array(values);
            explicitValues[5u] = CValue::array(unsignedInts);
            explicitValues[6u] = CValue::array(longs);
            explicitValues[7u] = CValue::array(unsignedLongs);
            explicitValues[8u] = CValue::array(floats);
            explicitValues[9u] = CValue::array(doubles);
            explicitValues[10u] = CValue::array(pointers);
            if (all.getArgumentCount() != 11u || explicitValues[10u].getType() != CType::POINTER) return 14;
            return 0;
        }
    )K") == 0);
}

TEST_CASE("runtime C FFI: native bytes check bounds and honor managed pointer leases",
          "[libk][ffi][buffer][adapter]") {
    CHECK(run_adapter("__ffi_native_buffer__", R"K(
        main() : int {
            allocated : Expected<NativeBuffer, FfiError> = NativeBuffer::allocate(8u);
            if (!allocated.hasResult()) return 1;
            buffer : NativeBuffer = allocated.getResult();
            copy : NativeBuffer(buffer);
            if (!buffer.isAllocated() || buffer.isEmpty() || buffer.size() != 8u ||
                buffer.get(0u).getResult() != 0 || buffer.get(7u).getResult() != 0) return 2;
            if (!buffer.put(1u, 255).hasResult() || copy.get(1u).getResult() != 255) return 3;
            source : unsigned byte[3] {11, 22, 33};
            written : Expected<unsigned int, FfiError> = buffer.copyFrom(source, 2u);
            destination : byte[3];
            read : Expected<unsigned int, FfiError> = buffer.copyTo(destination, 2u);
            if (!written.hasResult() || written.getResult() != 3u || !read.hasResult() ||
                destination[0u] != 11 || destination[2u] != 33) return 4;
            signedSource : byte[1] {-1};
            if (!buffer.copyFrom(signedSource, 0u).hasResult() || buffer.get(0u).getResult() != 255) return 5;
            if (!buffer.get(8u).hasError() || buffer.get(8u).getError().code() != FfiErrorCode::OUT_OF_RANGE ||
                !buffer.put(8u, 1).hasError() || !buffer.copyFrom(source, 7u).hasError() ||
                !buffer.copyTo(destination, 4294967295u).hasError() ||
                !buffer.borrowedPointer(9u).hasError()) return 6;
            end : Expected<NativePointer, FfiError> = buffer.borrowedPointer(8u);
            if (!end.hasResult() || !end.getResult().isNull()) return 7;
            empty : unsigned byte[]! = new unsigned byte[0u];
            if (!buffer.copyFrom(empty, 8u).hasResult() || !buffer.copyTo(empty, 8u).hasResult() ||
                !buffer.copyFrom(empty, 9u).hasError()) return 8;
            independentResult : Expected<NativeBuffer, FfiError> = NativeBuffer::fromBytes(source);
            if (!independentResult.hasResult()) return 9;
            independent : NativeBuffer = independentResult.getResult();
            source[0u] = 99;
            if (independent.get(0u).getResult() != 11) return 10;
            buffer = buffer;
            if (buffer.size() != 8u || buffer.get(0u).getResult() != 255) return 11;
            weak : Weak<Object>;
            {
                pointer : NativePointer = buffer.pointer();
                second : NativePointer = copy.pointer();
                keeper : Shared<Object> = pointer.keepalive();
                secondKeeper : Shared<Object> = second.keepalive();
                weak = Weak<Object>(keeper);
                if (!buffer.hasLease() || pointer.isNull() || keeper != secondKeeper) return 12;
                if (!buffer.get(0u).hasError() || buffer.get(0u).getError().code() != FfiErrorCode::BUFFER_IN_USE ||
                    !buffer.put(0u, 0).hasError() || !buffer.copyFrom(source).hasError() ||
                    !buffer.copyTo(destination).hasError() || !buffer.borrowedPointer().hasError()) return 13;
                pointer = NativePointer();
                keeper = Shared<Object>();
                secondKeeper = Shared<Object>();
                if (!buffer.hasLease() || weak.expired() ||
                    !copy.get(0u).hasError() ||
                    copy.get(0u).getError().code() != FfiErrorCode::BUFFER_IN_USE) return 14;
                survivingCopy : NativePointer(second);
                second = NativePointer();
                if (!buffer.hasLease() || weak.expired() ||
                    !buffer.copyTo(destination).hasError()) return 101;
            }
            if (buffer.hasLease() || !weak.expired() || buffer.get(0u).getResult() != 255) return 15;
            {
                renewed : NativePointer = copy.pointer();
                another : NativePointer = buffer.pointer();
                if (!weak.expired() || !buffer.hasLease() ||
                    renewed.keepalive() != another.keepalive()) return 102;
            }
            if (buffer.hasLease() || buffer.get(0u).getResult() != 255) return 103;
            image : Shared<Module> = adapterImage();
            builder : CSignatureBuilder;
            signature : CSignature = builder.returns(CType::SIZE_T).parameter(CType::POINTER)
                .parameter(CType::SIZE_T).parameter(CType::UCHAR).build();
            fill : CFunction = adapterFunction(image, String("klang_ffi_fixture_fill"), signature);
            call : CCallBuilder = fill.call();
            call.addArgument(buffer);
            call.argument(CValue::unsignedValue(CType::SIZE_T, 8uL))
                .argument(CValue::unsignedValue(CType::UCHAR, 65uL));
            filled : Expected<CallResult, FfiError> = call.invoke();
            if (!filled.hasResult() || filled.getResult().asUnsigned().getResult() != 8uL ||
                !buffer.hasLease()) return 16;
            call.reset();
            if (buffer.hasLease() || buffer.get(0u).getResult() != 65 || buffer.get(7u).getResult() != 65) return 17;
            {
                arguments : CValue[3];
                arguments[0u] = CValue::buffer(buffer);
                arguments[1u] = CValue::unsignedValue(CType::SIZE_T, 8uL);
                arguments[2u] = CValue::unsignedValue(CType::UCHAR, 66uL);
                direct : Expected<CallResult, FfiError> = fill.invoke(arguments);
                if (!direct.hasResult()) return 18;
            }
            if (buffer.hasLease() || buffer.get(7u).getResult() != 66) return 19;
            emptyResult : Expected<NativeBuffer, FfiError> = NativeBuffer::allocate(0u);
            if (!emptyResult.hasResult()) return 20;
            emptyBuffer : NativeBuffer = emptyResult.getResult();
            if (!emptyBuffer.isAllocated() || !emptyBuffer.isEmpty() || !emptyBuffer.pointer().isNull() ||
                !emptyBuffer.get(0u).hasError()) return 21;
            defaultBuffer : NativeBuffer;
            if (defaultBuffer.isAllocated() || !defaultBuffer.pointer().isNull()) return 22;
            return 0;
        }
    )K") == 0);
}

TEST_CASE("runtime C FFI: native buffer pointer results retain storage after wrappers die",
          "[libk][ffi][buffer][lifetime][adapter]") {
    CHECK(run_adapter("__ffi_native_buffer_lifetime__", R"K(
        main() : int {
            image : Shared<Module> = adapterImage();
            builder : CSignatureBuilder;
            echoSignature : CSignature =
                builder.returns(CType::POINTER).parameter(CType::POINTER).build();
            echo : CFunction = adapterFunction(image, String("klang_ffi_fixture_pointer"), echoSignature);
            builder.reset();
            lengthSignature : CSignature =
                builder.returns(CType::SIZE_T).parameter(CType::POINTER).build();
            length : CFunction = adapterFunction(image, String("klang_ffi_fixture_strlen"), lengthSignature);
            saved : CallResult;
            weak : Weak<Object>;
            {
                bytes : unsigned byte[2] {75, 0};
                allocated : Expected<NativeBuffer, FfiError> = NativeBuffer::fromBytes(bytes);
                if (!allocated.hasResult()) return 1;
                buffer : NativeBuffer = allocated.getResult();
                result : Expected<CallResult, FfiError> = echo.call().argument(buffer).invoke();
                if (!result.hasResult()) return 2;
                saved = result.getResult();
                if (!buffer.hasLease()) return 3;
                {
                    extracted : Expected<NativePointer, FfiError> = saved.asPointer();
                    pointer : NativePointer = extracted.getResult();
                    keeper : Shared<Object> = pointer.keepalive();
                    weak = Weak<Object>(keeper);
                }
            }
            if (weak.expired()) return 4;
            {
                extracted : Expected<NativePointer, FfiError> = saved.asPointer();
                pointer : NativePointer = extracted.getResult();
                saved = CallResult();
                result : Expected<CallResult, FfiError> =
                    length.call().argument(CValue::pointerValue(pointer)).invoke();
                if (!result.hasResult() || result.getResult().asUnsigned().getResult() != 1uL ||
                    weak.expired()) return 5;
            }
            if (!weak.expired()) return 6;
            return 0;
        }
    )K") == 0);
}

TEST_CASE("runtime C FFI: UTF-8 C strings reject zero bytes and retain their terminator",
          "[libk][ffi][cstring][adapter]") {
    CHECK(run_adapter("__ffi_utf8_cstring__", R"K(
        main() : int {
            image : Shared<Module> = adapterImage();
            builder : CSignatureBuilder;
            lengthSignature : CSignature =
                builder.returns(CType::SIZE_T).parameter(CType::POINTER).build();
            length : CFunction = adapterFunction(image, String("klang_ffi_fixture_strlen"), lengthSignature);
            builder.reset();
            echoSignature : CSignature =
                builder.returns(CType::POINTER).parameter(CType::POINTER).build();
            echo : CFunction = adapterFunction(image, String("klang_ffi_fixture_pointer"), echoSignature);
            saved : CallResult;
            weak : Weak<Object>;
            {
                converted : Expected<Utf8CString, FfiError> = Utf8CString::fromString(String("Aé🙂"));
                if (!converted.hasResult()) return 1;
                string : Utf8CString = converted.getResult();
                string = string;
                if (!string.isValid() || string.isEmpty() || string.byteLength() != 7u) return 2;
                call : CCallBuilder = length.call();
                call.addArgument(string);
                result : Expected<CallResult, FfiError> = call.invoke();
                if (!result.hasResult() || result.getResult().asUnsigned().getResult() != 7uL) return 3;
                call.reset();
                {
                    borrowed : Expected<NativePointer, FfiError> = string.borrowedPointer();
                    if (!borrowed.hasResult() || borrowed.getResult().isNull() || borrowed.getResult().keepalive()) return 4;
                }
                arguments : CValue[1];
                arguments[0u] = CValue::cstring(string);
                direct : Expected<CallResult, FfiError> = length.invoke(arguments);
                if (!direct.hasResult() || direct.getResult().asUnsigned().getResult() != 7uL) return 5;
                aliased : Expected<CallResult, FfiError> = echo.call().argument(string).invoke();
                if (!aliased.hasResult()) return 6;
                saved = aliased.getResult();
                {
                    extracted : Expected<NativePointer, FfiError> = saved.asPointer();
                    pointer : NativePointer = extracted.getResult();
                    keeper : Shared<Object> = pointer.keepalive();
                    weak = Weak<Object>(keeper);
                }
            }
            if (weak.expired()) return 7;
            {
                extracted : Expected<NativePointer, FfiError> = saved.asPointer();
                pointer : NativePointer = extracted.getResult();
                saved = CallResult();
                result : Expected<CallResult, FfiError> =
                    length.call().argument(CValue::pointerValue(pointer)).invoke();
                if (!result.hasResult() || result.getResult().asUnsigned().getResult() != 7uL) return 8;
            }
            if (!weak.expired()) return 9;
            emptyResult : Expected<Utf8CString, FfiError> = Utf8CString::fromString(String(""));
            if (!emptyResult.hasResult()) return 10;
            empty : Utf8CString = emptyResult.getResult();
            if (!empty.isEmpty() || empty.byteLength() != 0u || empty.pointer().isNull()) return 11;
            emptyCall : Expected<CallResult, FfiError> = length.call().argument(empty).invoke();
            if (!emptyCall.hasResult() || emptyCall.getResult().asUnsigned().getResult() != 0uL) return 12;
            codepoints : char[4] {'a', (char) 0, 'b', (char) 0};
            withZero : String(codepoints);
            rejected : Expected<Utf8CString, FfiError> = Utf8CString::fromString(withZero);
            if (!rejected.hasError() || rejected.getError().code() != FfiErrorCode::EMBEDDED_ZERO ||
                rejected.getError().status() != 101 || rejected.getError().argumentIndex() != -1) return 13;
            invalid : Utf8CString;
            if (invalid.isValid() || invalid.isEmpty() || !invalid.borrowedPointer().hasError()) return 14;
            return 0;
        }
    )K") == 0);
}

TEST_CASE("runtime C FFI: nontrivial, boolean and UTF-32 arrays are not native pointer adapters",
          "[libk][ffi][array][negative][adapter]") {
    const auto compile = [](std::string_view source) {
        return gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    };
    CHECK_FALSE(compile(R"K(
        module __ffi_reject_struct_array__;
        class Record { value : int; }
        test() {
            records : Record[1];
            pointer : ffi::NativePointer = ffi::NativePointer::fromArray(records);
        }
    )K"));
    CHECK_FALSE(compile(R"K(
        module __ffi_reject_utf32_array__;
        test() {
            text : char[2] {'a', (char) 0};
            value : ffi::c::CValue = ffi::c::CValue::array(text);
        }
    )K"));
    CHECK_FALSE(compile(R"K(
        module __ffi_reject_bool_array__;
        test() {
            function : ffi::c::CFunction;
            values : bool[1] {true};
            call : ffi::c::CCallBuilder = function.call().argument(values);
        }
    )K"));
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
