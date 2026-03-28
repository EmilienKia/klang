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
 * RTTI interface descriptor.
 * This class is used to describe interfaces at runtime.
 */
final const class Interface : TypeInfo {
    name : const char[]?;
    bases : const TypeInfo?[]?;
    nested : const TypeInfo?[]?;
    enclosing : const TypeInfo?;

    private Interface(name : const char[]?, bases : const TypeInfo?[]?,
                      nested : const TypeInfo?[]?, enclosing : const TypeInfo?)
        : name(name), bases(bases), nested(nested), enclosing(enclosing) {}

    /**
     * Return the name of the interface.
     */
    const getName() : const char[]? {
        return name;
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
}

/**
 * RTTI class descriptor.
 * This class is used to describe classes at runtime.
 */
final const class Class : TypeInfo {
    name : const char[]?;
    bases : const TypeInfo?[]?;
    nested : const TypeInfo?[]?;
    enclosing : const TypeInfo?;

    private Class(name : const char[]?, bases : const TypeInfo?[]?,
                  nested : const TypeInfo?[]?, enclosing : const TypeInfo?)
        : name(name), bases(bases), nested(nested), enclosing(enclosing) {}

    /**
     * Return the name of the class.
     */
    const getName() : const char[]? {
        return name;
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

}

