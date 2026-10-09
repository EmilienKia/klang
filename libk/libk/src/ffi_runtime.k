/*
 * K Language standard library — runtime C foreign calls
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

module k;

namespace ffi {

/** Numeric values 0..9 match runtime/runtime_ffi.h. */
public enum FfiErrorCode {
    OK = 0;
    INVALID_TYPE = 1;
    UNSUPPORTED_ABI = 2;
    INVALID_SIGNATURE = 3;
    ARGUMENT_COUNT = 4;
    ARGUMENT_TYPE = 5;
    OUT_OF_RANGE = 6;
    NULL_FUNCTION = 7;
    OUT_OF_MEMORY = 8;
    INVALID_FRAME = 9;
}

private ffiErrorMessage(status : int) : String {
    if (status == 0) return String("Success");
    if (status == 1) return String("Invalid native type");
    if (status == 2) return String("Unsupported ABI: requires default Linux LP64 C ABI, signed char and binary32/binary64");
    if (status == 3) return String("Invalid native signature");
    if (status == 4) return String("Native argument count mismatch or uninitialized argument");
    if (status == 5) return String("Native argument or result type mismatch");
    if (status == 6) return String("Native argument out of range");
    if (status == 7) return String("Empty or zero-address native function");
    if (status == 8) return String("Native call allocation failed");
    if (status == 9) return String("Invalid native call frame or unavailable result");
    return String("Unknown native call status");
}

public final class FfiError {
private:
    _status : int;
    _message : String;
    _argumentIndex : int;

public:
    FfiError(status : int, argumentIndex : int = -1)
        : _status(status), _message(ffiErrorMessage(status)), _argumentIndex(argumentIndex) {}

    FfiError(code : FfiErrorCode, argumentIndex : int = -1)
        : _status((int) code), _message(ffiErrorMessage((int) code)), _argumentIndex(argumentIndex) {}

    FfiError(other : const FfiError&)
        : _status(other._status), _message(other._message), _argumentIndex(other._argumentIndex) {}

    operator =(other : const FfiError&) : FfiError& {
        if (&this != &other) {
            _status = other._status;
            _message = other._message;
            _argumentIndex = other._argumentIndex;
        }
        return this;
    }

    const code() : FfiErrorCode { return (FfiErrorCode) _status; }
    const status() : int { return _status; }
    const message() : const String& { return _message; }
    const argumentIndex() : int { return _argumentIndex; }
}

/**
 * An opaque native address, never an allocation owner.
 *
 * A keepalive only retains an associated K object. It makes no assertion about
 * whether that object owns the address or how long foreign code retains it.
 */
public final class NativePointer {
private:
    _address : byte*;
    _symbol : NativeSymbol;
    _keepalive : Shared<Object>;

    NativePointer(address : byte*, symbol : const NativeSymbol&, keepalive : const Shared<Object>&)
        : _address(address), _symbol(symbol), _keepalive(keepalive) {}

public:
    NativePointer() : _address(null), _symbol(), _keepalive() {}

    NativePointer(other : const NativePointer&)
        : _address(other._address), _symbol(other._symbol), _keepalive(other._keepalive) {}

    operator =(other : const NativePointer&) : NativePointer& {
        _address = other._address;
        _symbol = other._symbol;
        _keepalive = other._keepalive;
        return this;
    }

    static borrowed(address : byte*) : NativePointer {
        symbol : NativeSymbol;
        keepalive : Shared<Object>;
        return NativePointer(address, symbol, keepalive);
    }

    static retained(address : byte*, keepalive : const Shared<Object>&) : NativePointer {
        symbol : NativeSymbol;
        return NativePointer(address, symbol, keepalive);
    }

    static fromSymbol(symbol : const NativeSymbol&) : NativePointer {
        keepalive : Shared<Object>;
        return NativePointer(symbol.rawAddress(), symbol, keepalive);
    }

    const rawAddress() : byte* { return _address; }
    const isNull() : bool { return _address == null; }
    const keepalive() : Shared<Object> { return _keepalive; }
}

/** Language-independent category of a copied foreign result. */
public enum ResultKind {
    UNAVAILABLE = 0;
    VOID = 1;
    SIGNED = 2;
    UNSIGNED = 3;
    FLOATING = 4;
    POINTER = 5;
}

public final class CallResult {
    friend ::k::ffi::c::CFunction;

private:
    _kind : ResultKind;
    _signed : long;
    _unsigned : unsigned long;
    _floating : double;
    _pointer : NativePointer;
    _errnoCaptured : bool;
    _errno : int;

    CallResult(kind : ResultKind, signedValue : long, unsignedValue : unsigned long,
               floatingValue : double, pointer : const NativePointer&, captured : bool, errorNumber : int)
        : _kind(kind), _signed(signedValue), _unsigned(unsignedValue),
          _floating(floatingValue), _pointer(pointer), _errnoCaptured(captured), _errno(errorNumber) {}

public:
    CallResult() : _kind(ResultKind::UNAVAILABLE), _signed(0L), _unsigned(0uL),
        _floating(0.0), _pointer(), _errnoCaptured(false), _errno(0) {}

    CallResult(other : const CallResult&)
        : _kind(other._kind), _signed(other._signed), _unsigned(other._unsigned),
          _floating(other._floating), _pointer(other._pointer),
          _errnoCaptured(other._errnoCaptured), _errno(other._errno) {}

    operator =(other : const CallResult&) : CallResult& {
        _kind = other._kind;
        _signed = other._signed;
        _unsigned = other._unsigned;
        _floating = other._floating;
        _pointer = other._pointer;
        _errnoCaptured = other._errnoCaptured;
        _errno = other._errno;
        return this;
    }

    const kind() : ResultKind { return _kind; }
    const isVoid() : bool { return _kind == ResultKind::VOID; }
    const hasReturnValue() : bool {
        return _kind != ResultKind::VOID && _kind != ResultKind::UNAVAILABLE;
    }
    const errnoCaptured() : bool { return _errnoCaptured; }

    const asSigned() : Expected<long, FfiError> {
        result : Expected<long, FfiError>;
        if (_kind == ResultKind::SIGNED) {
            value : long = _signed;
            result.setResult(value);
        } else {
            error : FfiError(FfiErrorCode::ARGUMENT_TYPE);
            result.setError(error);
        }
        return result;
    }

    const asUnsigned() : Expected<unsigned long, FfiError> {
        result : Expected<unsigned long, FfiError>;
        if (_kind == ResultKind::UNSIGNED) {
            value : unsigned long = _unsigned;
            result.setResult(value);
        } else {
            error : FfiError(FfiErrorCode::ARGUMENT_TYPE);
            result.setError(error);
        }
        return result;
    }

    const asFloating() : Expected<double, FfiError> {
        result : Expected<double, FfiError>;
        if (_kind == ResultKind::FLOATING) {
            value : double = _floating;
            result.setResult(value);
        } else {
            error : FfiError(FfiErrorCode::ARGUMENT_TYPE);
            result.setError(error);
        }
        return result;
    }

    const asPointer() : Expected<NativePointer, FfiError> {
        result : Expected<NativePointer, FfiError>;
        if (_kind == ResultKind::POINTER) {
            value : NativePointer = _pointer;
            result.setResult(value);
        } else {
            error : FfiError(FfiErrorCode::ARGUMENT_TYPE);
            result.setError(error);
        }
        return result;
    }

    const getErrno() : Expected<int, FfiError> {
        result : Expected<int, FfiError>;
        if (_errnoCaptured) {
            value : int = _errno;
            result.setResult(value);
        } else {
            error : FfiError(FfiErrorCode::INVALID_FRAME);
            result.setError(error);
        }
        return result;
    }
}

namespace c {

// Transparent aliases for the supported Linux LP64, signed-char C host.
public alias Char : byte;
public alias SignedChar : byte;
public alias UnsignedChar : unsigned byte;
public alias Short : short;
public alias UShort : unsigned short;
public alias Int : int;
public alias UInt : unsigned int;
public alias Long : long;
public alias ULong : unsigned long;
public alias LongLong : long;
public alias ULongLong : unsigned long;
public alias Size : unsigned long;
public alias Ptrdiff : long;
public alias SizeT : unsigned long;
public alias PtrdiffT : long;
public alias Float : float;
public alias Double : double;
public alias Bool : bool;
public alias Int8 : byte;
public alias UInt8 : unsigned byte;
public alias Int16 : short;
public alias UInt16 : unsigned short;
public alias Int32 : int;
public alias UInt32 : unsigned int;
public alias Int64 : long;
public alias UInt64 : unsigned long;

/** Exact native type IDs from runtime/runtime_ffi.h. */
public enum CType {
    VOID = 0;
    SINT8 = 1;
    UINT8 = 2;
    SINT16 = 3;
    UINT16 = 4;
    SINT32 = 5;
    UINT32 = 6;
    SINT64 = 7;
    UINT64 = 8;
    FLOAT = 9;
    DOUBLE = 10;
    POINTER = 11;
    CHAR = 12;
    SCHAR = 13;
    UCHAR = 14;
    SHORT = 15;
    USHORT = 16;
    INT = 17;
    UINT = 18;
    LONG = 19;
    ULONG = 20;
    LLONG = 21;
    ULLONG = 22;
    SIZE_T = 23;
    PTRDIFF_T = 24;
    BOOL = 25;
}

public enum CAbi {
    DEFAULT = 0;
}

@::k::ffi::Extern("C") private __k_ffi_type_canonical(type : int) : int;
@::k::ffi::Extern("C") private __k_ffi_type_size(type : int) : int;
@::k::ffi::Extern("C") private __k_ffi_prepare(returnType : int, parameters : const int*,
    count : unsigned int, abi : int, status : int*) : byte*;
@::k::ffi::Extern("C") private __k_ffi_signature_release(signature : byte*);
@::k::ffi::Extern("C") private __k_ffi_frame_create(signature : byte*, status : int*) : byte*;
@::k::ffi::Extern("C") private __k_ffi_frame_release(frame : byte*);
@::k::ffi::Extern("C") private __k_ffi_set_signed(frame : byte*, index : unsigned int, type : int, value : long) : int;
@::k::ffi::Extern("C") private __k_ffi_set_unsigned(frame : byte*, index : unsigned int, type : int, value : unsigned long) : int;
@::k::ffi::Extern("C") private __k_ffi_set_float(frame : byte*, index : unsigned int, type : int, value : double) : int;
@::k::ffi::Extern("C") private __k_ffi_set_pointer(frame : byte*, index : unsigned int, type : int, value : byte*) : int;
@::k::ffi::Extern("C") private __k_ffi_invoke(function : byte*, frame : byte*, count : unsigned int, captureErrno : int) : int;
@::k::ffi::Extern("C") private __k_ffi_result_type(frame : byte*, status : int*) : int;
@::k::ffi::Extern("C") private __k_ffi_result_signed(frame : byte*, status : int*) : long;
@::k::ffi::Extern("C") private __k_ffi_result_unsigned(frame : byte*, status : int*) : unsigned long;
@::k::ffi::Extern("C") private __k_ffi_result_float(frame : byte*, status : int*) : double;
@::k::ffi::Extern("C") private __k_ffi_result_pointer(frame : byte*, status : int*) : byte*;
@::k::ffi::Extern("C") private __k_ffi_result_errno(frame : byte*, status : int*) : int;

public typeSize(type : CType) : Expected<unsigned int, FfiError> {
    result : Expected<unsigned int, FfiError>;
    size : int = __k_ffi_type_size((int) type);
    if (size < 0) {
        error : FfiError(FfiErrorCode::INVALID_TYPE);
        result.setError(error);
    } else {
        value : unsigned int = (unsigned int) size;
        result.setResult(value);
    }
    return result;
}

public canonicalType(type : CType) : Expected<CType, FfiError> {
    result : Expected<CType, FfiError>;
    canonical : int = __k_ffi_type_canonical((int) type);
    if (canonical < 0) {
        error : FfiError(FfiErrorCode::INVALID_TYPE);
        result.setError(error);
    } else {
        value : CType = (CType) canonical;
        result.setResult(value);
    }
    return result;
}

private final class SignatureHandle : public Object {
    friend CSignature;
    friend CPreparedSignature;
    friend CFunction;

private:
    _handle : byte*;
    _signature : CSignature;

public:
    SignatureHandle(signature : const CSignature&) : _handle(null), _signature(signature) {}
    ~SignatureHandle() {
        __k_ffi_signature_release(_handle);
    }
}

/** Immutable signature value; parameter arrays and vectors are deep-copied. */
public final class CSignature {
private:
    _returnType : CType;
    _parameters : Vector<CType>;
    _abi : CAbi;

public:
    CSignature() : _returnType(CType::VOID), _parameters(), _abi(CAbi::DEFAULT) {}

    CSignature(returnType : CType, parameters : const CType[]&, abi : CAbi = CAbi::DEFAULT)
        : _returnType(returnType), _parameters(parameters), _abi(abi) {}

    CSignature(returnType : CType, parameters : const Vector<CType>&, abi : CAbi = CAbi::DEFAULT)
        : _returnType(returnType), _parameters(parameters), _abi(abi) {}

    CSignature(other : const CSignature&)
        : _returnType(other._returnType), _parameters(other._parameters), _abi(other._abi) {}

    operator =(other : const CSignature&) : CSignature& {
        if (&this != &other) {
            _parameters = other._parameters;
            _returnType = other._returnType;
            _abi = other._abi;
        }
        return this;
    }

    const getReturnType() : CType { return _returnType; }
    const getAbi() : CAbi { return _abi; }
    const getParameterCount() : unsigned int { return _parameters.size(); }
    const getParameterTypes() : Vector<CType> { return _parameters; }
    const getParameterType(index : unsigned int) : CType { return _parameters.get(index); }

    const prepare() : Expected<CPreparedSignature, FfiError> {
        result : Expected<CPreparedSignature, FfiError>;
        // Allocate the K holder before creating any native resource.
        resource : Shared<SignatureHandle>(new SignatureHandle(this));
        count : unsigned int = _parameters.size();
        types : int[]! = new int[count];
        try {
            {
            i : unsigned int = 0;
            while (i < count) {
                types[i] = (int) _parameters.get(i);
                ++i;
            }
            data : const int* = null;
            if (count != 0u) data = &types[0u];
            status : int = 0;
            resource.get()->_handle = __k_ffi_prepare((int) _returnType, data, count, (int) _abi, &status);
            if (status != 0 || resource.get()->_handle == null) {
                if (status == 0) status = (int) FfiErrorCode::OUT_OF_MEMORY;
                error : FfiError(status);
                result.setError(error);
            } else {
                prepared : CPreparedSignature(resource);
                result.setResult(prepared);
            }
            }
        } finally {
            delete types;
        }
        return result;
    }
}

/** Copies share only the immutable prepared native resource. */
public final class CPreparedSignature {
    friend CSignature;
    friend CFunction;

private:
    _resource : Shared<SignatureHandle>;

    CPreparedSignature(resource : const Shared<SignatureHandle>&) : _resource(resource) {}

public:
    CPreparedSignature() : _resource() {}

    CPreparedSignature(other : const CPreparedSignature&) : _resource(other._resource) {}

    operator =(other : const CPreparedSignature&) : CPreparedSignature& {
        if (&this != &other) {
            _resource = other._resource;
        }
        return this;
    }

    const isPrepared() : bool { return !_resource.isNull(); }
    const getSignature() : CSignature {
        if (!_resource) return CSignature();
        return _resource.get()->_signature;
    }
    const getReturnType() : CType {
        if (!_resource) return CType::VOID;
        return _resource.get()->_signature.getReturnType();
    }
    const getAbi() : CAbi {
        if (!_resource) return CAbi::DEFAULT;
        return _resource.get()->_signature.getAbi();
    }
    const getParameterCount() : unsigned int {
        if (!_resource) return 0u;
        return _resource.get()->_signature.getParameterCount();
    }
    const getParameterTypes() : Vector<CType> {
        if (!_resource) {
            empty : Vector<CType>;
            return empty;
        }
        return _resource.get()->_signature.getParameterTypes();
    }
    const getParameterType(index : unsigned int) : CType {
        if (!_resource) throw IndexOutOfBoundsError();
        return _resource.get()->_signature.getParameterType(index);
    }
}

public final class CSignatureBuilder {
private:
    _returnType : CType;
    _parameters : Vector<CType>;
    _abi : CAbi;

public:
    CSignatureBuilder() : _returnType(CType::VOID), _parameters(), _abi(CAbi::DEFAULT) {}

    CSignatureBuilder(other : const CSignatureBuilder&)
        : _returnType(other._returnType), _parameters(other._parameters), _abi(other._abi) {}

    operator =(other : const CSignatureBuilder&) : CSignatureBuilder& {
        if (&this != &other) {
            _parameters = other._parameters;
            _returnType = other._returnType;
            _abi = other._abi;
        }
        return this;
    }

    setReturnType(type : CType) { _returnType = type; }
    addParameter(type : CType) { _parameters.append(type); }
    setAbi(abi : CAbi) { _abi = abi; }
    reset() {
        _parameters.clear();
        _returnType = CType::VOID;
        _abi = CAbi::DEFAULT;
    }

    returns(type : CType) : CSignatureBuilder& {
        setReturnType(type);
        return this;
    }
    parameter(type : CType) : CSignatureBuilder& {
        addParameter(type);
        return this;
    }
    callingConvention(abi : CAbi) : CSignatureBuilder& {
        setAbi(abi);
        return this;
    }

    const getReturnType() : CType { return _returnType; }
    const getAbi() : CAbi { return _abi; }
    const getParameterCount() : unsigned int { return _parameters.size(); }
    const getParameterTypes() : Vector<CType> { return _parameters; }
    const getParameterType(index : unsigned int) : CType { return _parameters.get(index); }
    const build() : CSignature { return CSignature(_returnType, _parameters, _abi); }
    const prepare() : Expected<CPreparedSignature, FfiError> {
        signature : CSignature = build();
        return signature.prepare();
    }
}

/** Explicit declared type plus an unconverted scalar or pointer payload. */
public final class CValue {
    friend CFunction;

private:
    _type : CType;
    _kind : ResultKind;
    _signed : long;
    _unsigned : unsigned long;
    _floating : double;
    _pointer : NativePointer;

public:
    CValue() : _type(CType::VOID), _kind(ResultKind::UNAVAILABLE),
        _signed(0L), _unsigned(0uL), _floating(0.0), _pointer() {}

    CValue(value : byte) : _type(CType::SCHAR), _kind(ResultKind::SIGNED),
        _signed(value), _unsigned(0uL), _floating(0.0), _pointer() {}
    CValue(value : unsigned byte) : _type(CType::UCHAR), _kind(ResultKind::UNSIGNED),
        _signed(0L), _unsigned(value), _floating(0.0), _pointer() {}
    CValue(value : short) : _type(CType::SHORT), _kind(ResultKind::SIGNED),
        _signed(value), _unsigned(0uL), _floating(0.0), _pointer() {}
    CValue(value : unsigned short) : _type(CType::USHORT), _kind(ResultKind::UNSIGNED),
        _signed(0L), _unsigned(value), _floating(0.0), _pointer() {}
    CValue(value : int) : _type(CType::INT), _kind(ResultKind::SIGNED),
        _signed(value), _unsigned(0uL), _floating(0.0), _pointer() {}
    CValue(value : unsigned int) : _type(CType::UINT), _kind(ResultKind::UNSIGNED),
        _signed(0L), _unsigned(value), _floating(0.0), _pointer() {}
    CValue(value : long) : _type(CType::LONG), _kind(ResultKind::SIGNED),
        _signed(value), _unsigned(0uL), _floating(0.0), _pointer() {}
    CValue(value : unsigned long) : _type(CType::ULONG), _kind(ResultKind::UNSIGNED),
        _signed(0L), _unsigned(value), _floating(0.0), _pointer() {}
    CValue(value : float) : _type(CType::FLOAT), _kind(ResultKind::FLOATING),
        _signed(0L), _unsigned(0uL), _floating(value), _pointer() {}
    CValue(value : double) : _type(CType::DOUBLE), _kind(ResultKind::FLOATING),
        _signed(0L), _unsigned(0uL), _floating(value), _pointer() {}
    CValue(value : bool) : _type(CType::BOOL), _kind(ResultKind::UNSIGNED),
        _signed(0L), _unsigned(value ? 1uL : 0uL), _floating(0.0), _pointer() {}
    CValue(value : const NativePointer&) : _type(CType::POINTER), _kind(ResultKind::POINTER),
        _signed(0L), _unsigned(0uL), _floating(0.0), _pointer(value) {}

    CValue(other : const CValue&)
        : _type(other._type), _kind(other._kind), _signed(other._signed),
          _unsigned(other._unsigned), _floating(other._floating), _pointer(other._pointer) {}

    operator =(other : const CValue&) : CValue& {
        _type = other._type;
        _kind = other._kind;
        _signed = other._signed;
        _unsigned = other._unsigned;
        _floating = other._floating;
        _pointer = other._pointer;
        return this;
    }

    static signedValue(type : CType, value : long) : CValue {
        result : CValue;
        result._type = type;
        result._kind = ResultKind::SIGNED;
        result._signed = value;
        return result;
    }
    static unsignedValue(type : CType, value : unsigned long) : CValue {
        result : CValue;
        result._type = type;
        result._kind = ResultKind::UNSIGNED;
        result._unsigned = value;
        return result;
    }
    static floatingValue(type : CType, value : double) : CValue {
        result : CValue;
        result._type = type;
        result._kind = ResultKind::FLOATING;
        result._floating = value;
        return result;
    }
    static pointerValue(value : const NativePointer&) : CValue {
        return CValue(value);
    }

    const getType() : CType { return _type; }
    const kind() : ResultKind { return _kind; }
}

private final class PointerResultLease : public Object {
private:
    _symbol : NativeSymbol;
    _arguments : Vector<CValue>;

public:
    PointerResultLease(symbol : const NativeSymbol&, arguments : const Vector<CValue>&)
        : _symbol(symbol), _arguments(arguments) {}
}

/** A symbol, immutable prepared signature and per-function errno policy. */
public final class CFunction {
    friend CCallBuilder;

private:
    _symbol : NativeSymbol;
    _prepared : CPreparedSignature;
    _captureErrno : bool;

    CFunction(symbol : const NativeSymbol&, prepared : const CPreparedSignature&, captureErrno : bool)
        : _symbol(symbol), _prepared(prepared), _captureErrno(captureErrno) {}

    const validate(count : unsigned int) : int {
        if (_symbol.isEmpty() || !_symbol.hasAddress()) return (int) FfiErrorCode::NULL_FUNCTION;
        if (!_prepared.isPrepared()) return (int) FfiErrorCode::INVALID_SIGNATURE;
        if (count != _prepared.getParameterCount()) return (int) FfiErrorCode::ARGUMENT_COUNT;
        return 0;
    }

    const decode(frame : byte*, arguments : const Vector<CValue>&) : Expected<CallResult, FfiError> {
        result : Expected<CallResult, FfiError>;
        status : int = 0;
        declared : int = __k_ffi_result_type(frame, &status);
        if (status != 0 || declared != (int) _prepared.getReturnType()) {
            if (status == 0) status = (int) FfiErrorCode::INVALID_FRAME;
            error : FfiError(status);
            result.setError(error);
            return result;
        }
        canonical : int = __k_ffi_type_canonical(declared);
        kind : ResultKind = ResultKind::UNAVAILABLE;
        signedValue : long = 0L;
        unsignedValue : unsigned long = 0uL;
        floatingValue : double = 0.0;
        pointer : NativePointer;
        if (canonical == 0) {
            kind = ResultKind::VOID;
        } else if (canonical == 1 || canonical == 3 || canonical == 5 || canonical == 7) {
            kind = ResultKind::SIGNED;
            signedValue = __k_ffi_result_signed(frame, &status);
        } else if (canonical == 2 || canonical == 4 || canonical == 6 || canonical == 8 || canonical == 25) {
            kind = ResultKind::UNSIGNED;
            unsignedValue = __k_ffi_result_unsigned(frame, &status);
        } else if (canonical == 9 || canonical == 10) {
            kind = ResultKind::FLOATING;
            floatingValue = __k_ffi_result_float(frame, &status);
        } else if (canonical == 11) {
            kind = ResultKind::POINTER;
            address : byte* = __k_ffi_result_pointer(frame, &status);
            if (status == 0) {
                // A returned pointer may alias an argument, or module storage.
                lease : Shared<PointerResultLease>(new PointerResultLease(_symbol, arguments));
                keepalive : Shared<Object>(lease);
                pointer = NativePointer::retained(address, keepalive);
            }
        } else {
            status = (int) FfiErrorCode::INVALID_TYPE;
        }
        if (status != 0) {
            error : FfiError(status);
            result.setError(error);
            return result;
        }
        errorNumber : int = 0;
        if (_captureErrno) {
            errorNumber = __k_ffi_result_errno(frame, &status);
            if (status != 0) {
                error : FfiError(status);
                result.setError(error);
                return result;
            }
        }
        value : CallResult(kind, signedValue, unsignedValue, floatingValue, pointer, _captureErrno, errorNumber);
        result.setResult(value);
        return result;
    }

    const invokeValues(arguments : const Vector<CValue>&) : Expected<CallResult, FfiError> {
        result : Expected<CallResult, FfiError>;
        status : int = validate(arguments.size());
        if (status != 0) {
            error : FfiError(status);
            result.setError(error);
            return result;
        }
        frame : byte* = __k_ffi_frame_create(_prepared._resource.get()->_handle, &status);
        // All work after acquiring the frame, including diagnostics, may throw.
        try {
            {
            if (status != 0 || frame == null) {
                if (status == 0) status = (int) FfiErrorCode::OUT_OF_MEMORY;
                error : FfiError(status);
                result.setError(error);
                return result;
            }
            i : unsigned int = 0;
            while (i < arguments.size()) {
                value : const CValue& = arguments.get(i);
                if (value._kind == ResultKind::SIGNED) {
                    status = __k_ffi_set_signed(frame, i, (int) value._type, value._signed);
                } else if (value._kind == ResultKind::UNSIGNED) {
                    status = __k_ffi_set_unsigned(frame, i, (int) value._type, value._unsigned);
                } else if (value._kind == ResultKind::FLOATING) {
                    status = __k_ffi_set_float(frame, i, (int) value._type, value._floating);
                } else if (value._kind == ResultKind::POINTER) {
                    status = __k_ffi_set_pointer(frame, i, (int) value._type, value._pointer.rawAddress());
                } else {
                    status = (int) FfiErrorCode::ARGUMENT_TYPE;
                }
                if (status != 0) {
                    error : FfiError(status, (int) i);
                    result.setError(error);
                    return result;
                }
                ++i;
            }
            status = __k_ffi_invoke(_symbol.rawAddress(), frame, arguments.size(), _captureErrno ? 1 : 0);
            if (status != 0) {
                error : FfiError(status);
                result.setError(error);
                return result;
            }
            return decode(frame, arguments);
            }
        } finally {
            __k_ffi_frame_release(frame);
        }
    }

public:
    CFunction() : _symbol(), _prepared(), _captureErrno(false) {}

    CFunction(other : const CFunction&)
        : _symbol(other._symbol), _prepared(other._prepared), _captureErrno(other._captureErrno) {}

    operator =(other : const CFunction&) : CFunction& {
        if (&this != &other) {
            _symbol = other._symbol;
            _prepared = other._prepared;
            _captureErrno = other._captureErrno;
        }
        return this;
    }

    static bind(symbol : const NativeSymbol&, prepared : const CPreparedSignature&, captureErrno : bool = false)
        : Expected<CFunction, FfiError> {
        result : Expected<CFunction, FfiError>;
        if (symbol.isEmpty() || !symbol.hasAddress()) {
            error : FfiError(FfiErrorCode::NULL_FUNCTION);
            result.setError(error);
        } else if (!prepared.isPrepared()) {
            error : FfiError(FfiErrorCode::INVALID_SIGNATURE);
            result.setError(error);
        } else {
            function : CFunction(symbol, prepared, captureErrno);
            result.setResult(function);
        }
        return result;
    }

    const getSymbol() : NativeSymbol { return _symbol; }
    const getSignature() : CSignature { return _prepared.getSignature(); }
    const getPreparedSignature() : CPreparedSignature { return _prepared; }
    const capturesErrno() : bool { return _captureErrno; }

    const invoke(arguments : const CValue[]&) : Expected<CallResult, FfiError> {
        status : int = validate(arguments.size);
        if (status != 0) {
            result : Expected<CallResult, FfiError>;
            error : FfiError(status);
            result.setError(error);
            return result;
        }
        // Copy values and their keepalives before entering foreign code.
        snapshot : Vector<CValue>(arguments);
        return invokeValues(snapshot);
    }

    const call() : CCallBuilder { return CCallBuilder(this); }
}

public final class CCallBuilder {
private:
    _function : CFunction;
    _arguments : Vector<CValue>;

public:
    CCallBuilder(function : const CFunction&) : _function(function), _arguments() {}

    CCallBuilder(other : const CCallBuilder&)
        : _function(other._function), _arguments(other._arguments) {}

    operator =(other : const CCallBuilder&) : CCallBuilder& {
        if (&this != &other) {
            _function = other._function;
            _arguments = other._arguments;
        }
        return this;
    }

    addArgument(value : const CValue&) { _arguments.append(value); }
    reset() { _arguments.clear(); }
    argument(value : const CValue&) : CCallBuilder& {
        addArgument(value);
        return this;
    }
    const getArgumentCount() : unsigned int { return _arguments.size(); }
    const getFunction() : CFunction { return _function; }
    const getArguments() : Vector<CValue> { return _arguments; }

    const invoke() : Expected<CallResult, FfiError> {
        status : int = _function.validate(_arguments.size());
        if (status != 0) {
            result : Expected<CallResult, FfiError>;
            error : FfiError(status);
            result.setError(error);
            return result;
        }
        snapshot : Vector<CValue>(_arguments);
        return _function.invokeValues(snapshot);
    }
}

} // namespace c
} // namespace ffi
