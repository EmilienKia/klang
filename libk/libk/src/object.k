/*
 * K Language standard library — base module
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
 * C runtime helper: extract the RTTI Class pointer from an object's vtable.
 * Implemented in rtti.c. Takes the object pointer (void*) and returns
 * the Class pointer from vtable slot 0.
 */
/**
 * Object — root base class for all K objects.
 *
 * Provides a minimal common interface that every class in K can rely on.
 * The compiler auto-links the base standard library (module ::k) with every
 * K program or library.
 */
const class Object {
private:
    @ffi::Extern("C") private static __k_object_get_class(obj : const Object&) : const Class&;
public:
    /**
     * Virtual destructor.
     *
     * Declared first so it occupies vtable slot 0 (right after the RTTI slot),
     * making it the universal, stable destructor slot for every class and
     * interface in the whole type hierarchy: since every class and every
     * root interface implicitly (and virtually) extends ::k::Object, any
     * object can be destroyed polymorphically through a reference/owner
     * typed as its declared (possibly abstract/interface) type, as long as
     * ::k::Object stays reachable via the primary vtable inheritance chain.
     *
     * Empty by default: subclasses override it to release their own
     * resources; the compiler-generated chaining logic still calls each
     * base's destructor explicitly (this is not related to overriding).
     */
    ~Object() {
    }

    /**
     * Return a hash code for this object.
     *
     * Default implementation returns `0`. Subclasses should override this
     * method when they need value-based hashing.
     *
     * @return A hash code for this object.
     */
    const hash() : int {
        return 0;
    }

    /**
     * Return the ::k::Class object describing the concrete class of this object.
     *
     * Delegates to the C runtime helper `__k_object_get_class`.
     *
     * @return The runtime class descriptor of this object.
     */
    final const getClass() : const Class& {
        return __k_object_get_class(this);
    }
}
