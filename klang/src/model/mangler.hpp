/*
* K Language compiler
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
#ifndef KLANG_MANGLER_HPP
#define KLANG_MANGLER_HPP

#include <memory>

#include "../common/common.hpp"

/*
 * The K lang mangling is largely inspired by the Itanium C++ ABI mangling scheme,
 * reusing some of its principles and components, but is incompatible due to some structure changes.
 *
 * The K mangle scheme is on the following general form:
 * [prefix] [symbol type and props] [symbol name] [symbol type-specific suffix]
 *
 * The prefix is always '_K' to avoid conflicts with other mangling schemes.
 *
 * The symbol type and properties section encodes what kind of symbol is being mangled (e.g., function, type, variable)
 * and any relevant properties (e.g., visibility, linkage).
 * The symbol name section encodes the fully qualified name of the symbol, including namespaces and class names.
 * As the K imply the usage of namespace (at least the module namespace), names are automatically qualified.
 * The symbol type-specific suffix encodes additional information specific to the symbol type, such as function parameter types for functions.
 * Symbol types and props: [Symbol type] [Symbol props]
 * Symbol type: [F|V]
 * - 'F' for function
 * - 'V' for variable
 * Symbol props: [K][M]
 * - 'K' for constant (variable or function)
 * - 'M' for non-static member function, imply a first 'this' pointer parameter of the type of the parent structure,
 * to use in combination with 'K' for const member functions.
 *
 * The symbol name section encodes the fully qualified name of the symbol, including namespaces and class names.
 * Each name component is prefixed by its length to allow unambiguous parsing using the Itanium C++ ABI mangling style:
 * 'N' + encoded symbol name + [template parameters] + 'E'
 *
 * Function suffix: [Parameter types]
 * Suffix for functions are the concatenation of the mangled types of each parameter in order.
 *
 * Type mangling: [modifier][basic type|qualified type]
 * The modifiers are applied in the following order (from outermost to innermost):
 * - 'P' for pointer type
 * - 'R' for reference type
 * - 'K' for const qualifier
 * - 'V' for volatile qualifier
 * - 'r' for restrict qualifier
 * The basic types are encoded as:
 * - 'v' for void (only for return types or to indicate no parameters)
 * - 'b' for bool
 * - 'c' for char (8 bits)
 * - 'h' for unsigned char
 * - 's' for short (16 bits)
 * - 't' for unsigned short
 * - 'i' for int (32 bits)
 * - 'j' for unsigned int
 * - 'x' for long (64 bits)
 * - 'y' for unsigned long
 * - 'f' for float (32 bits)
 * - 'd' for double (64 bits)
 * - 'e' for long double (128 bits)
 * Qualified types (e.g., structures) are encoded using the same name mangling scheme as symbol names:
 * 'N' + encoded qualified name + [template parameter] + 'E'
 */
namespace k::model {

class type;
class context;
class function;
class constructor;
class destructor;
class static_constructor;
class static_destructor;

class mangler {
protected:
    std::shared_ptr<context> _context;

public:
    mangler() = delete;
    mangler(const mangler&) = default;
    mangler(mangler&&) = default;

    explicit mangler(const std::shared_ptr<context>& context) : _context(context) {};

    std::string mangle_type(const type& ty) const;

    std::string mangle_function(const function& func) const;
    std::string mangle_constructor(const constructor& ctor) const;
    std::string mangle_destructor(const destructor& dtor) const;
    std::string mangle_static_constructor(const static_constructor& sctor) const;
    std::string mangle_static_destructor(const static_destructor& sdtor) const;

    /**
     * Mangle the C2 (base-object constructor) variant of a constructor.
     * Itanium-inspired: _KFMC2N<class>E<params>
     * This is the base-subobject constructor variant used in virtual inheritance.
     */
    std::string mangle_constructor_c2(const constructor& ctor) const;

    /**
     * Mangle the D2 (base-object destructor) variant of a destructor.
     * Itanium-inspired: _KFMD2N<class>E v
     */
    std::string mangle_destructor_d2(const destructor& dtor) const;

    /**
     * Mangle the vtable global variable name for a class.
     * Convention: _KTVN<class>E
     */
    static std::string mangle_vtable(const name& class_name);

    /**
     * Mangle the RTTI global variable name for a class or interface.
     * Convention: _KTRIN<class>E
     *
     * The RTTI global is a constant struct { ptr self, ptr name_cstr, ptr null_introspection }.
     * The 'typeid' of the class is the address of this global itself (self-pointer), which
     * guarantees uniqueness even across dynamically-loaded modules (the linker ensures a single
     * definition per strong symbol).
     */
    static std::string mangle_rtti(const name& class_name);

    /**
     * Mangle the name of the virtual dispatch thunk (wrapper that loads the vptr and calls through the vtable).
     * Convention: _KFMvN<class><funcname>E<params>
     * The 'v' after 'M' denotes "virtual dispatch".
     */
    std::string mangle_virtual_dispatch(const function& func) const;

    /**
     * Mangle the name of the concrete implementation (non-virtual) of a virtual function.
     * Used for the specific-implementation symbol (invoked by the vtable slot and directly
     * for non-virtual qualified calls like MyClass::myMethod()).
     * Convention: same as mangle_function (the implementation IS the function body).
     * Alias for mangle_function provided for clarity.
     */
    std::string mangle_virtual_impl(const function& func) const {
        return mangle_function(func);
    }


    static std::string mangle_short_name(const std::string& short_name);

    static std::string mangle_fq_name(const name& name, bool with_k_prefix = false);
    static std::string mangle_fq_name_with_raw_last_part(const name& name, const std::string& last_part, bool with_k_prefix = false);

    static std::string mangle_namespace(const name& ns_name);
    static std::string mangle_global_variable(const name& ns_name);
    static std::string mangle_structure(const name& ns_name);


};

} // k::model
#endif //KLANG_MANGLER_HPP