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
}

namespace rtti {
    /** Exact semantic kinds; native pointer representation does not erase these distinctions. */
    enum TypeKind {
        VOID = 0; PRIMITIVE = 1; CONST = 2;
        REFERENCE = 3; VIEW = 4; LINK = 5; POINTER = 6; OWNER = 7; DRAIN = 8;
        ARRAY = 9; SIZED_ARRAY = 10; CALLABLE = 11; MEMBER_CALLABLE = 12;
        STRUCT = 13; CLASS = 14; INTERFACE = 15; ANNOTATION = 16;
        ENUM = 17; UNION = 18; ALIAS = 19; UNRESOLVED = 20; ERASED = 21;
    }

    enum Eligibility {
        DIRECT = 0; VIRTUAL_ONLY = 1; DELETED = 2; UNRESOLVED = 3;
        NO_ENTRY = 4; BY_VALUE_AGGREGATE_INPUT = 5;
        UNSUPPORTED_NATIVE_TYPE = 6; UNSUPPORTED_ABI = 7; FOREIGN = 8;
        PRODUCER_METADATA_REQUIRED = 9; INCOMPATIBLE_PROVIDER = 10;
    }

    enum NativeKind { VOID = 0; INTEGER = 1; FLOAT = 2; POINTER = 3; AGGREGATE = 4; UNSUPPORTED = 5; }
    enum PrimitiveKind {
        BOOL = 0; CHAR = 1; BYTE = 2; UNSIGNED_BYTE = 3; SHORT = 4;
        UNSIGNED_SHORT = 5; INT = 6; UNSIGNED_INT = 7; LONG = 8;
        UNSIGNED_LONG = 9; LONG_LONG = 10; UNSIGNED_LONG_LONG = 11; FLOAT = 12; DOUBLE = 13;
    }

    /** Immutable compiler-owned data. A null layout is represented by !hasLayout(), not size zero. */
    struct Type {
        private kind : unsigned int;
        private detail : unsigned int;
        private flags : unsigned int;
        private byteSize : unsigned long;
        private alignment : unsigned long;
        private extent : unsigned long;
        private element : const Type?;
        private result : const Type?;
        private parameters : const Type?[]?;
        private throwsTypes : const Type?[]?;
        private nominal : const k::TypeInfo?;
        private identity : const char[]?;
        private canonical : const Type?;
        private templateOrigin : const TemplateOrigin?;
        private lifecycle : const Lifecycle?[]?;
        private virtualBodies : const VirtualBody?[]?;
        private arrayDataOffset : unsigned long;
        private memberOf : const Type?;
        private projections : const Projection?[]?;
        private alternatives : const UnionAlternative?[]?;
        private tagOffset : unsigned long;
        private payloadOffset : unsigned long;
        private arrayLengthOffset : unsigned long;
        private nominalIdentity : const NominalIdentity?;

        const getKind() : TypeKind { return (TypeKind) kind; }
        /** Primitive: compiler primitive ordinal; callable: addresser ordinal. */
        const getDetail() : unsigned int { return detail; }
        const getPrimitiveKind() : PrimitiveKind { return (PrimitiveKind) detail; }
        /** Whether size/alignment describe a complete, fixed value allocation. */
        const hasLayout() : bool { return (flags & 1u) != 0u; }
        const isResolved() : bool { return (flags & 2u) != 0u; }
        const hasTrivialAggregateDestruction() : bool { return (flags & 4u) != 0u; }
        const hasExternalVirtualBodies() : bool { return (flags & 8u) != 0u; }
        /** Abstract declarations have RTTI/bodies but need a concrete dispatch map. */
        const isAbstractAggregate() : bool { return (flags & 16u) != 0u; }
        const getSize() : unsigned long { return byteSize; }
        const getAlignment() : unsigned long { return alignment; }
        const getExtent() : unsigned long { return extent; }
        const getElement() : const Type? { return element; }
        const getReturnType() : const Type? { return result; }
        const getParameterTypes() : const Type?[]? { return parameters; }
        const getThrowsTypes() : const Type?[]? { return throwsTypes; }
        const getNominalType() : const k::TypeInfo? { return nominal; }
        const getIdentity() : const char[]? { return identity; }
        /** Representation canonicalization; exact semantic identity still includes aliases. */
        const getCanonical() : const Type? { return canonical; }
        const getTemplateOrigin() : const TemplateOrigin? { return templateOrigin; }
        const getLifecycle() : const Lifecycle?[]? { return lifecycle; }
        const getVirtualBodies() : const VirtualBody?[]? { return virtualBodies; }
        const getArrayDataOffset() : unsigned long { return arrayDataOffset; }
        const getArrayLengthOffset() : unsigned long { return arrayLengthOffset; }
        const getArrayLengthBitWidth() : unsigned int { return detail; }
        const getMemberOf() : const Type? { return memberOf; }
        const getProjections() : const Projection?[]? { return projections; }
        const getAlternatives() : const UnionAlternative?[]? { return alternatives; }
        const getTagOffset() : unsigned long { return tagOffset; }
        const getPayloadOffset() : unsigned long { return payloadOffset; }
        const getNominalIdentity() : const NominalIdentity? { return nominalIdentity; }
    }

    /** An address-significant immutable anchor owned by one compiled unit/image. */
    struct Producer {
        private unit : const k::Unit?;
        private unitName : const char[]?;
        private target : const char[]?;
        private dataLayout : const char[]?;
        private rttiAbi : unsigned int;
        const getUnit() : const k::Unit? { return unit; }
        const getUnitName() : const char[]? { return unitName; }
        const getTarget() : const char[]? { return target; }
        const getDataLayout() : const char[]? { return dataLayout; }
        const getRttiAbi() : unsigned int { return rttiAbi; }
    }

    /** Symbolic keys require proven producer binding; they are not equality by name. */
    struct NominalIdentity {
        private owner : const Producer?;
        private producer : const Producer?;
        private declaration : const char[]?;
        private templateOrigin : const TemplateOrigin?;
        const getOwner() : const Producer? { return owner; }
        /** Null for imported/re-instantiated declarations until producer resolution. */
        const getProducer() : const Producer? { return producer; }
        const hasResolvedProducer() : bool { return producer != null; }
        const getDeclaration() : const char[]? { return declaration; }
        const getTemplateOrigin() : const TemplateOrigin? { return templateOrigin; }
    }

    struct UnionAlternative {
        private name : const char[]?;
        private type : const Type?;
        private tag : unsigned int;
        private flags : unsigned int;
        const getName() : const char[]? { return name; }
        const getType() : const Type? { return type; }
        const getTag() : unsigned int { return tag; }
        const isConst() : bool { return (flags & 1u) != 0u; }
    }

    struct Projection {
        private target : const Type?;
        private recipe : const ReceiverProjection?;
        private flags : unsigned int;
        const getTarget() : const Type? { return target; }
        const getRecipe() : const ReceiverProjection? { return recipe; }
        /** Visibility in bits 0-1, virtual base=4. */
        const getFlags() : unsigned int { return flags; }
    }

    struct ReceiverStep {
        private offset : unsigned long;
        private flags : unsigned int;
        const getOffset() : unsigned long { return offset; }
        const loadsPointer() : bool { return (flags & 1u) != 0u; }
    }

    struct ReceiverProjection {
        private delta : long;
        private steps : const ReceiverStep?[]?;
        private flags : unsigned int;
        /** Byte delta for a direct recipe; applied to the selected source subobject. */
        const getDelta() : long { return delta; }
        const getSteps() : const ReceiverStep?[]? { return steps; }
        const isIndirect() : bool { return (flags & 1u) != 0u; }
    }

    struct TemplateArgument {
        private kind : unsigned int;
        private type : const Type?;
        private value : const byte*;
        private valueSize : unsigned long;
        private valueAlignment : unsigned long;
        private pack : const Type?[]?;
        /** 0: type, 1: exact typed constant bytes, 2: type pack. */
        const getKind() : unsigned int { return kind; }
        const getType() : const Type? { return type; }
        const getValueAddress() : const byte* { return value; }
        const getValueSize() : unsigned long { return valueSize; }
        const getValueAlignment() : unsigned long { return valueAlignment; }
        const getPack() : const Type?[]? { return pack; }
    }

    struct TemplateOrigin {
        private identity : const char[]?;
        private arguments : const TemplateArgument?[]?;
        private enclosing : const TemplateOrigin?;
        const getIdentity() : const char[]? { return identity; }
        const getArguments() : const TemplateArgument?[]? { return arguments; }
        const getEnclosing() : const TemplateOrigin? { return enclosing; }
    }

    struct NativeValue {
        private kind : unsigned int;
        private bitWidth : unsigned int;
        private byteSize : unsigned long;
        private alignment : unsigned long;
        private semanticType : const Type?;
        private logicalIndex : int;
        private flags : unsigned int;
        private pointeeSize : unsigned long;
        private pointeeAlignment : unsigned long;
        private attributes : const char[]?;
        private offset : unsigned long;
        private fields : const NativeValue?[]?;
        private extent : unsigned long;
        const getKind() : NativeKind { return (NativeKind) kind; }
        const getBitWidth() : unsigned int { return bitWidth; }
        const getSize() : unsigned long { return byteSize; }
        const getAlignment() : unsigned long { return alignment; }
        const getType() : const Type? { return semanticType; }
        /** -1: hidden result, -2: receiver, -3: physical return, -4: native field. */
        const getLogicalIndex() : int { return logicalIndex; }
        /** sret=1, byval=2, sext=4, zext=8, inreg=16, noalias=32, callee cleanup=64, receiver projection=128. */
        const getFlags() : unsigned int { return flags; }
        const requiresReceiverProjection() : bool { return (flags & 128u) != 0u; }
        const getPointeeSize() : unsigned long { return pointeeSize; }
        const getPointeeAlignment() : unsigned long { return pointeeAlignment; }
        const getAttributes() : const char[]? { return attributes; }
        /** Offset relative to the containing physical aggregate. */
        const getOffset() : unsigned long { return offset; }
        /** Struct fields in order, or one repeated element for an LLVM array. */
        const getFields() : const NativeValue?[]? { return fields; }
        /** Exact native array element count, including zero-sized elements. */
        const getExtent() : unsigned long { return extent; }
    }

    struct NativeSignature {
        private revision : unsigned int;
        private callingConvention : unsigned int;
        private target : const char[]?;
        private dataLayout : const char[]?;
        private result : const NativeValue?;
        private arguments : const NativeValue?[]?;
        private resultIndex : int;
        private receiverIndex : int;
        private resultSize : unsigned long;
        private resultAlignment : unsigned long;
        private flags : unsigned int;
        private bodyReceiverIndex : int;
        const getRevision() : unsigned int { return revision; }
        const getCallingConvention() : unsigned int { return callingConvention; }
        const getTarget() : const char[]? { return target; }
        const getDataLayout() : const char[]? { return dataLayout; }
        const getResult() : const NativeValue? { return result; }
        const getArguments() : const NativeValue?[]? { return arguments; }
        const getResultIndex() : int { return resultIndex; }
        const getReceiverIndex() : int { return receiverIndex; }
        /** Actual provider's implicit receiver; may instead be an explicit source argument. */
        const getBodyReceiverIndex() : int { return bodyReceiverIndex; }
        const getResultSize() : unsigned long { return resultSize; }
        const getResultAlignment() : unsigned long { return resultAlignment; }
        /** Callee owner cleanup=1, caller result cleanup=2, unverified import=4, native vararg=8. */
        const getFlags() : unsigned int { return flags; }
        const isVariadic() : bool { return (flags & 8u) != 0u; }
    }

    struct Receiver {
        private type : const Type?;
        private slot : int;
        private flags : unsigned int;
        const getType() : const Type? { return type; }
        const getSlot() : int { return slot; }
        /** const=1, virtual=2, project using dynamic cast map/body receiver identity=4. */
        const getFlags() : unsigned int { return flags; }
    }

    struct Signature {
        private returnType : const Type?;
        private parameterTypes : const Type?[]?;
        private arity : unsigned int;
        private native : const NativeSignature?;
        private receiver : const Receiver?;
        private templateOrigin : const TemplateOrigin?;
        private eligibility : unsigned int;
        private symbol : const char[]?;
        private entry : const byte*;
        private throwsTypes : const Type?[]?;
        private enclosingTemplateOrigin : const TemplateOrigin?;
        const getReturnType() : const Type? { return returnType; }
        const getParameterTypes() : const Type?[]? { return parameterTypes; }
        /** All declared parameters are required, including source-defaulted parameters. */
        const getArity() : unsigned int { return arity; }
        const getNative() : const NativeSignature? { return native; }
        const getReceiver() : const Receiver? { return receiver; }
        const getTemplateOrigin() : const TemplateOrigin? { return templateOrigin; }
        const getEligibility() : Eligibility { return (Eligibility) eligibility; }
        const getSymbol() : const char[]? { return symbol; }
        /** Producer-local existing body address; never public symbol lookup or an adapter. */
        const getEntry() : const byte* { return entry; }
        const getThrowsTypes() : const Type?[]? { return throwsTypes; }
        const getEnclosingTemplateOrigin() : const TemplateOrigin? { return enclosingTemplateOrigin; }
    }

    struct Lifecycle {
        private operation : unsigned int;
        private flags : unsigned int;
        private signature : const Signature?;
        /** 0: destructor, 1: copy constructor, 2: drain constructor. */
        const getOperation() : unsigned int { return operation; }
        /** Visibility in bits 0-1, deleted=4, compiler-generated=8. */
        const getFlags() : unsigned int { return flags; }
        const getSignature() : const Signature? { return signature; }
    }

    struct VirtualBody {
        private source : const Type?;
        private slot : unsigned int;
        private body : const Signature?;
        private receiver : const Type?;
        private vtable : const byte*;
        private projection : const ReceiverProjection?;
        /** Source subobject identity plus its concrete vtable disambiguate repeated bases. */
        const getSource() : const Type? { return source; }
        const getSlot() : unsigned int { return slot; }
        const getBody() : const Signature? { return body; }
        /** Project the complete object to this type using its producer cast map. */
        const getReceiver() : const Type? { return receiver; }
        /** Producer-local existing table, not a preempting public definition. */
        const getVtable() : const byte* { return vtable; }
        const getProjection() : const ReceiverProjection? { return projection; }
    }
}

/**
 * Base RTTI type information descriptor.
 * Nominal aggregate descriptors implement this interface. Exact semantic
 * descriptors are separate immutable k::rtti::Type records.
 */
const interface TypeInfo {
    /** Dispatch to the callback for the concrete descriptor kind. */
    const accept(visitor : RttiVisitor&) : void;

    /**
     * Return the name of the type.
     * This name is the same as the name of the type in the source code without any context qualification.
     *
     * @return The unqualified source-level type name.
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
     *
     * @return The fully qualified type name.
     */
    const getFullName() : const char[]?;

    /**
     * Return the base interfaces and classes of the type, if any.
     * Each element is a TypeInfo descriptor.
     *
     * @return The direct base types, or `null` if none are recorded.
     */
    const getBases() : const TypeInfo?[]?;

    /**
     * Return the nested types (classes and interfaces) declared inside this type, if any.
     * Each element is a TypeInfo descriptor.
     *
     * @return The nested types, or `null` if none are recorded.
     */
    const getNested() : const TypeInfo?[]?;

    /**
     * Return the enclosing type that this type is nested in, if any.
     * Returns null if this type is a top-level (non-nested) type.
     *
     * @return The enclosing aggregate type, or `null` for a top-level type.
     */
    const getEnclosing() : const TypeInfo?;

    /**
     * Return the declared visibility of this type.
     *
     * @return The declared type visibility.
     */
    const getVisibility() : Visibility;

    /**
     * Return true if this type is declared as a static nested type.
     *
     * @return `true` when the type is declared static.
     */
    const isStatic() : bool;
}

/**
 * RTTI parameter descriptor.
 * This class describes a function or constructor parameter at runtime.
 * Preserves the parameter name/annotations and exact position/type/source markers.
 */
final const class Parameter {
    name : const char[]?;
    annotations : const Annotation?[]?;
    position : unsigned int;
    constraints : unsigned int;
    exactType : const rtti::Type?;

    const getPosition() : unsigned int { return position; }
    const getType() : const rtti::Type? { return exactType; }
    /** Source-default=1, source-vararg=2, pack-expansion=4, foreign-C-string=8. */
    const getConstraints() : unsigned int { return constraints; }
    const hasSourceDefault() : bool { return (constraints & 1u) != 0u; }
    const isVararg() : bool { return (constraints & 2u) != 0u; }

    /**
     * Return the name of the parameter.
     *
     * @return The parameter name.
     */
    const getName() : const char[]? {
        return name;
    }

    const accept(visitor : RttiVisitor&) : void {
        visitor.visitParameter(this);
    }

    /**
     * Return the annotations applied to this parameter.
     * Returns null if the parameter has no annotations.
     *
     * @return The parameter annotations, or `null` if none are present.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }
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
    parameters : const Parameter?[]?;
    signature : const rtti::Signature?;

    const getSignature() : const rtti::Signature? { return signature; }
    const getReturnType() : const rtti::Type? { return signature->getReturnType(); }
    const getArity() : unsigned int { return signature->getArity(); }

    /**
     * Internal bit-flag layout for the 'flags' field.
     * Bits 0-1: visibility (matches Visibility enum values: 0=PUBLIC, 1=PROTECTED, 2=PRIVATE).
     * Bit 2: is_static (1 if the function is static).
     * Bit 3: is_member (1 if the function is a member of an aggregate type).
     * Remaining bits are reserved for future use.
     */

    /**
     * Return the short (unqualified) name of the function.
     *
     * @return The unqualified function name.
     */
    const getName() : const char[]? {
        return name;
    }

    const accept(visitor : RttiVisitor&) : void {
        visitor.visitFunction(this);
    }

    /**
     * Return the fully qualified name of the function
     * (including module, namespaces, and enclosing class if any).
     *
     * @return The fully qualified function name.
     */
    const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the enclosing aggregate type (class or interface) that owns this function.
     * Returns null if this is a non-member (free) function.
     *
     * @return The owning aggregate type, or `null` for a free function.
     */
    const getOwner() : const AggregateType? {
        return owner;
    }

    /**
     * Return the declared visibility of this function.
     *
     * @return The declared function visibility.
     */
    const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this function is declared as static.
     *
     * @return `true` when the function is static.
     */
    const isStatic() : bool {
        return (flags & 4u) != 0u;
    }

    /**
     * Return true if this function is a member of an aggregate type (class or interface).
     * Returns false for non-member (free) functions.
     *
     * @return `true` for member functions, `false` for free functions.
     */
    const isMember() : bool {
        return (flags & 8u) != 0u;
    }

    /**
     * Return the annotations applied to this function.
     * Returns null if the function has no annotations.
     *
     * @return The function annotations, or `null` if none are present.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }

    /**
     * Return the parameters of this function.
     * Returns null if the function has no parameters.
     *
     * @return The function parameters, or `null` if none are present.
     */
    const getParameters() : const Parameter?[]? {
        return parameters;
    }
}

/**
 * RTTI constructor descriptor.
 * This class describes a constructor at runtime.
 * Exposes the number of parameters and annotations.
 */
final const class Constructor {
    paramCount : int;
    annotations : const Annotation?[]?;
    parameters : const Parameter?[]?;

    /**
     * Return the number of parameters of this constructor (excluding 'this').
     *
     * @return The constructor parameter count.
     */
    const getParamCount() : int {
        return paramCount;
    }

    const accept(visitor : RttiVisitor&) : void {
        visitor.visitConstructor(this);
    }

    /**
     * Return the annotations applied to this constructor.
     * Returns null if the constructor has no annotations.
     *
     * @return The constructor annotations, or `null` if none are present.
     */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }

    /**
     * Return the parameters of this constructor.
     * Returns null if the constructor has no parameters.
     *
     * @return The constructor parameters, or `null` if none are present.
     */
    const getParameters() : const Parameter?[]? {
        return parameters;
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
    _castMap : byte*;
    @ffi::Extern("C") private static __k_iface_get_functions(rtti : const Interface&) : const Function?[]?;
    @ffi::Extern("C") private static __k_iface_get_semantic_type(rtti : const Interface&) : const rtti::Type?;

    const getSemanticType() : const rtti::Type? {
        return __k_iface_get_semantic_type(this);
    }

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
          enclosing(enclosing), flags(flags), annotations(annotations), _castMap(null) {}

    /**
     * Return the name of the interface.
     */
    override const getName() : const char[]? {
        return name;
    }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitInterface(this);
    }

    /**
     * Return the fully qualified name of the interface.
     */
    override const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the bases of the interface, if any.
     * Each element is a TypeInfo descriptor (always an Interface at runtime).
     */
    override const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types (classes and interfaces) declared inside this interface, if any.
     * Each element is a TypeInfo descriptor.
     */
    override const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this interface is nested in, if any.
     * Returns null if this interface is a top-level (non-nested) type.
     */
    override const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this interface.
     */
    override const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this interface is declared as a static nested type.
     */
    override const isStatic() : bool {
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
    _castMap : byte*;
    @ffi::Extern("C") private static __k_class_get_functions(rtti : const Class&) : const Function?[]?;
    @ffi::Extern("C") private static __k_class_get_constructors(rtti : const Class&) : const Constructor?[]?;
    @ffi::Extern("C") private static __k_class_get_semantic_type(rtti : const Class&) : const rtti::Type?;

    const getSemanticType() : const rtti::Type? {
        return __k_class_get_semantic_type(this);
    }

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
          enclosing(enclosing), flags(flags), annotations(annotations), _castMap(null) {}

    /**
     * Return the name of the class.
     */
    override const getName() : const char[]? {
        return name;
    }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitClass(this);
    }

    /**
     * Return the fully qualified name of the class.
     */
    override const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the base interfaces and classes of the class, if any.
     * Each element is a TypeInfo descriptor (always an Interface or a Class at runtime).
     */
    override const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types (classes and interfaces) declared inside this class, if any.
     * Each element is a TypeInfo descriptor.
     */
    override const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this class is nested in, if any.
     * Returns null if this class is a top-level (non-nested) type.
     */
    override const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this class.
     */
    override const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this class is declared as a static nested type.
     */
    override const isStatic() : bool {
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
    override const getName() : const char[]? {
        return name;
    }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitAnnotationType(this);
    }

    /**
     * Return the fully qualified name of the annotation type.
     */
    override const getFullName() : const char[]? {
        return fullName;
    }

    /**
     * Return the bases of the annotation type, if any.
     * Each element is a TypeInfo descriptor.
     */
    override const getBases() : const TypeInfo?[]? {
        return bases;
    }

    /**
     * Return the nested types declared inside this annotation type, if any.
     * Each element is a TypeInfo descriptor.
     */
    override const getNested() : const TypeInfo?[]? {
        return nested;
    }

    /**
     * Return the enclosing type that this annotation type is nested in, if any.
     * Returns null if this annotation type is a top-level (non-nested) type.
     */
    override const getEnclosing() : const TypeInfo? {
        return enclosing;
    }

    /**
     * Return the declared visibility of this annotation type.
     */
    override const getVisibility() : Visibility {
        return (Visibility) (flags & 3u);
    }

    /**
     * Return true if this annotation type is declared as a static nested type.
     */
    override const isStatic() : bool {
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
 * RTTI descriptor for a namespace.
 *
 * Namespace descriptors enumerate only declarations owned directly by that
 * namespace. Nested aggregate types remain owned by their aggregate, and
 * imported declarations are never added to the local inventory.
 */
final const class Namespace {
    name : const char[]?;
    fullName : const char[]?;
    parent : const Namespace?;
    unit : const Unit?;
    namespaces : const Namespace?[]?;
    types : const TypeInfo?[]?;
    functions : const Function?[]?;
    annotations : const Annotation?[]?;

    /** Return the local namespace name; the unit root has an empty name. */
    const getName() : const char[]? {
        return name;
    }

    const accept(visitor : RttiVisitor&) : void {
        visitor.visitNamespace(this);
    }

    /** Return the fully-qualified namespace name; the unit root is empty. */
    const getFullName() : const char[]? {
        return fullName;
    }

    /** Return the parent namespace, or null for the unit root. */
    const getParent() : const Namespace? {
        return parent;
    }

    /** Return the owning compilation unit. */
    const getUnit() : const Unit? {
        return unit;
    }

    /** Return this namespace's direct child namespaces. */
    const getNamespaces() : const Namespace?[]? {
        return namespaces;
    }

    /** Return this namespace's direct class, interface, and annotation types. */
    const getTypes() : const TypeInfo?[]? {
        return types;
    }

    /** Return this namespace's direct public free functions. */
    const getFunctions() : const Function?[]? {
        return functions;
    }

    /** Return annotations attached to this namespace. */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }
}

/**
 * RTTI unit (module) descriptor.
 * This class describes the compiler model unit at runtime. A unit can contain
 * declarations merged from multiple source files.
 */
final const class Unit {
    name : const char[]?;
    fullName : const char[]?;
    functions : const Function?[]?;
    rootNamespace : const Namespace?;
    types : const TypeInfo?[]?;
    annotations : const Annotation?[]?;

    /**
     * Return the short name of the unit (module name).
     */
    const getName() : const char[]? {
        return name;
    }

    const accept(visitor : RttiVisitor&) : void {
        visitor.visitUnit(this);
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

    /** Return the non-null root namespace owned by this unit. */
    const getRootNamespace() : const Namespace? {
        return rootNamespace;
    }

    /** Return the direct types owned by the unit's root namespace. */
    const getTypes() : const TypeInfo?[]? {
        return types;
    }

    /** Return annotations attached to this compiler unit. */
    const getAnnotations() : const Annotation?[]? {
        return annotations;
    }
}

/**
 * C runtime helper: extract the RTTI AnnotationType pointer from an annotation's vtable.
 * Implemented in rtti.c. Takes the annotation pointer (void*) and returns
 * the AnnotationType pointer from vtable slot 0.
 */
/**
 * Annotation — root base class for all K annotation instances.
 *
 * Every annotation type in K implicitly inherits from Annotation.
 * Provides a minimal interface for runtime annotation type identification.
 * Annotation member functions are never virtual — only the vptr/RTTI slot
 * is used for type resolution.
 */
const class Annotation {
private:
    @ffi::Extern("C") private static __k_annotation_get_type(ann : const Annotation&) : const AnnotationType&;
public:
    /**
     * Return the ::k::AnnotationType object describing the concrete annotation type.
     * Delegates to the C runtime helper __k_annotation_get_type.
     */
    final const getAnnotationType() : const AnnotationType& {
        return __k_annotation_get_type(this);
    }

    final const accept(visitor : RttiVisitor&) : void {
        visitor.visitAnnotation(this);
    }
}

/**
 * Read-only callbacks for the concrete runtime RTTI descriptor kinds.
 * Implementations receive borrowed descriptors. During a module walk,
 * RecursiveRttiVisitor.currentModule() returns the owning image handle.
 */
public interface RttiVisitor {
    visitApplicationModule(image : const ApplicationModule&) : void;
    visitLibraryModule(image : const LibraryModule&) : void;
    visitNativeLibraryModule(image : const NativeLibraryModule&) : void;
    visitUnit(unitDescriptor : const Unit&) : void;
    visitNamespace(scope : const Namespace&) : void;
    visitClass(typeDescriptor : const Class&) : void;
    visitInterface(typeDescriptor : const Interface&) : void;
    visitAnnotationType(typeDescriptor : const AnnotationType&) : void;
    visitFunction(functionDescriptor : const Function&) : void;
    visitConstructor(constructorDescriptor : const Constructor&) : void;
    visitParameter(parameterDescriptor : const Parameter&) : void;
    visitAnnotation(annotationDescriptor : const Annotation&) : void;
}

/**
 * A depth-first visitor over the containment graph exposed by runtime RTTI.
 *
 * The default implementation visits each descriptor's declared children.
 * Subclasses may override an enter hook to prune a node's children; its leave
 * hook is still called. Callbacks are non-recursive unless a subclass invokes
 * another walk entry point.
 */
public class RecursiveRttiVisitor : public RttiVisitor {
private:
    _moduleStack : Vector<Shared<Module>>;

    _visitAnnotations(annotations : const Annotation?[]?) : void {
        if (annotations == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < annotations->size) {
            ann : const Annotation? = annotations[i];
            if (ann != null) {
                ann->accept(this);
            }
            ++i;
        }
    }

    _visitTypes(types : const TypeInfo?[]?) : void {
        if (types == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < types->size) {
            typeDescriptor : const TypeInfo? = types[i];
            if (typeDescriptor != null) {
                typeDescriptor->accept(this);
            }
            ++i;
        }
    }

    _visitNamespaces(scopes : const Namespace?[]?) : void {
        if (scopes == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < scopes->size) {
            scope : const Namespace? = scopes[i];
            if (scope != null) {
                scope->accept(this);
            }
            ++i;
        }
    }

    _visitFunctions(functions : const Function?[]?) : void {
        if (functions == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < functions->size) {
            functionDescriptor : const Function? = functions[i];
            if (functionDescriptor != null) {
                functionDescriptor->accept(this);
            }
            ++i;
        }
    }

    _visitConstructors(constructors : const Constructor?[]?) : void {
        if (constructors == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < constructors->size) {
            constructorDescriptor : const Constructor? = constructors[i];
            if (constructorDescriptor != null) {
                constructorDescriptor->accept(this);
            }
            ++i;
        }
    }

    _visitParameters(parameters : const Parameter?[]?) : void {
        if (parameters == null) {
            return;
        }
        i : unsigned int = 0;
        while (i < parameters->size) {
            parameterDescriptor : const Parameter? = parameters[i];
            if (parameterDescriptor != null) {
                parameterDescriptor->accept(this);
            }
            ++i;
        }
    }

    _visitUnits(image : const Module&) : void {
        units : Vector<UnitView> = image.units();
        i : unsigned int = 0;
        while (i < units.size()) {
            units.get(i).accept(this);
            ++i;
        }
    }

protected:
    enterApplicationModule(image : const ApplicationModule&) : bool { return true; }
    leaveApplicationModule(image : const ApplicationModule&) : void {}
    enterLibraryModule(image : const LibraryModule&) : bool { return true; }
    leaveLibraryModule(image : const LibraryModule&) : void {}
    enterNativeLibraryModule(image : const NativeLibraryModule&) : bool { return true; }
    leaveNativeLibraryModule(image : const NativeLibraryModule&) : void {}
    enterUnit(unitDescriptor : const Unit&) : bool { return true; }
    leaveUnit(unitDescriptor : const Unit&) : void {}
    enterNamespace(scope : const Namespace&) : bool { return true; }
    leaveNamespace(scope : const Namespace&) : void {}
    enterClass(typeDescriptor : const Class&) : bool { return true; }
    leaveClass(typeDescriptor : const Class&) : void {}
    enterInterface(typeDescriptor : const Interface&) : bool { return true; }
    leaveInterface(typeDescriptor : const Interface&) : void {}
    enterAnnotationType(typeDescriptor : const AnnotationType&) : bool { return true; }
    leaveAnnotationType(typeDescriptor : const AnnotationType&) : void {}
    enterFunction(functionDescriptor : const Function&) : bool { return true; }
    leaveFunction(functionDescriptor : const Function&) : void {}
    enterConstructor(constructorDescriptor : const Constructor&) : bool { return true; }
    leaveConstructor(constructorDescriptor : const Constructor&) : void {}
    enterParameter(parameterDescriptor : const Parameter&) : bool { return true; }
    leaveParameter(parameterDescriptor : const Parameter&) : void {}
    enterAnnotation(annotationDescriptor : const Annotation&) : bool { return true; }
    annotationObserved(annotationDescriptor : const Annotation&) : void {}
    leaveAnnotation(annotationDescriptor : const Annotation&) : void {}

public:
    /** Return the owner of the descriptor currently being traversed. */
    const currentModule() : Shared<Module> {
        if (_moduleStack.size() == 0u) {
            return Shared<Module>();
        }
        return _moduleStack.get(_moduleStack.size() - 1u);
    }

    /**
     * Traverse one module. Empty handles are rejected. The module is retained
     * for the complete call, including callbacks and nested walks.
     */
    walkModule(image : Shared<Module>) : void {
        if (!image) {
            throw NullDereferenceError();
        }
        _moduleStack.append(image);
        try {
            image.get()->accept(this);
        } catch (error : Throwable&) {
            _moduleStack.removeAt(_moduleStack.size() - 1u);
            throw;
        }
        _moduleStack.removeAt(_moduleStack.size() - 1u);
    }

    /**
     * Snapshot, identity-deduplicate, then traverse the supplied root modules.
     * Modules discovered by callbacks are not added to this walk.
     */
    walkModules(modules : const Vector<Shared<Module>>&) : void {
        snapshot : Vector<Shared<Module>> = modules;
        roots : Vector<Shared<Module>>;
        i : unsigned int = 0;
        while (i < snapshot.size()) {
            image : Shared<Module> = snapshot.get(i);
            if (!image) {
                throw NullDereferenceError();
            }
            found : bool = false;
            j : unsigned int = 0;
            while (j < roots.size()) {
                if (roots.get(j) == image) {
                    found = true;
                    break;
                }
                ++j;
            }
            if (!found) {
                roots.append(image);
            }
            ++i;
        }
        i = 0;
        while (i < roots.size()) {
            walkModule(roots.get(i));
            ++i;
        }
    }

    /** Traverse the application's fixed startup-linked module snapshot. */
    walkLinkedModules(image : Shared<ApplicationModule>) : void {
        if (!image) {
            throw NullDereferenceError();
        }
        snapshot : Vector<Shared<Module>> = image.get()->linkedModules();
        walkModules(snapshot);
    }

    override visitApplicationModule(image : const ApplicationModule&) : void {
        if (enterApplicationModule(image)) {
            _visitUnits(image);
        }
        leaveApplicationModule(image);
    }

    override visitLibraryModule(image : const LibraryModule&) : void {
        if (enterLibraryModule(image)) {
            _visitUnits(image);
        }
        leaveLibraryModule(image);
    }

    override visitNativeLibraryModule(image : const NativeLibraryModule&) : void {
        if (enterNativeLibraryModule(image)) {
            _visitUnits(image);
        }
        leaveNativeLibraryModule(image);
    }

    override visitUnit(unitDescriptor : const Unit&) : void {
        if (enterUnit(unitDescriptor)) {
            _visitAnnotations(unitDescriptor.getAnnotations());
            root : const Namespace? = unitDescriptor.getRootNamespace();
            if (root != null) {
                root->accept(this);
            }
        }
        leaveUnit(unitDescriptor);
    }

    override visitNamespace(scope : const Namespace&) : void {
        if (enterNamespace(scope)) {
            _visitAnnotations(scope.getAnnotations());
            _visitNamespaces(scope.getNamespaces());
            _visitTypes(scope.getTypes());
            _visitFunctions(scope.getFunctions());
        }
        leaveNamespace(scope);
    }

    override visitClass(typeDescriptor : const Class&) : void {
        if (enterClass(typeDescriptor)) {
            _visitAnnotations(typeDescriptor.getAnnotations());
            _visitTypes(typeDescriptor.getNested());
            _visitFunctions(typeDescriptor.getFunctions());
            _visitConstructors(typeDescriptor.getConstructors());
        }
        leaveClass(typeDescriptor);
    }

    override visitInterface(typeDescriptor : const Interface&) : void {
        if (enterInterface(typeDescriptor)) {
            _visitAnnotations(typeDescriptor.getAnnotations());
            _visitTypes(typeDescriptor.getNested());
            _visitFunctions(typeDescriptor.getFunctions());
        }
        leaveInterface(typeDescriptor);
    }

    override visitAnnotationType(typeDescriptor : const AnnotationType&) : void {
        if (enterAnnotationType(typeDescriptor)) {
            _visitAnnotations(typeDescriptor.getAnnotations());
            _visitTypes(typeDescriptor.getNested());
        }
        leaveAnnotationType(typeDescriptor);
    }

    override visitFunction(functionDescriptor : const Function&) : void {
        if (enterFunction(functionDescriptor)) {
            _visitAnnotations(functionDescriptor.getAnnotations());
            _visitParameters(functionDescriptor.getParameters());
        }
        leaveFunction(functionDescriptor);
    }

    override visitConstructor(constructorDescriptor : const Constructor&) : void {
        if (enterConstructor(constructorDescriptor)) {
            _visitAnnotations(constructorDescriptor.getAnnotations());
            _visitParameters(constructorDescriptor.getParameters());
        }
        leaveConstructor(constructorDescriptor);
    }

    override visitParameter(parameterDescriptor : const Parameter&) : void {
        if (enterParameter(parameterDescriptor)) {
            _visitAnnotations(parameterDescriptor.getAnnotations());
        }
        leaveParameter(parameterDescriptor);
    }

    override visitAnnotation(annotationDescriptor : const Annotation&) : void {
        if (enterAnnotation(annotationDescriptor)) {
            annotationObserved(annotationDescriptor);
        }
        leaveAnnotation(annotationDescriptor);
    }
}
