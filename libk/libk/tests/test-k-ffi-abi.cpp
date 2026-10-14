/*
 * K Language standard library — private direct-entry K FFI ABI proofs
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

#include <ffi.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/TargetParser/Triple.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// This is a private ABI experiment, NOT permission to throw across k::ffi's
// public foreign-C boundary. No CSignature/CFunction or public FFI bridge is used.
constexpr std::string_view source = R"K(
    module k_ffi_abi;

    @ffi::Extern("C") private klang_k_ffi_abi_execute(frame : byte*);

    tokenDrops : int = 0;
    tokenSum : int = 0;
    resultDrops : int = 0;
    scopeDrops : int = 0;
    lastEntry : int = 0;

    struct Token {
        value : int;
        Token(v : int) : value(v) {}
        ~Token() { ++tokenDrops; tokenSum += value; }
    }
    struct Scope {
        ~Scope() { ++scopeDrops; }
    }
    struct Borrowed {
        value : long;
        Borrowed(v : long) : value(v) {}
    }
    struct Result {
        value : long;
        token : Token!;
        Result(v : long, t : int) : value(v), token(new Token(t)) {}
        ~Result() { ++resultDrops; }
        read() : long { return value + token->value; }
    }
    struct Receiver {
        bias : long;
        Receiver(v : long) : bias(v) {}
        calculate(multiplier : int) : long {
            lastEntry = 2;
            return bias * multiplier + 3L;
        }
        build(seed : long, input : const Borrowed&) : Result {
            lastEntry = 5;
            result : Result(bias + seed + input.value, 19);
            return result;
        }
    }
    struct DrainBox {
        token : Token!;
        DrainBox() : token(new Token(61)) {}
    }
    class ProbeError : public Exception {
        public ProbeError(code : int) : Exception(code) {}
    }
    class Base {
        public marker : long = 11L;
        public Base() {}
        public score(delta : int) : long { lastEntry = 8; return marker + delta; }
    }
    class Derived : public Base {
        public value : long = 100L;
        public Derived() {}
        public override score(delta : int) : long {
            lastEntry = 9;
            return value + delta + 1000L;
        }
    }

    receiver : Receiver(40L);
    borrowed : Borrowed(7L);
    drainBox : DrainBox;
    derived : Derived;

    scalar(a : long, b : double, c : int, d : float) : double {
        lastEntry = 1;
        return (double) a + 2.0 * b + (double) c + (double) d;
    }
    mixed(head : long, input : const Borrowed&, fraction : double,
          output : int+, nullable : const Borrowed?, tail : float) : double {
        lastEntry = 3;
        extra : long = 0L;
        if (nullable != null) extra = nullable->value;
        *output = (int) (head + input.value + extra);
        return fraction * 2.0 + (double) tail + (double) *output;
    }
    makeResult(seed : long, input : const Borrowed&) : Result {
        lastEntry = 4;
        result : Result(seed + input.value, 17);
        return result;
    }
    makeToken(value : int) : Token! { return new Token(value); }
    consume(token : Token!, code : int) : int {
        lastEntry = 6;
        guard : Scope;
        if (code != 0) throw ProbeError(code);
        return token->value;
    }
    drain(value : DrainBox#) : int {
        lastEntry = 7;
        acquired : Token! = value.token;
        return acquired->value;
    }

    // Fixture accessors supply real K objects, never invoke a selected target.
    receiverAddress() : Receiver& { return receiver; }
    borrowedAddress() : const Borrowed& { return borrowed; }
    drainAddress() : DrainBox& { return drainBox; }
    drainIsEmpty() : bool { return drainBox.token == null; }
    baseAddress() : Base& { return derived; }
    derivedAddress() : Derived& { return derived; }
    drops() : int { return tokenDrops; }
    sum() : int { return tokenSum; }
    results() : int { return resultDrops; }
    scopes() : int { return scopeDrops; }
    entered() : int { return lastEntry; }

    catchDriver(frame : byte*, expected : int) : int {
        try {
            callerGuard : Scope;
            klang_k_ffi_abi_execute(frame);
            return -1;
        } catch (error : ProbeError&) {
            if (error.getCode() != expected) return -2;
            return error.getCode();
        } catch (other : Throwable&) {
            return -3;
        }
    }
    baseCatchDriver(frame : byte*, expected : int) : int {
        try {
            callerGuard : Scope;
            klang_k_ffi_abi_execute(frame);
            return -1;
        } catch (error : Exception&) {
            if (error.getCode() != expected) return -2;
            return error.getCode();
        }
    }
)K";

void require_supported_abi(llvm::Module& module) {
    const llvm::Triple triple(module.getTargetTriple());
    INFO("K target: " << triple.str() << "; libffi default ABI: " << FFI_DEFAULT_ABI);
    // AArch64, for example, uses x8 rather than an ordinary pointer argument for
    // LLVM sret. LP64 alone is not evidence that the recipe below is valid.
    if (!triple.isOSLinux() || triple.getArch() != llvm::Triple::x86_64) {
        SKIP("Direct K sret/ffi unwind recipe is currently proved only on Linux x86-64");
    }
    REQUIRE(module.getDataLayout().getPointerSize() == sizeof(void*));
    REQUIRE(sizeof(void*) == 8);
    REQUIRE(module.getDataLayout().isLittleEndian());
}

ffi_type* physical_type(llvm::Type* type) {
    if (type->isVoidTy()) return &ffi_type_void;
    if (type->isPointerTy()) return &ffi_type_pointer;
    if (type->isIntegerTy(1)) return &ffi_type_uint8;
    if (type->isIntegerTy(32)) return &ffi_type_sint32;
    if (type->isIntegerTy(64)) return &ffi_type_sint64;
    if (type->isFloatTy()) return &ffi_type_float;
    if (type->isDoubleTy()) return &ffi_type_double;
    FAIL("Unproved direct K physical type (by-value aggregate inputs are deferred)");
    return nullptr;
}

struct NativeCall {
    std::string symbol;
    ffi_cif cif{};
    std::vector<ffi_type*> parameters;
    void (*entry)() = nullptr;
    std::size_t result_size = 0;
    std::size_t result_alignment = 0;

    NativeCall(llvm::Function& function, ffi_type* result,
               std::initializer_list<ffi_type*> expected,
               std::initializer_list<const char*> names, bool sret = false) {
        symbol = function.getName().str();
        INFO("Actual K body: " << symbol);
        REQUIRE_FALSE(function.isDeclaration());
        REQUIRE(function.getCallingConv() == llvm::CallingConv::C);
        REQUIRE_FALSE(function.isVarArg());
        REQUIRE_FALSE(function.hasFnAttribute(llvm::Attribute::Naked));
        REQUIRE(physical_type(function.getReturnType()) == result);
        REQUIRE(function.arg_size() == expected.size());
        REQUIRE(names.size() + (sret ? 1 : 0) == expected.size());
        for (auto& argument : function.args()) {
            parameters.push_back(physical_type(argument.getType()));
            REQUIRE_FALSE(function.hasParamAttribute(argument.getArgNo(),
                                                       llvm::Attribute::ByVal));
            REQUIRE(function.hasParamAttribute(argument.getArgNo(),
                                                llvm::Attribute::StructRet)
                    == (sret && argument.getArgNo() == 0));
        }
        REQUIRE(parameters == std::vector<ffi_type*>(expected));
        unsigned index = sret ? 1 : 0;
        for (const auto* name : names) {
            REQUIRE(function.getArg(index++)->getName() == name);
        }
        if (sret) {
            auto* type = function.getParamAttribute(0, llvm::Attribute::StructRet)
                                 .getValueAsType();
            REQUIRE(type != nullptr);
            REQUIRE(type->isStructTy());
            const auto& layout = function.getParent()->getDataLayout();
            result_size = layout.getTypeAllocSize(type).getFixedValue();
            result_alignment = layout.getABITypeAlign(type).value();
            REQUIRE(result_size > 0);
        }
        REQUIRE(ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
                            static_cast<unsigned>(parameters.size()), result,
                            parameters.data()) == FFI_OK);
    }

    void bind(k::model::gen::jit& jit) {
        REQUIRE(symbol.starts_with("_K"));
        entry = jit.lookup_symbol<void (*)()>(symbol);
        REQUIRE(entry != nullptr);
        REQUIRE(entry == jit.lookup_symbol<void (*)()>(symbol));
    }
};

struct CallFrame {
    NativeCall& call;
    void* result;
    std::vector<void*> slots;
    unsigned destroyed = 0;
    bool returned = false;

    CallFrame(NativeCall& call, void* result, std::vector<void*> slots)
        : call(call), result(result), slots(std::move(slots)) {
        REQUIRE(this->slots.size() == call.parameters.size());
    }
};

struct NativeCleanup {
    CallFrame& frame;
    ~NativeCleanup() { ++frame.destroyed; }
};

class Fixture {
    std::shared_ptr<k::compiler> compiler;
    std::map<std::string, std::unique_ptr<NativeCall>> calls;
    std::unique_ptr<k::model::gen::jit> jit;

    llvm::Function& body(const std::string& name) {
        auto* function = compiler->get_context_for_test()->module().getFunction(
            compiler->get_element_mangled_name(name));
        REQUIRE(function != nullptr);
        return *function;
    }

    void add(const std::string& name, ffi_type* result,
             std::initializer_list<ffi_type*> parameters,
             std::initializer_list<const char*> names, bool sret = false) {
        calls.emplace(name, std::make_unique<NativeCall>(body(name), result,
                                                        parameters, names, sret));
    }

public:
    Fixture() {
        compiler = compile_model_with_stdlib(source);
        REQUIRE(compiler != nullptr);
        auto& module = compiler->get_context_for_test()->module();
        require_supported_abi(module);

        add("scalar", &ffi_type_double,
            {&ffi_type_sint64, &ffi_type_double, &ffi_type_sint32, &ffi_type_float},
            {"a", "b", "c", "d"});
        add("Receiver::calculate", &ffi_type_sint64,
            {&ffi_type_pointer, &ffi_type_sint32}, {"this", "multiplier"});
        add("mixed", &ffi_type_double,
            {&ffi_type_sint64, &ffi_type_pointer, &ffi_type_double,
             &ffi_type_pointer, &ffi_type_pointer, &ffi_type_float},
            {"head", "input", "fraction", "output", "nullable", "tail"});
        add("makeResult", &ffi_type_void,
            {&ffi_type_pointer, &ffi_type_sint64, &ffi_type_pointer},
            {"seed", "input"}, true);
        add("Receiver::build", &ffi_type_void,
            {&ffi_type_pointer, &ffi_type_pointer, &ffi_type_sint64, &ffi_type_pointer},
            {"this", "seed", "input"}, true);
        add("Result::read", &ffi_type_sint64, {&ffi_type_pointer}, {"this"});
        auto result = find_aggregate(compiler, "Result");
        REQUIRE(result != nullptr);
        auto destructor = result->get_destructor();
        REQUIRE(destructor != nullptr);
        auto* destructor_body =
            compiler->get_context_for_test()->lookup_llvm_function(destructor);
        REQUIRE(destructor_body != nullptr);
        calls.emplace("destroyResult",
            std::make_unique<NativeCall>(*destructor_body, &ffi_type_void,
                std::initializer_list<ffi_type*>{&ffi_type_pointer},
                std::initializer_list<const char*>{"this"}));
        add("makeToken", &ffi_type_pointer, {&ffi_type_sint32}, {"value"});
        add("consume", &ffi_type_sint32,
            {&ffi_type_pointer, &ffi_type_sint32}, {"token", "code"});
        add("drain", &ffi_type_sint32, {&ffi_type_pointer}, {"value"});
        add("Base::score", &ffi_type_sint64,
            {&ffi_type_pointer, &ffi_type_sint32}, {"this", "delta"});
        add("Derived::score", &ffi_type_sint64,
            {&ffi_type_pointer, &ffi_type_sint32}, {"this", "delta"});
        for (const char* name : {"receiverAddress", "borrowedAddress", "drainAddress",
                                  "baseAddress", "derivedAddress"}) {
            add(name, &ffi_type_pointer, {}, {});
        }
        for (const char* name : {"drops", "sum", "results", "scopes", "entered"}) {
            add(name, &ffi_type_sint32, {}, {});
        }
        add("drainIsEmpty", &ffi_type_uint8, {}, {});
        for (const char* name : {"catchDriver", "baseCatchDriver"}) {
            add(name, &ffi_type_sint32,
                {&ffi_type_pointer, &ffi_type_sint32}, {"frame", "expected"});
            bool invokes_executor = false;
            bool has_landing_pad = false;
            for (auto& block : body(name)) {
                for (auto& instruction : block) {
                    has_landing_pad |= llvm::isa<llvm::LandingPadInst>(instruction);
                    if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction)) {
                        auto* callee = invoke->getCalledFunction();
                        invokes_executor |= callee &&
                            callee->getName() == "klang_k_ffi_abi_execute";
                    }
                }
            }
            REQUIRE(invokes_executor);
            REQUIRE(has_landing_pad);
        }
        REQUIRE_FALSE(body("consume").hasFnAttribute(llvm::Attribute::NoUnwind));
        REQUIRE(module.getFunction("__k_thrown_typeinfo_chain_addr") != nullptr);
        REQUIRE(module.getFunction("__k_thrown_typeinfo_addr") != nullptr);

        // These targets have no source callers. Any compiler-emitted adapter
        // forwarding to them would violate that fact. Ordinary virtual thunks
        // are not selected: virtual entries above come from their real bodies.
        for (const char* name : {"scalar", "Receiver::calculate", "mixed",
                                  "makeResult", "Receiver::build", "consume", "drain"}) {
            auto* target = &body(name);
            for (auto& function : module) {
                for (auto& block : function) {
                    for (auto& instruction : block) {
                        auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                        REQUIRE_FALSE((call && call->getCalledOperand()->stripPointerCasts()
                                                   == target));
                    }
                }
            }
        }
        for (const char* name : {"ffi_call", "ffi_closure_alloc", "ffi_prep_closure_loc"}) {
            REQUIRE(module.getFunction(name) == nullptr);
        }
        jit = compiler->to_jit();
        REQUIRE(jit != nullptr);
        for (auto& [name, call] : calls) call->bind(*jit);
    }

    NativeCall& operator[](const std::string& name) { return *calls.at(name); }

    template<class T>
    T get(const std::string& name) {
        // libffi may write ffi_arg bytes for a narrow integral return.
        if constexpr (std::is_integral_v<T>) {
            ffi_arg result = 0;
            CallFrame frame((*this)[name], &result, {});
            execute(frame);
            return static_cast<T>(result);
        } else {
            T result{};
            CallFrame frame((*this)[name], &result, {});
            execute(frame);
            return result;
        }
    }

    static void execute(CallFrame& frame);
};

struct ResultStorage {
    std::size_t alignment;
    void* address;

    explicit ResultStorage(const NativeCall& call)
        : alignment(call.result_alignment),
          address(::operator new(call.result_size, std::align_val_t(alignment))) {}
    ~ResultStorage() { ::operator delete(address, std::align_val_t(alignment)); }
};

} // namespace

// The ONLY shared executable invocation path. It accepts data, not a selected
// target's static type; no typed call, closure, JIT stub or per-target adapter.
extern "C" void klang_k_ffi_abi_execute(void* opaque) {
    auto& frame = *static_cast<CallFrame*>(opaque);
    NativeCleanup cleanup{frame};
    ffi_call(&frame.call.cif, frame.call.entry, frame.result, frame.slots.data());
    frame.returned = true;
}

void Fixture::execute(CallFrame& frame) {
    klang_k_ffi_abi_execute(&frame);
    REQUIRE(frame.returned);
    REQUIRE(frame.destroyed == 1);
}

TEST_CASE("Direct K FFI: real scalar and receiver entries preserve addresser slots",
          "[libk][ffi][k-abi][direct]") {
    Fixture fixture;
    int64_t a = 20;
    double b = 1.25;
    int32_t c = -4;
    float d = 0.5f;
    double result = 0;
    CallFrame scalar(fixture["scalar"], &result, {&a, &b, &c, &d});
    Fixture::execute(scalar);
    CHECK(result == 19.0);
    CHECK(fixture.get<int32_t>("entered") == 1);

    void* receiver = fixture.get<void*>("receiverAddress");
    REQUIRE(receiver != nullptr);
    int32_t multiplier = 3;
    int64_t member_result = 0;
    CallFrame member(fixture["Receiver::calculate"], &member_result,
                     {&receiver, &multiplier});
    Fixture::execute(member);
    CHECK(member_result == 123);
    CHECK(fixture.get<int32_t>("entered") == 2);

    void* borrowed = fixture.get<void*>("borrowedAddress");
    REQUIRE(borrowed != nullptr);
    void* nullable = borrowed;
    int32_t guards[] = {111, 0, 222};
    int32_t* output = &guards[1];
    CallFrame mixed(fixture["mixed"], &result,
                    {&a, &borrowed, &b, &output, &nullable, &d});
    REQUIRE(mixed.slots[1] == &borrowed);
    REQUIRE(mixed.slots[3] == &output);
    REQUIRE(*static_cast<void**>(mixed.slots[1]) == borrowed);
    Fixture::execute(mixed);
    CHECK(result == 37.0);
    CHECK(guards[0] == 111);
    CHECK(guards[1] == 34);
    CHECK(guards[2] == 222);
    CHECK(fixture.get<int32_t>("entered") == 3);

    nullable = nullptr;
    CallFrame null_view(fixture["mixed"], &result,
                        {&a, &borrowed, &b, &output, &nullable, &d});
    Fixture::execute(null_view);
    CHECK(result == 30.0);
    CHECK(guards[1] == 27);
}

TEST_CASE("Direct K FFI: compiler sret precedes receiver and owns real K result fields",
          "[libk][ffi][k-abi][sret][lifecycle]") {
    Fixture fixture;
    void* borrowed = fixture.get<void*>("borrowedAddress");
    void* receiver = fixture.get<void*>("receiverAddress");
    int64_t seed = 5;
    const auto drops = fixture.get<int32_t>("drops");
    const auto sum = fixture.get<int32_t>("sum");
    const auto results = fixture.get<int32_t>("results");

    for (bool member : {false, true}) {
        auto& call = fixture[member ? "Receiver::build" : "makeResult"];
        ResultStorage storage(call);
        void* destination = storage.address;
        CallFrame frame(call, nullptr,
                        member ? std::vector<void*>{&destination, &receiver, &seed, &borrowed}
                               : std::vector<void*>{&destination, &seed, &borrowed});
        Fixture::execute(frame);
        CHECK(fixture.get<int32_t>("entered") == (member ? 5 : 4));
        int64_t value = 0;
        CallFrame read(fixture["Result::read"], &value, {&destination});
        Fixture::execute(read);
        CHECK(value == (member ? 71 : 29));
        CHECK(fixture.get<int32_t>("drops") == drops + (member ? 1 : 0));
        CallFrame destroy(fixture["destroyResult"], nullptr, {&destination});
        Fixture::execute(destroy);
    }
    CHECK(fixture.get<int32_t>("drops") == drops + 2);
    CHECK(fixture.get<int32_t>("sum") == sum + 36);
    CHECK(fixture.get<int32_t>("results") == results + 2);
}

TEST_CASE("Direct K FFI: owner handoff and aggregate drain clean up exactly once",
          "[libk][ffi][k-abi][owner][drain][lifecycle]") {
    Fixture fixture;
    int32_t token_value = 53;
    void* source_owner = nullptr;
    CallFrame create(fixture["makeToken"], &source_owner, {&token_value});
    Fixture::execute(create);
    REQUIRE(source_owner != nullptr);
    const auto drops = fixture.get<int32_t>("drops");
    const auto sum = fixture.get<int32_t>("sum");

    // S0 stages the physical owner transfer explicitly; original-binding
    // capture/authorization belongs to Argument, not this ABI feasibility test.
    void* incoming = std::exchange(source_owner, nullptr);
    REQUIRE(source_owner == nullptr);
    REQUIRE(fixture.get<int32_t>("drops") == drops);
    int32_t code = 0;
    ffi_arg result = 0;
    CallFrame consume(fixture["consume"], &result, {&incoming, &code});
    Fixture::execute(consume);
    CHECK(static_cast<int32_t>(result) == 53);
    CHECK(source_owner == nullptr);
    CHECK(fixture.get<int32_t>("drops") == drops + 1);
    CHECK(fixture.get<int32_t>("sum") == sum + 53);
    CHECK(fixture.get<int32_t>("scopes") == 1);

    void* box = fixture.get<void*>("drainAddress");
    REQUIRE_FALSE(fixture.get<uint8_t>("drainIsEmpty"));
    CallFrame drain(fixture["drain"], &result, {&box});
    Fixture::execute(drain);
    CHECK(static_cast<int32_t>(result) == 61);
    CHECK(fixture.get<uint8_t>("drainIsEmpty"));
    CHECK(fixture.get<int32_t>("entered") == 7);
    CHECK(fixture.get<int32_t>("drops") == drops + 2);
    CHECK(fixture.get<int32_t>("sum") == sum + 114);
}

TEST_CASE("Direct K FFI: K typed catches unwind libffi and both native and K guards",
          "[libk][ffi][k-abi][exception][unwind][owner]") {
    Fixture fixture;
    for (const char* driver : {"catchDriver", "baseCatchDriver", "catchDriver"}) {
        CAPTURE(driver);
        const auto drops = fixture.get<int32_t>("drops");
        const auto sum = fixture.get<int32_t>("sum");
        const auto scopes = fixture.get<int32_t>("scopes");
        int32_t token_value = 73;
        void* source_owner = nullptr;
        CallFrame create(fixture["makeToken"], &source_owner, {&token_value});
        Fixture::execute(create);
        REQUIRE(source_owner != nullptr);
        void* incoming = std::exchange(source_owner, nullptr);
        REQUIRE(source_owner == nullptr);
        int32_t code = 731;
        ffi_arg unreachable_result = 0x5a5a;
        CallFrame target(fixture["consume"], &unreachable_result, {&incoming, &code});
        void* opaque = &target;
        ffi_arg caught = 0;
        CallFrame caller(fixture[driver], &caught, {&opaque, &code});
        Fixture::execute(caller);
        CHECK(static_cast<int32_t>(caught) == code);
        CHECK_FALSE(target.returned);
        CHECK(unreachable_result == 0x5a5a);
        CHECK(target.destroyed == 1);
        CHECK(source_owner == nullptr);
        CHECK(fixture.get<int32_t>("entered") == 6);
        CHECK(fixture.get<int32_t>("drops") == drops + 1);
        CHECK(fixture.get<int32_t>("sum") == sum + 73);
        CHECK(fixture.get<int32_t>("scopes") == scopes + 2);
    }
}

TEST_CASE("Direct K FFI: adjusted receiver enters concrete override not virtual thunk",
          "[libk][ffi][k-abi][virtual][receiver]") {
    Fixture fixture;
    void* base = fixture.get<void*>("baseAddress");
    void* complete = fixture.get<void*>("derivedAddress");
    REQUIRE(base != nullptr);
    REQUIRE(complete != nullptr);
    REQUIRE(base != complete);
    // A normal compiler-generated upcast is the S0 projection oracle. Publishing
    // and resolving this adjustment through RTTI is a later metadata step.
    auto adjustment = reinterpret_cast<std::intptr_t>(complete)
                    - reinterpret_cast<std::intptr_t>(base);
    void* adjusted = static_cast<unsigned char*>(base) + adjustment;
    REQUIRE(adjusted == complete);
    int32_t delta = 5;
    int64_t result = 0;
    CallFrame base_body(fixture["Base::score"], &result, {&base, &delta});
    Fixture::execute(base_body);
    CHECK(result == 16);
    CHECK(fixture.get<int32_t>("entered") == 8);
    CallFrame override_body(fixture["Derived::score"], &result, {&adjusted, &delta});
    Fixture::execute(override_body);
    CHECK(result == 1105);
    CHECK(fixture.get<int32_t>("entered") == 9);
    CHECK(fixture["Base::score"].entry != fixture["Derived::score"].entry);
}
