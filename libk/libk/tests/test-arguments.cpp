/*
 * Borrowed Argument source-slot, capability and lifetime regressions.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
#include "gen/gen_arguments.hpp"
#include "model/statements.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace {

auto jit_k(std::string_view source) {
    auto compiler = compile_model_with_stdlib(source);
    REQUIRE(compiler);
    REQUIRE_FALSE(llvm::verifyModule(compiler->get_context_for_test()->module(), &llvm::errs()));
    return compiler->to_jit();
}

int run(k::model::gen::jit& jit, const char* name = "test") {
    auto function = jit.lookup_symbol<int(*)()>(name);
    REQUIRE(function != nullptr);
    return function();
}

void rejects(const std::string& source, const std::string& diagnostic) {
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("argument-negative.k", source, false, false);
        FAIL("Invalid Argument program compiled");
    } catch (const k::log::compiler_error& error) {
        CHECK(std::string(error.what()).find(diagnostic) != std::string::npos);
    }
}

} // namespace

TEST_CASE("Argument: exact value and addresser metadata", "[libk][argument][metadata]") {
    auto jit = jit_k(R"K(
        module argument_metadata;
        referenceOf(value : int&) : int& { return value; }
        parameterSlot(value : int&) : int {
            holder : Argument = Argument::capture(value);
            if (holder.getLogicalType()->getKind() != rtti::TypeKind::REFERENCE) return 1;
            if (!holder.canReadSlot() || holder.isSlotWritable()) return 2;
            *holder.get<int>() = 28;
            return 0;
        }
        readonlyMetadata() : int {
            x : const int = 3;
            r : const int& = x;
            v : const int? = &x;
            l : const int+ = &x;
            p : const int* = &x;
            o : const int! = new int(4);
            holders : Argument[5]{
                Argument::capture(r), Argument::capture(v), Argument::capture(l),
                Argument::capture(p), Argument::capture(o)
            };
            i : unsigned int = 0u;
            while (i < 5u) {
                if (holders[i].canMutate()) return 1;
                if (holders[i].getReferentType()->getKind() != rtti::TypeKind::CONST) return 2;
                if (holders[i].get<const int>() == null) return 3;
                i += 1u;
            }
            if (!holders[2].isSlotWritable() || !holders[3].isSlotWritable()) return 4;
            if (!holders[4].canTakeOwner() || holders[0].canTakeOwner()) return 5;
            return 0;
        }
        test() : int {
            x : int = 0;
            r : int& = x;
            v : int? = &x;
            l : int+ = &x;
            p : int* = &x;
            o : int! = new int(9);
            d : int# = #x;
            value : Argument = Argument::capture(x);
            reference : Argument = Argument::capture(r);
            view : Argument = Argument::capture(v);
            link : Argument = Argument::capture(l);
            pointer : Argument = Argument::capture(p);
            owner : Argument = Argument::capture(o);
            drain : Argument = Argument::capture(d);
            returned : Argument = Argument::capture(referenceOf(x));
            constValue : const int = 3;
            constPointer : const int* = &constValue;
            constant : Argument = Argument::capture(constPointer);
            if (value.getCategory() != ArgumentCategory::VALUE_LVALUE) return 1;
            if (value.getLogicalType()->getKind() != rtti::TypeKind::PRIMITIVE) return 2;
            if (value.getLogicalType()->getPrimitiveKind() != rtti::PrimitiveKind::INT) return 3;
            if (value.isNull() || value.read<int>() != 0) return 4;
            if (reference.getLogicalType()->getKind() != rtti::TypeKind::REFERENCE) return 5;
            if (view.getLogicalType()->getKind() != rtti::TypeKind::VIEW) return 6;
            if (link.getLogicalType()->getKind() != rtti::TypeKind::LINK) return 7;
            if (pointer.getLogicalType()->getKind() != rtti::TypeKind::POINTER) return 8;
            if (owner.getLogicalType()->getKind() != rtti::TypeKind::OWNER) return 9;
            if (drain.getLogicalType()->getKind() != rtti::TypeKind::DRAIN) return 10;
            if (!reference.canReadSlot() || reference.isSlotWritable()) return 11;
            if (!pointer.canReadSlot() || !pointer.isSlotWritable()) return 12;
            if (!owner.canTakeOwner() || o == null || owner.read<int>() != 9) return 13;
            if (!drain.canDrain() || drain.canTakeOwner()) return 14;
            *value.get<int>() = 17;
            if (reference.read<int>() != 17 || pointer.read<int>() != 17) return 15;
            if (returned.getCategory() != ArgumentCategory::REFERENCE || returned.canReadSlot()) return 16;
            if (returned.read<int>() != 17) return 17;
            if (constant.getReferentType()->getKind() != rtti::TypeKind::CONST) return 18;
            if (constant.canMutate() || constant.get<const int>() == null) return 19;
            try { constant.get<int>(); return 20; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::CONST_VIOLATION) return 21;
            }
            if (constant.read<int>() != 3) return 22;
            if (parameterSlot(x) != 0 || x != 28) return 23;
            return 0;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 0);
    CHECK(run(*jit, "readonlyMetadata") == 0);
}

TEST_CASE("Argument: declaration const preserves pointee qualifiers and writable slots",
          "[libk][argument][const][declaration-const][regression]") {
    auto jit = jit_k(R"K(
        module argument_declaration_const;
        struct Sources {
            public pointer : const int* = null;
            public owner : const int! = null;
        }
        readonlyField(box : const Sources&) : Argument {
            return Argument::capture(box.pointer);
        }
        pointerMetadata() : int {
            first : int = 1;
            second : int = 9;
            const pointer : int* = &first;
            holder : Argument = Argument::capture(pointer);
            copied : Argument = holder;
            if (holder.canMutate()) return -1;
            if (!holder.isSlotWritable()) return -2;
            if (holder.getLogicalType()->getKind() != rtti::TypeKind::POINTER) return -3;
            if (holder.getBindingType()->getKind() != rtti::TypeKind::POINTER) return -4;
            if (holder.getReferentType()->getKind() != rtti::TypeKind::CONST) return -5;
            if (!holder.canRead() || !holder.canReadSlot() || holder.canTakeOwner()) return -6;
            if (holder.get<const int>() == null || holder.read<int>() != 1) return -7;
            pointer = &second;
            if (holder.read<int>() != 9 || copied.read<int>() != 9) return -8;
            pointer = null;
            if (!holder.isNull() || !copied.isNull()) return -9;
            pointer = &first;
            if (holder.read<int>() != 1 || first != 1 || second != 9) return -10;
            return 0;
        }
        rejectedMutation() : int {
            value : int = 1;
            const pointer : int* = &value;
            holder : Argument = Argument::capture(pointer);
            try { *holder.get<int>() = 2; return -11; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::CONST_VIOLATION) return -12;
            }
            if (value != 1 || holder.read<int>() != 1) return -13;
            return 1;
        }
        fieldAndOwnerControls() : int {
            value : int = 4;
            box : Sources;
            box.pointer = &value;
            box.owner = new int(8);
            pointer : Argument = Argument::capture(box.pointer);
            owner : Argument = Argument::capture(box.owner);
            readonly : Argument = readonlyField(box);
            const scalar : int = 6;
            scalarHolder : Argument = Argument::capture(scalar);
            if (pointer.canMutate() || !pointer.isSlotWritable()) return -14;
            if (pointer.getReferentType()->getKind() != rtti::TypeKind::CONST) return -15;
            if (pointer.get<const int>() == null || pointer.read<int>() != 4) return -16;
            if (readonly.canMutate() || readonly.isSlotWritable()) return -17;
            if (readonly.read<int>() != 4 || box.pointer == null) return -18;
            if (scalarHolder.canMutate() || scalarHolder.isSlotWritable()) return -19;
            if (scalarHolder.getReferentType()->getKind() != rtti::TypeKind::CONST) return -20;
            if (scalarHolder.read<int>() != 6) return -21;
            if (owner.canMutate() || !owner.isSlotWritable() || !owner.canTakeOwner()) return -22;
            if (owner.getLogicalType()->getKind() != rtti::TypeKind::OWNER) return -23;
            if (owner.getReferentType()->getKind() != rtti::TypeKind::CONST) return -24;
            try { owner.takeOwner<int>(); return -25; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::CONST_VIOLATION) return -26;
            }
            if (box.owner == null || owner.isNull()) return -27;
            taken : const int! = owner.takeOwner<const int>();
            if (box.owner != null || !owner.isNull()) return -28;
            if (taken == null || *taken != 8) return -29;
            return 0;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit, "pointerMetadata") == 0);
    CHECK(run(*jit, "rejectedMutation") == 1);
    CHECK(run(*jit, "fieldAndOwnerControls") == 0);
}

TEST_CASE("Argument: reference-valued holder fields use the referenced receiver",
          "[libk][argument][receiver][reference-field][regression]") {
    auto compiler = compile_model_with_stdlib(R"K(
        module argument_reference_fields;
        struct Wrapper {
            public holder : Argument&;
            Wrapper(value : Argument&) : holder(value) {}
        }
        normalReceiver() : int {
            value : int = 7;
            holder : Argument = Argument::capture(value);
            reference : Argument& = holder;
            return reference.read<int>();
        }
        fieldRead() : int {
            value : int = 7;
            holder : Argument = Argument::capture(value);
            wrapper : Wrapper(holder);
            return wrapper.holder.read<int>();
        }
        fieldGet() : int {
            first : int = 7;
            second : int = 9;
            pointer : int* = &first;
            holder : Argument = Argument::capture(pointer);
            wrapper : Wrapper(holder);
            *wrapper.holder.get<int>() = 8;
            pointer = &second;
            *wrapper.holder.get<int>() = 10;
            if (wrapper.holder.read<int>() != 10 || first != 8) return -1;
            return first + second;
        }
        fieldIsNull() : int {
            value : int = 12;
            pointer : int* = null;
            holder : Argument = Argument::capture(pointer);
            wrapper : Wrapper(holder);
            if (!wrapper.holder.isNull()) return -2;
            pointer = &value;
            if (wrapper.holder.isNull()) return -3;
            return wrapper.holder.read<int>();
        }
        fieldTake() : int {
            source : int! = new int(31);
            holder : Argument = Argument::capture(source);
            copied : Argument = holder;
            wrapper : Wrapper(holder);
            copyWrapper : Wrapper(copied);
            taken : int! = wrapper.holder.takeOwner<int>();
            if (source != null || taken == null || *taken != 31) return -4;
            if (!holder.isNull() || !copied.isNull() || !wrapper.holder.isNull()) return -5;
            empty : int! = wrapper.holder.takeOwner<int>();
            if (empty != null || source != null) return -6;
            source = new int(32);
            if (wrapper.holder.isNull() || copyWrapper.holder.read<int>() != 32) return -7;
            again : int! = wrapper.holder.take<int!>();
            if (source != null || again == null || *again != 32) return -8;
            if (!holder.isNull() || !copied.isNull() || !wrapper.holder.isNull()) return -9;
            return *taken + *again;
        }
    )K");
    REQUIRE(compiler);
    auto& module = compiler->get_context_for_test()->module();
    REQUIRE_FALSE(llvm::verifyModule(module, &llvm::errs()));
    // Reject a reference-slot receiver before JIT can interpret unrelated stack
    // memory as Argument metadata. These intrinsics must load the field first.
    for (const char* probe : {"fieldRead", "fieldGet", "fieldTake"}) {
        CAPTURE(probe);
        bool checked_receiver = false;
        for (const auto& function : module) {
            if (!function.getName().contains(probe)) continue;
            for (const auto& block : function) {
                for (const auto& instruction : block) {
                    auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                    if (!call) continue;
                    auto* callee = call->getCalledFunction();
                    if (!callee || !callee->getName().contains("_check")
                        || callee->getName().contains("_checkAddress")) continue;
                    checked_receiver = true;
                    REQUIRE(llvm::isa<llvm::LoadInst>(call->getArgOperand(0)));
                }
            }
        }
        REQUIRE(checked_receiver);
    }
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    CHECK(run(*jit, "normalReceiver") == 7);
    CHECK(run(*jit, "fieldRead") == 7);
    CHECK(run(*jit, "fieldGet") == 18);
    CHECK(run(*jit, "fieldIsNull") == 12);
    CHECK(run(*jit, "fieldTake") == 63);
}

TEST_CASE("Argument: null unbound readonly and exact typed errors", "[libk][argument][checks]") {
    auto jit = jit_k(R"K(
        module argument_checks;
        union Choice { number : int; }
        test() : int {
            x : int = 0;
            p : int* = null;
            absent : Argument;
            nullable : Argument = Argument::capture(p);
            readonly : Argument = Argument::borrow(x);
            copied : Argument = readonly;
            text : String("borrowed");
            imported : Argument = Argument::capture(text);
            values : int[2]{3, 4};
            array : Argument = Argument::capture(values);
            choice : Choice;
            choice.number = 16;
            unionValue : Argument = Argument::capture(choice);
            if (unionValue.read<Choice>().number != 16 || unionValue.canTakeOwner()) return 9;
            if (array.getReferentType()->getExtent() != (unsigned long) 2) return 5;
            if (array.read<int[2]>()[1] != 4) return 6;
            try { array.read<int[3]>(); return 7; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::TYPE_MISMATCH) return 8;
            }
            if (absent.isBound() || !nullable.isBound() || !nullable.isNull()) return 1;
            if (nullable.get<int>() != null || readonly.canMutate() || copied.canDrain()) return 2;
            if (copied.read<int>() != 0 || copied.get<const int>() == null) return 3;
            caught : int = 0;
            try { absent.read<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::UNBOUND) caught += 1;
            }
            try { nullable.read<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::NULL_NOT_ALLOWED) caught += 2;
            }
            try { copied.get<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::CONST_VIOLATION) caught += 4;
            }
            try { readonly.read<long>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 8;
            }
            try { readonly.read<unsigned int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 16;
            }
            p = &x;
            try { nullable.takeOwner<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::OWNER_TAKE_FORBIDDEN) caught += 32;
            }
            if (p == null || x != 0) return 4;
            try { absent.isNull(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::UNBOUND) caught += 64;
            }
            try { imported.read<String>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::UNTRUSTED_TYPE) caught += 128;
            }
            return caught;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 255);
}

TEST_CASE("Argument: local field and array pointer slots remain original", "[libk][argument][slots]") {
    auto jit = jit_k(R"K(
        module argument_slots;
        struct Box { public p : int* = null; }
        struct LinkBox {
            public value : int+;
            LinkBox(p : int+) : value(p) {}
        }
        calls : int = 0;
        index() : unsigned int { calls += 1; return 1u; }
        count(args : ArgumentPack) : unsigned int { return args.size; }
        test() : int {
            x : int = 11;
            y : int = 22;
            p : int* = &x;
            q : int* = &y;
            link : int+ = &x;
            box : Box;
            box.p = &x;
            pointers : int*[2]{&x, &y};
            links : int+[2]{&x, &y};
            linkBox : LinkBox(&x);
            local : Argument = Argument::capture(p);
            referent : Argument = Argument::capture(*p);
            shared : Argument = local;
            linked : Argument = Argument::capture(link);
            field : Argument = Argument::capture(box.p);
            element : Argument = Argument::capture(pointers[index()]);
            linkField : Argument = Argument::capture(linkBox.value);
            linkElement : Argument = Argument::capture(links[1]);
            pack : Argument[2]{local, shared};
            defaults : Argument[2];
            if (defaults[0].isBound() || defaults[1].isBound()) return 1;
            if (count(pack) != 2u || calls != 1 || element.read<int>() != 22) return 2;
            p = &y;
            link = &y;
            box.p = &y;
            pointers[1] = &x;
            linkBox.value = &y;
            links[1] = &x;
            if (local.read<int>() != 22 || linked.read<int>() != 22) return 3;
            if (field.read<int>() != 22 || element.read<int>() != 11) return 4;
            pack[0] = Argument::capture(q);
            q = &x;
            p = null;
            if (!local.isNull() || !shared.isNull() || !pack[1].isNull()) return 5;
            if (pack[0].read<int>() != 11 || q == null || box.p == null) return 6;
            if (pointers[0] == null || *pointers[0] != 11 || calls != 1) return 7;
            if (referent.read<int>() != 11 || referent.canTakeOwner()) return 8;
            if (linkField.read<int>() != 22 || linkElement.read<int>() != 11) return 9;
            return 0;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 0);
}

TEST_CASE("Argument: owner aliases take and reassignment have exact cleanup", "[libk][argument][owner]") {
    auto jit = jit_k(R"K(
        module argument_owners;
        destroyed : int = 0;
        copied : int = 0;
        struct Resource {
            public value : int;
            Resource(n : int) : value(n) {}
            Resource(other : const Resource&) : value(other.value) { copied += 1; }
            ~Resource() { destroyed += 1; }
        }
        struct Box { public owner : Resource! = null; }
        test() : int {
            source : Resource! = new Resource(10);
            holder : Argument = Argument::capture(source);
            shared : Argument = holder;
            pack : Argument[2]{holder, shared};
            if (source == null || holder.read<Resource>().value != 10) return 1;
            try { holder.takeOwner<int>(); return 8; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::TYPE_MISMATCH) return 9;
            }
            if (source == null || shared.isNull() || pack[0].isNull()
                || source.value != 10 || destroyed != 0) return 10;
            holderPointer : Argument* = &shared;
            ownedHolder : Argument! = new Argument();
            *ownedHolder = holder;
            if (holderPointer->read<Resource>().value != 10
                || ownedHolder->read<Resource>().value != 10) return 11;
            first : Resource! = holderPointer->takeOwner<Resource>();
            if (source != null || first == null || !holder.isNull() || !pack[1].isNull()) return 2;
            empty : Resource! = ownedHolder->takeOwner<Resource>();
            if (empty != null || first.value != 10) return 3;
            source = new Resource(20);
            if (shared.read<Resource>().value != 20 || pack[0].isNull()) return 4;
            second : Resource! = pack[0].takeOwner<Resource>();
            if (source != null || second.value != 20 || !shared.isNull()) return 5;
            box : Box;
            box.owner = new Resource(30);
            boxPointer : Box* = &box;
            fields : Argument = Argument::capture(boxPointer->owner);
            fieldTake : Resource! = fields.takeOwner<Resource>();
            if (box.owner != null || fieldTake.value != 30) return 6;
            owners : Resource![2];
            owners[0] = new Resource(40);
            owners[1] = new Resource(50);
            element : Argument = Argument::capture(owners[1]);
            arrayTake : Resource! = element.takeOwner<Resource>();
            if (owners[1] != null || owners[0] == null || arrayTake.value != 50) return 7;
            return 0;
        }
        destructionCount() : int { return destroyed; }
        copyCount() : int { return copied; }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 0);
    CHECK(run(*jit, "destructionCount") == 5);
    CHECK(run(*jit, "copyCount") == 0);
}

TEST_CASE("Argument: const referent transfer differs from readonly owner slot", "[libk][argument][const]") {
    auto jit = jit_k(R"K(
        module argument_const;
        made : int = 0;
        destroyed : int = 0;
        phase : int = 0;
        sourceBefore : int = -999;
        sourceCleared : int = -1;
        takenPresent : int = -1;
        takenValue : int = -999;
        madeAtTransfer : int = -1;
        destroyedAtTransfer : int = -1;
        boxPresent : int = -1;
        struct Resource {
            public value : int = 8;
            Resource() { made += 1; }
            ~Resource() { destroyed += 1; }
        }
        struct Box { public owner : Resource! = null; }
        resetCounters() {
            made = 0;
            destroyed = 0;
            phase = 0;
            sourceBefore = -999;
            sourceCleared = -1;
            takenPresent = -1;
            takenValue = -999;
            madeAtTransfer = -1;
            destroyedAtTransfer = -1;
            boxPresent = -1;
        }
        inspect(box : const Box&) : Argument { return Argument::capture(box.owner); }
        inspectPointer(box : const Box*) : Argument { return Argument::capture(box->owner); }
        test() : int {
            resetCounters();
            source : const Resource! = new Resource();
            phase = 1;
            holder : Argument = Argument::capture(source);
            if (!holder.canTakeOwner()) return -1;
            if (!holder.isSlotWritable()) return -2;
            if (holder.canMutate()) return -3;
            if (holder.getReferentType()->getKind() != rtti::TypeKind::CONST) return -4;
            try { holder.get<Resource>(); return -5; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::CONST_VIOLATION) return -6;
            }
            try { holder.takeOwner<Resource>(); return -7; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::CONST_VIOLATION) return -8;
            }
            if (source == null) return -9;
            if (holder.isNull()) return -10;
            if (destroyed != 0) return -11;
            sourceBefore = source.value;
            phase = 2;
            taken : const Resource! = holder.takeOwner<const Resource>();
            sourceCleared = 0;
            if (source == null) sourceCleared = 1;
            takenPresent = 0;
            if (taken != null) {
                takenPresent = 1;
                takenValue = taken.value;
            }
            madeAtTransfer = made;
            destroyedAtTransfer = destroyed;
            phase = 3;
            if (source != null) return -12;
            if (taken == null) return -13;
            if (taken.value != 8) return -14;
            box : Box;
            box.owner = new Resource();
            boxPresent = 0;
            if (box.owner != null) boxPresent = 1;
            phase = 4;
            readonly : Argument = inspect(box);
            readonlyArrow : Argument = inspectPointer(&box);
            if (readonly.canTakeOwner()) return -15;
            if (readonly.isSlotWritable()) return -16;
            if (readonlyArrow.canTakeOwner()) return -17;
            if (readonlyArrow.isSlotWritable()) return -18;
            if (readonly.getBindingType()->getKind() != rtti::TypeKind::CONST) return -19;
            denied : int = 0;
            try { readonly.takeOwner<Resource>(); return -20; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::OWNER_TAKE_FORBIDDEN) return -21;
                denied += 1;
            }
            try { readonlyArrow.takeOwner<Resource>(); return -22; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::OWNER_TAKE_FORBIDDEN) return -23;
            }
            narrowed : Argument = Argument::borrow(box.owner);
            try { narrowed.takeOwner<Resource>(); return -24; }
            catch (e : ArgumentError&) {
                if (e.getReason() != ArgumentErrorReason::OWNER_TAKE_FORBIDDEN) return -25;
                denied += 2;
            }
            boxPresent = 0;
            if (box.owner != null) boxPresent = 1;
            if (box.owner == null) return -26;
            phase = 5;
            return denied;
        }
        ordinaryControl() : int {
            resetCounters();
            {
                source : const Resource! = new Resource();
                phase = 1;
                if (source == null) return -101;
                sourceBefore = source.value;
                if (source.value != 8) return -102;
                phase = 2;
                taken : const Resource! = source;
                sourceCleared = 0;
                if (source == null) sourceCleared = 1;
                takenPresent = 0;
                if (taken != null) {
                    takenPresent = 1;
                    takenValue = taken.value;
                }
                madeAtTransfer = made;
                destroyedAtTransfer = destroyed;
                phase = 3;
                if (source != null) return -103;
                if (taken == null) return -104;
                if (taken.value != 8) return -105;
                box : Box;
                box.owner = new Resource();
                boxPresent = 0;
                if (box.owner != null) boxPresent = 1;
                phase = 4;
                if (box.owner == null) return -106;
                phase = 5;
            }
            phase = 6;
            return 0;
        }
        constructionCount() : int { return made; }
        destructionCount() : int { return destroyed; }
        reachedPhase() : int { return phase; }
        sourceValueBeforeTransfer() : int { return sourceBefore; }
        sourceClearedAfterTransfer() : int { return sourceCleared; }
        takenPresentAfterTransfer() : int { return takenPresent; }
        takenValueAfterTransfer() : int { return takenValue; }
        transferConstructionCount() : int { return madeAtTransfer; }
        transferDestructionCount() : int { return destroyedAtTransfer; }
        boxOwnerPresent() : int { return boxPresent; }
    )K");
    REQUIRE(jit);
    const auto checkObservations = [&](int result, int expectedResult, int expectedPhase) {
        const int constructions = run(*jit, "constructionCount");
        const int destructions = run(*jit, "destructionCount");
        const int phase = run(*jit, "reachedPhase");
        const int sourceBefore = run(*jit, "sourceValueBeforeTransfer");
        const int sourceCleared = run(*jit, "sourceClearedAfterTransfer");
        const int takenPresent = run(*jit, "takenPresentAfterTransfer");
        const int takenValue = run(*jit, "takenValueAfterTransfer");
        const int constructionsAtTransfer = run(*jit, "transferConstructionCount");
        const int destructionsAtTransfer = run(*jit, "transferDestructionCount");
        const int boxPresent = run(*jit, "boxOwnerPresent");
        CAPTURE(result, constructions, destructions, phase, sourceBefore, sourceCleared,
                takenPresent, takenValue, constructionsAtTransfer, destructionsAtTransfer, boxPresent);
        CHECK(result == expectedResult);
        CHECK(phase == expectedPhase);
        CHECK(constructions == 2);
        CHECK(destructions == 2);
        CHECK(sourceBefore == 8);
        CHECK(sourceCleared == 1);
        CHECK(takenPresent == 1);
        CHECK(takenValue == 8);
        CHECK(constructionsAtTransfer == 1);
        CHECK(destructionsAtTransfer == 0);
        CHECK(boxPresent == 1);
    };
    {
        INFO("Argument const-referent owner transfer");
        const int result = run(*jit);
        checkObservations(result, 3, 5);
    }
    {
        INFO("Argument-free ordinary const-referent owner transfer and Box cleanup");
        const int result = run(*jit, "ordinaryControl");
        checkObservations(result, 0, 6);
    }
}

TEST_CASE("Argument: exact nominal aliases and explicit drains", "[libk][argument][drain][nominal]") {
    auto jit = jit_k(R"K(
        module argument_nominal_drain;
        typedef Token : int;
        typedef Other : int;
        struct Move {
            public value : int;
            Move(n : int) : value(n) {}
            Move(other : Move#) : value(other.value) { other.value = 0; }
        }
        test() : int {
            token : Token = (Token) 6;
            holder : Argument = Argument::capture(token);
            if ((int) holder.read<Token>() != 6) return 1;
            if ((int) holder.read<const Token>() != 6) return 5;
            if (holder.getReferentType()->getKind() != rtti::TypeKind::ALIAS) return 2;
            caught : int = 0;
            try { holder.read<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 1;
            }
            try { holder.read<Other>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 2;
            }
            source : Move(19);
            drain : Argument = Argument::capture(#source);
            if (!drain.canDrain() || drain.canTakeOwner()) return 3;
            moved : Move(drain.drain<Move>());
            if (source.value != 0 || moved.value != 19) return 4;
            narrowed : Argument = Argument::borrow(#moved);
            try { narrowed.drain<Move>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::DRAIN_FORBIDDEN) caught += 4;
            }
            ordinary : Argument = Argument::capture(moved);
            try { ordinary.drain<Move>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::DRAIN_FORBIDDEN) caught += 8;
            }
            return caught;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 15);
}

TEST_CASE("Argument: strong addresser aliases preserve nominal ownership", "[libk][argument][owner][nominal]") {
    auto jit = jit_k(R"K(
        module argument_addresser_alias;
        typedef Handle : int!;
        typedef OtherHandle : int!;
        typedef Pointer : int*;
        resetOwner(slot : int!&) { slot = new int(23); }
        test() : int {
            x : int = 4;
            p : Pointer = (Pointer) &x;
            pointer : Argument = Argument::capture(p);
            source : Handle = (Handle) new int(12);
            holder : Argument = Argument::capture(source);
            copy : Argument = holder;
            if (holder.getLogicalType()->getKind() != rtti::TypeKind::ALIAS) return 1;
            if (!holder.canTakeOwner() || holder.read<int>() != 12) return 2;
            if (pointer.canTakeOwner() || pointer.read<int>() != 4) return 3;
            caught : int = 0;
            try { holder.takeOwner<int>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 1;
            }
            try { holder.take<OtherHandle>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::TYPE_MISMATCH) caught += 2;
            }
            try { pointer.take<Handle>(); }
            catch (e : ArgumentError&) {
                if (e.getReason() == ArgumentErrorReason::OWNER_TAKE_FORBIDDEN) caught += 4;
            }
            if (holder.isNull() || pointer.isNull() || caught != 7) return 4;
            taken : Handle = copy.take<Handle>();
            if (!holder.isNull() || !copy.isNull()) return 5;
            plain : int! = (int!) taken;
            if (plain == null || *plain != 12) return 6;
            resetOwner(source);
            if (copy.read<int>() != 23 || holder.isNull()
                || holder.getLogicalType()->getKind() != rtti::TypeKind::ALIAS) return 7;
            again : Handle = holder.take<Handle>();
            second : int! = (int!) again;
            if (second == null || *second != 23 || !copy.isNull()) return 8;
            return 0;
        }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit) == 0);
}

TEST_CASE("Argument: temporary holders and taken owner arguments unwind once", "[libk][argument][lifetime]") {
    auto jit = jit_k(R"K(
        module argument_lifetime;
        destroyed : int = 0;
        entered : int = 0;
        struct Resource {
            Resource() {}
            ~Resource() { destroyed += 1; }
        }
        class Stop : FatalError { public Stop() : FatalError(41) {} }
        fail() : int { throw Stop(); }
        consume(owner : Resource!, n : int) { entered += 1; }
        normal() : int {
            source : Resource! = new Resource();
            Argument::capture(source);
            if (source == null || destroyed != 0) return 1;
            consume(Argument::capture(source).takeOwner<Resource>(), 0);
            if (source != null || destroyed != 1 || entered != 1) return 2;
            return 0;
        }
        unwind() : int {
            source : Resource! = new Resource();
            shared : Argument = Argument::capture(source);
            try {
                consume(Argument::capture(source).takeOwner<Resource>(), fail());
            } catch (e : Stop&) {
                if (source != null || !shared.isNull()) return 1;
                if (destroyed != 2 || entered != 1 || e.getCode() != 41) return 2;
                return 0;
            }
            return 3;
        }
        unwindBorrow() : int {
            try {
                source : Resource! = new Resource();
                holder : Argument = Argument::capture(source);
                copy : Argument = holder;
                if (source == null || copy.isNull()) return 1;
                fail();
            } catch (e : Stop&) {
                if (destroyed != 3 || entered != 1) return 2;
                return 0;
            }
            return 3;
        }
        destructionCount() : int { return destroyed; }
    )K");
    REQUIRE(jit);
    CHECK(run(*jit, "normal") == 0);
    CHECK(run(*jit, "unwind") == 0);
    CHECK(run(*jit, "unwindBorrow") == 0);
    CHECK(run(*jit, "destructionCount") == 3);
}

TEST_CASE("Argument: known temporary and forged intrinsic captures diagnose", "[libk][argument][negative]") {
    for (const auto* expression : {"42", "new int(3)", "make()"}) {
        CAPTURE(expression);
        rejects(std::string(R"K(
            module argument_nonaddressable;
            make() : int! { return new int(7); }
            test() { holder : Argument = Argument::capture()K") + expression + "); }",
            "Argument capture requires");
    }
    rejects(R"K(
        module argument_temporary_field;
        struct Box { public p : int* = null; Box() {} }
        test() { holder : Argument = Argument::capture(Box().p); }
    )K", "Argument capture requires");
    rejects(R"K(
        module argument_bad_access;
        test() {
            source : int! = new int(3);
            holder : Argument = Argument::capture(source);
            holder.read<int!>();
        }
    )K", "Argument typed access requires");
    rejects(R"K(
        module argument_bad_const_access;
        test() {
            source : int! = new int(3);
            holder : Argument = Argument::capture(source);
            holder.read<const int!>();
        }
    )K", "Argument typed access requires");
    rejects(R"K(
        module argument_bad_exact_take;
        test() {
            source : int! = new int(3);
            holder : Argument = Argument::capture(source);
            holder.take<int>();
        }
    )K", "Argument take requires an exact logical owner type");
    rejects(R"K(
        module argument_adapted_capture;
        test() {
            source : int = 3;
            holder : Argument = Argument::capture<long>(source);
        }
    )K", "Argument capture requires");
    rejects(R"K(
        module argument_forgery;
        @k::annotations::Intrinsic("argument.take_owner.v1")
        fake() : int!;
        test() { value : int! = fake(); }
    )K", "Invalid or non-canonical Argument intrinsic");
    rejects(R"K(
        module argument_const_drain;
        test() {
            const x : int = 4;
            holder : Argument = Argument::capture(#x);
        }
    )K", "Cannot drain a const object");
    rejects(R"K(
        module argument_private_field;
        struct Box { private source : int! = null; }
        test() {
            box : Box;
            pointer : Box* = &box;
            holder : Argument = Argument::capture(pointer->source);
        }
    )K", "not accessible");
    rejects(R"K(
        module argument_const_adaptation;
        struct Box { public source : int! = null; }
        inspect(box : const Box&) {
            holder : Argument = Argument::capture<int!>(box.source);
        }
    )K", "Argument capture requires");
}

TEST_CASE("Argument: capture is inline metadata not an owner factory or invoker", "[libk][argument][ir]") {
    auto compiler = compile_model_with_stdlib(R"K(
        module argument_inline;
        test() : int {
            source : int! = new int(9);
            holder : Argument = Argument::capture(source);
            shared : Argument = holder;
            if (holder.read<int>() != 9) return 2;
            taken : int! = shared.takeOwner<int>();
            if (source != null || !holder.isNull()) return 1;
            return *taken;
        }
    )K");
    REQUIRE(compiler);
    const auto& module = compiler->get_context_for_test()->module();
    REQUIRE_FALSE(llvm::verifyModule(module, &llvm::errs()));
    unsigned declarations = 0;
    unsigned clears = 0;
    auto k_namespace = compiler->get_unit()->get_root_namespace()->get_child_namespace("k");
    REQUIRE(k_namespace);
    auto holder = k_namespace->get_aggregate("Argument");
    REQUIRE(holder);
    for (const auto& model : holder->functions()) {
        if (k::model::gen::get_argument_intrinsic(*model) == k::model::gen::argument_intrinsic::none) continue;
        if (model->is_template()) continue;
        auto* native = compiler->get_context_for_test()->lookup_llvm_function(model);
        if (!native) continue;
        REQUIRE(native);
        CHECK(native->isDeclaration());
        CHECK(native->hasExternalLinkage());
        CHECK_FALSE(native->hasComdat());
        CHECK(native->use_empty());
        ++declarations;
    }
    for (const auto& function : module) {
        for (const auto& block : function) {
            for (const auto& instruction : block) {
                if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
                    if (llvm::isa<llvm::ConstantPointerNull>(store->getValueOperand())) ++clears;
            }
        }
    }
    CHECK(declarations >= 3);
    CHECK(clears >= 1);
}

TEST_CASE("Argument: final intrinsic validation rejects layout and annotation drift",
          "[libk][argument][intrinsic][contract]") {
    auto compiler = compile_model_with_stdlib(R"K(
        module argument_contract;
        test() : int {
            value : int = 3;
            holder : Argument = Argument::capture(value);
            return holder.read<int>();
        }
    )K");
    REQUIRE(compiler);
    REQUIRE_FALSE(llvm::verifyModule(compiler->get_context_for_test()->module(), &llvm::errs()));
    auto k_namespace = compiler->get_unit()->get_root_namespace()->get_child_namespace("k");
    REQUIRE(k_namespace);
    auto holder = k_namespace->get_aggregate("Argument");
    REQUIRE(holder);
    std::shared_ptr<k::model::function> capture;
    for (auto function : holder->functions()) {
        if (!function->is_template()
            && k::model::gen::get_argument_intrinsic(*function) == k::model::gen::argument_intrinsic::capture) {
            capture = function;
            break;
        }
    }
    REQUIRE(capture);
    REQUIRE(k::model::gen::has_argument_intrinsic_contract(*capture));
    auto* native = llvm::cast<llvm::StructType>(holder->get_struct_type()->get_llvm_type());
    std::vector<llvm::Type*> fields(native->element_begin(), native->element_end());
    SECTION("Wrong physical field type") {
        auto changed = fields;
        changed[6] = llvm::Type::getInt8Ty(native->getContext());
        native->setBody(changed, false);
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
        native->setBody(fields, false);
    }
    SECTION("Wrong packing") {
        native->setBody(fields, true);
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
        native->setBody(fields, false);
    }
    SECTION("Raw annotation is provisional, not final authorization") {
        auto& annotations = capture->get_annotations_mutable();
        REQUIRE(annotations.size() == 1);
        auto resolved = annotations[0].resolved_type;
        annotations[0].resolved_type.reset();
        CHECK(k::model::gen::get_argument_intrinsic(*capture) == k::model::gen::argument_intrinsic::capture);
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
        annotations[0].resolved_type = resolved;
    }
    SECTION("A representation-compatible but different reference parameter is invalid") {
        auto parameter = capture->parameters().front();
        auto original = parameter->get_type();
        parameter->set_type(compiler->get_context_for_test()->from_type(
            k::model::primitive_type::LONG)->get_reference());
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
        parameter->set_type(original);
    }
    SECTION("A representation-compatible descriptor getter is not a semantic match") {
        auto getter = holder->get_function("getReferentType");
        REQUIRE(getter);
        auto original = getter->get_return_type();
        getter->set_return_type(compiler->get_context_for_test()->from_type(
            k::model::primitive_type::INT)->get_const()->get_view());
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
        getter->set_return_type(std::const_pointer_cast<k::model::type>(original));
    }
    SECTION("A signature-compatible function body is still not an intrinsic") {
        // Each section owns a fresh compiler/model; no null-block restoration is needed.
        REQUIRE(capture->get_block());
        CHECK_FALSE(k::model::gen::has_argument_intrinsic_contract(*capture));
    }
}
