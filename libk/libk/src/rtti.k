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
 * RTTI function descriptor.
 * This class describes a function (member or non-member) at runtime.
 * Constructors and destructors are excluded.
 */
final const class Function {
    name : const char[]?;
    fullName : const char[]?;
    owner : const AggregateType?;
    flags : unsigned int;
    annotations : const Annotation?[]?;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the function is static).
     * Bit 3: is_member (1 if the function is a member of an aggregate type).
     * Remaining bits are reserved for future use.
     */

    /**
     * Return the short (unqualified) name of the function.
     */
    const getName() : const char[]? {
        return name;
    }

    /**
     * Return the fully qualified name of the function
     * (including module, namespaces, and enclosing class if any).
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the enclosing aggregate type (class or interface) that owns this function.
     * Returns null if this is a non-member (free) function.
     */
    const getOwner() : const AggregateType? {
        return owner;
    }

    /**
     * Return the declared visibility of this function.
     */
    const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this function is declared as static.
     */
    const isStatic() : bool {
        return (flags & 4u) != 0u;
    }

    /**
     * Return true if this function is a member of an aggregate type (class or interface).
     * Returns false for non-member (free) functions.
     */
    const isMember() : bool {
        return (flags & 8u) != 0u;
    }

    /**
     * Return the annotations applied to this function.
     * Returns null if the function has no annotations.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }
}

/**
 * RTTI constructor descriptor.
 * This class describes a constructor at runtime.
 * Currently only exposes the number of parameters.
 */
final const class Constructor {
    paramCount : int;

    /**
     * Return the number of parameters of this constructor (excluding 'this').
     */
    const getParamCount() : int {
        return paramCount;
    }
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
    annotations : const Annotation?[]?;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the type is a static nested type).
     * Remaining bits are reserved for future use.
     */

    private Interface(name : const char[]?, fullName : const char[]?,
                      bases : const TypeInfo?[]?,
                      nested : const TypeInfo?[]?, enclosing : const TypeInfo?,
                      flags : unsigned int, annotations : const Annotation?[]?)
        : name(name), fullName(fullName), bases(bases), nested(nested),
          enclosing(enclosing), flags(flags), annotations(annotations) {}

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

    /**
     * Return the annotations attached to this interface, if any.
     * Each element is an Annotation instance.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }

    /**
     * Return the public member functions of this interface, if any.
     * Each element is a Function descriptor.
     * Reads the functions field from the RTTI struct via a C runtime helper
     * (field is synthesized by the compiler but not a K-level field to avoid
     * circular struct resolution).
     */
    const getFunctions() : const Function?[]? {
        return __k_iface_get_functions(this);
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
    annotations : const Annotation?[]?;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the type is a static nested type).
     * Remaining bits are reserved for future use.
     */

    private Class(name : const char[]?, fullName : const char[]?,
                  bases : const TypeInfo?[]?,
                  nested : const TypeInfo?[]?, enclosing : const TypeInfo?,
                  flags : unsigned int, annotations : const Annotation?[]?)
        : name(name), fullName(fullName), bases(bases), nested(nested),
          enclosing(enclosing), flags(flags), annotations(annotations) {}

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

    /**
     * Return the annotations attached to this class, if any.
     * Each element is an Annotation instance.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }

    /**
     * Return the public member functions of this class, if any.
     * Each element is a Function descriptor.
     * Reads the functions field from the RTTI struct via a C runtime helper.
     */
    const getFunctions() : const Function?[]? {
        return __k_class_get_functions(this);
    }

    /**
     * Return the public constructors of this class, if any.
     * Each element is a Constructor descriptor.
     * Reads the constructors field from the RTTI struct via a C runtime helper.
     */
    const getConstructors() : const Constructor?[]? {
        return __k_class_get_constructors(this);
    }

}

/**
 * RTTI annotation type descriptor.
 * This class is used to describe annotation types at runtime.
 */
final const class AnnotationType : public AggregateType {
    name : const char[]?;
    fullName : const char[]?;
    bases : const TypeInfo?[]?;
    nested : const TypeInfo?[]?;
    enclosing : const TypeInfo?;
    flags : unsigned int;
    annotations : const Annotation?[]?;

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the type is a static nested type).
     * Remaining bits are reserved for future use.
     */

    private AnnotationType(name : const char[]?, fullName : const char[]?,
                           bases : const TypeInfo?[]?,
                           nested : const TypeInfo?[]?, enclosing : const TypeInfo?,
                           flags : unsigned int, annotations : const Annotation?[]?)
        : name(name), fullName(fullName), bases(bases), nested(nested),
          enclosing(enclosing), flags(flags), annotations(annotations) {}

    /**
     * Return the name of the annotation type.
     */
    const getName() : const char[]? {
        return name;
    }

    /**
     * Return the fully qualified name of the annotation type.
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the bases of the annotation type, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types declared inside this annotation type, if any.
     * Each element is a TypeInfo descriptor.
     */
    const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this annotation type is nested in, if any.
     * Returns null if this annotation type is a top-level (non-nested) type.
     */
    const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this annotation type.
     */
    const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this annotation type is declared as a static nested type.
     */
    const isStatic() : bool {
        return (flags & 4u) != 0u;
    }

    /**
     * Return the annotations (meta-annotations) attached to this annotation type, if any.
     * Each element is an Annotation instance.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }
}

/**
 * RTTI unit (module) descriptor.
 * This class describes a compilation unit (module) at runtime.
 */
final const class Unit {
    name : const char[]?;
    fullName : const char[]?;
    functions : const Function?[]?;

    /**
     * Return the short name of the unit (module name).
     */
    const getName() : const char[]? {
        return name;
    }

    /**
     * Return the fully qualified name of the unit.
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the public non-member functions declared in this unit, if any.
     * Each element is a Function descriptor.
     */
    const getFunctions() : const Function?[]? {
        return functions;
    }
}

/**
 * C runtime helper: extract the RTTI AnnotationType pointer from an annotation's vtable.
 * Implemented in rtti.c. Takes the annotation pointer (void*) and returns
 * the AnnotationType pointer from vtable slot 0.
 */
extern __k_annotation_get_type(ann : const Annotation&) : const AnnotationType&;

/**
 * C runtime helper: extract the functions array from a Class RTTI global.
 * Implemented in rtti.c. Reads field index 10 of the RTTI struct.
 * Takes a Class& directly (not AggregateType&) to avoid base-class pointer
 * adjustment that would shift the pointer away from the struct start.
 */
extern __k_class_get_functions(rtti : const Class&) : const Function?[]?;

/**
 * C runtime helper: extract the functions array from an Interface RTTI global.
 * Implemented in rtti.c. Same logic as __k_class_get_functions, but takes
 * Interface& directly to avoid base-class pointer adjustment.
 */
extern __k_iface_get_functions(rtti : const Interface&) : const Function?[]?;

/**
 * C runtime helper: extract the constructors array from a Class RTTI global.
 * Implemented in rtti.c. Reads field index 11 of the RTTI struct.
 * Takes a Class& directly (not AggregateType&) to avoid base-class pointer
 * adjustment that would shift the pointer away from the struct start.
 */
extern __k_class_get_constructors(rtti : const Class&) : const Constructor?[]?;

/**
 * Annotation — root base class for all K annotation instances.
 *
 * Every annotation type in K implicitly inherits from Annotation.
 * Provides a minimal interface for runtime annotation type identification.
 * Annotation member functions are never virtual — only the vptr/RTTI slot
 * is used for type resolution.
 */
const class Annotation {
public:
    /**
     * Return the ::k::AnnotationType object describing the concrete annotation type.
     * Delegates to the C runtime helper __k_annotation_get_type.
     */
    final const getAnnotationType() : const AnnotationType& {
        return __k_annotation_get_type(this);
    }
}
