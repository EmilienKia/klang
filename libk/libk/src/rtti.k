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
 * Visibility of a type declaration.
 */
enum Visibility {
    PUBLIC = 0;
    PROTECTED = 1;
    PRIVATE = 2;
};

/**
 * Base RTTI type information descriptor.
 * All type descriptors will implement this interface.
 */
const interface TypeInfo {
    /**
     * Return the name of the type.
     * This name is the same as the name of the type in the source code without any context qualification.
     */
    const getName() : const char[]?;
}

/**
 * RTTI descriptor for aggregate types (classes and interfaces).
 * Provides access to bases, nested types, enclosing type, and modifiers.
 */
const interface AggregateType : public TypeInfo {
    /**
     * Return the fully qualified name of the type (including module and namespaces).
     * For example, "::mymodule::MyClass" or "::k::String".
     */
    const getFullName() : const char[]?;

    /**
     * Return the base interfaces and classes of the type, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getBases() : const TypeInfo?[]?;

    /**
     * Return the nested types (classes and interfaces) declared inside this type, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getNested() : const TypeInfo?[]?;

    /**
     * Return the enclosing type that this type is nested in, if any.
     * Returns null if this type is a top-level (non-nested) type.
     */
    const getEnclosing() : const TypeInfo?;

    /**
     * Return the declared visibility of this type.
     */
    const getVisibility() : Visibility;

    /**
     * Return true if this type is declared as a static nested type.
     */
    const isStatic() : bool;
}

/**
 * RTTI interface descriptor.
 * This class is used to describe interfaces at runtime.
 */
final const class Interface : public AggregateType {
    name : const char[]?;
    fullName : const char[]?;
    bases : const TypeInfo?[]?;
    nested : const TypeInfo?[]?;
    enclosing : const TypeInfo?;
    flags : unsigned int;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the type is a static nested type).
     * Remaining bits are reserved for future use.
     */

    private Interface(name : const char[]?, fullName : const char[]?,
                      bases : const TypeInfo?[]?,
                      nested : const TypeInfo?[]?, enclosing : const TypeInfo?,
                      flags : unsigned int)
        : name(name), fullName(fullName), bases(bases), nested(nested),
          enclosing(enclosing), flags(flags) {}

    /**
     * Return the name of the interface.
     */
    const getName() : const char[]? {
        return name;
    }

    /**
     * Return the fully qualified name of the interface.
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the bases of the interface, if any.
     * Each element is a TypeInfo descriptor (always an Interface at runtime).
     */
    const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types (classes and interfaces) declared inside this interface, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this interface is nested in, if any.
     * Returns null if this interface is a top-level (non-nested) type.
     */
    const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this interface.
     */
    const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this interface is declared as a static nested type.
     */
    const isStatic() : bool {
        return (flags & 4u) != 0u;
    }
}

/**
 * RTTI class descriptor.
 * This class is used to describe classes at runtime.
 */
final const class Class : public AggregateType {
    name : const char[]?;
    fullName : const char[]?;
    bases : const TypeInfo?[]?;
    nested : const TypeInfo?[]?;
    enclosing : const TypeInfo?;
    flags : unsigned int;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the type is a static nested type).
     * Remaining bits are reserved for future use.
     */

    private Class(name : const char[]?, fullName : const char[]?,
                  bases : const TypeInfo?[]?,
                  nested : const TypeInfo?[]?, enclosing : const TypeInfo?,
                  flags : unsigned int)
        : name(name), fullName(fullName), bases(bases), nested(nested),
          enclosing(enclosing), flags(flags) {}

    /**
     * Return the name of the class.
     */
    const getName() : const char[]? {
        return name;
    }

    /**
     * Return the fully qualified name of the class.
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the base interfaces and classes of the class, if any.
     * Each element is a TypeInfo descriptor (always an Interface or a Class at runtime).
     */
    const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types (classes and interfaces) declared inside this class, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this class is nested in, if any.
     * Returns null if this class is a top-level (non-nested) type.
     */
    const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this class.
     */
    const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this class is declared as a static nested type.
     */
    const isStatic() : bool {
        return (flags & 4u) != 0u;
    }

}
