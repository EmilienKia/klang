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

