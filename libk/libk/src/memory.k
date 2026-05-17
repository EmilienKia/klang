/*
 * K Language standard library — memory primitives
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

module k;

/**
 * UniSlot<T> — raw storage for a single object of type T.
 *
 * Memory for T is embedded directly in the UniSlot struct layout.
 * The user is responsible for calling construct() / destruct() at the
 * appropriate time. The UniSlot constructor/destructor only manage the
 * storage region — they do NOT call T's constructor or destructor.
 *
 * Usage:
 *   slot : UniSlot<MyClass>;
 *   slot.construct();          // calls MyClass default constructor on _slot
 *   slot.get().doSomething();
 *   slot.destruct();           // calls MyClass destructor on _slot
 *   // UniSlot goes out of scope — no double-destruct
 *
 * With argument forwarding:
 *   slot : UniSlot<Point>;
 *   slot.construct<int, int>(10, 20);  // forwards to Point(int, int)
 *   slot.destruct();
 *
 * Calling destruct() without a prior construct(), or double-construct without
 * an intervening destruct(), is undefined behaviour.
 */
template<typename T>
struct UniSlot {
    private:
    _slot : T;

    public:
    @annotations::Intrinsic("UniSlot::constructor")
    UniSlot();

    @annotations::Intrinsic("UniSlot::destructor")
    ~UniSlot();

    @annotations::Intrinsic("UniSlot::construct")
    template<typename...Args>
    construct(Args...args);

    @annotations::Intrinsic("UniSlot::destruct")
    destruct();

    get() : T& { return _slot; }
}


/**
 * MultiSlot<T> — raw storage for a dynamically-allocated array of T objects.
 *
 * Memory is allocated via malloc/realloc/free but objects are NOT
 * automatically constructed or destroyed. The user must explicitly call
 * construct(index) / destruct(index) for each element's lifetime.
 *
 * This is the array counterpart to UniSlot<T>: it manages a buffer of
 * uninitialized storage that can hold up to 'capacity' objects of type T.
 *
 * Usage:
 *   slots : MultiSlot<Point>;
 *   slots.allocate(10);                   // allocate buffer for 10 Points
 *   slots.construct<int, int>(0, 1, 2);   // construct Point(1,2) at index 0
 *   slots.get(0).doSomething();
 *   slots.destruct(0);                    // destroy Point at index 0
 *   slots.deallocate();                   // free the buffer
 *
 * Reallocate preserves raw memory content (but not object validity beyond
 * the old capacity — the caller must track which indices are alive):
 *   slots.reallocate(20);                 // grow buffer to 20 elements
 *
 * Calling destruct() on an unconstructed index, or construct() on an
 * already-constructed index without intervening destruct(), is undefined
 * behaviour.
 */
template<typename T>
struct MultiSlot {
    private:
    _data : T*;
    _capacity : int;

    public:
    @annotations::Intrinsic("MultiSlot::constructor")
    MultiSlot();

    @annotations::Intrinsic("MultiSlot::destructor")
    ~MultiSlot();

    @annotations::Intrinsic("MultiSlot::allocate")
    allocate(capacity : int);

    @annotations::Intrinsic("MultiSlot::reallocate")
    reallocate(newCapacity : int);

    @annotations::Intrinsic("MultiSlot::deallocate")
    deallocate();

    @annotations::Intrinsic("MultiSlot::construct")
    template<typename...Args>
    construct(index : int, Args...args);

    @annotations::Intrinsic("MultiSlot::destruct")
    destruct(index : int);

    @annotations::Intrinsic("MultiSlot::get")
    get(index : int) : T&;

    const getCapacity() : int { return _capacity; }
}
