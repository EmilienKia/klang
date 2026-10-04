/*
 * Shared immutable network addresses. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
module k;
namespace io {
public struct NativeNetworkAddress {}

/** IPv4 endpoint, unresolved hostname, or Unix pathname. Copies share storage. */
public class NetworkAddress {
private:
    _native : NativeNetworkAddress*;
    @ffi::Extern("C") private static __k_io_address_create(endpoint: const char*,
        port: int, unixSocket: int) : NativeNetworkAddress*;
    @ffi::Extern("C") private static __k_io_address_retain(address: NativeNetworkAddress*);
    @ffi::Extern("C") private static __k_io_address_release(address: NativeNetworkAddress*);
    @ffi::Extern("C") private static __k_io_address_host(address: const NativeNetworkAddress*) : const char*;
    @ffi::Extern("C") private static __k_io_address_host_length(address: const NativeNetworkAddress*) : int;
    @ffi::Extern("C") private static __k_io_address_copy_host(address: const NativeNetworkAddress*,
        destination: char*, capacity: int) : int;
    @ffi::Extern("C") private static __k_io_address_port(address: const NativeNetworkAddress*) : int;
    @ffi::Extern("C") private static __k_io_address_family(address: const NativeNetworkAddress*) : int;
    @ffi::Extern("C") private static __k_io_address_resolved(address: const NativeNetworkAddress*) : int;
    @ffi::Extern("C") private static __k_io_setup_errno() : int;
    NetworkAddress(native: NativeNetworkAddress*) : _native(native) {
        if (_native == null) {
            error : IoError = IoError::fromErrno(__k_io_setup_errno(), 0L);
            error.raiseSync();
        }
    }
    friend DatagramSocket;
    friend DatagramReceive;
public:
    NetworkAddress() : _native(null) {}
    NetworkAddress(host: const char[], port: int) : _native(null) {
        length : int = host.size;
        if (length > 0 && host[length - 1] == (char) 0) --length;
        text : char[]! = new char[length + 1];
        i : int = 0;
        while (i < length) {
            if (host[i] == (char) 0) {
                error : IoError = IoError::fromErrno(22, 0L); error.raiseSync();
            }
            text[i] = host[i]; ++i;
        }
        text[length] = (char) 0;
        _native = __k_io_address_create(&text[0], port, 0);
        if (_native == null) {
            error : IoError = IoError::fromErrno(__k_io_setup_errno(), 0L);
            error.raiseSync();
        }
    }
    NetworkAddress(other: const NetworkAddress&) : _native(other._native) {
        __k_io_address_retain(_native);
    }
    operator =(other: const NetworkAddress&) : NetworkAddress& {
        address : NativeNetworkAddress* = other._native;
        __k_io_address_retain(address);
        __k_io_address_release(_native);
        _native = address;
        return this;
    }
    ~NetworkAddress() { __k_io_address_release(_native); _native = null; }
    static of(host: const char[], port: int) : NetworkAddress! { return new NetworkAddress(host, port); }
    static ofName(host: const char[], port: int) : NetworkAddress! { return NetworkAddress::of(host, port); }
    static loopback(port: int) : NetworkAddress! { return new NetworkAddress("127.0.0.1", port); }
    static any(port: int) : NetworkAddress! { return new NetworkAddress("*", port); }
    static unix(path: const file::Path&) : NetworkAddress {
        return NetworkAddress(__k_io_address_create(path.nativePath(), 0, 1));
    }
    static unix(path: const char[]) : NetworkAddress {
        endpoint : file::Path(path);
        return NetworkAddress::unix(endpoint);
    }
    const native() : NativeNetworkAddress* { return _native; }
    const nativeHost() : const char* { return __k_io_address_host(_native); }
    const port() : int { return __k_io_address_port(_native); }
    /** 4 for IPv4, 1 for Unix, 0 for an empty value. */
    const family() : int { return __k_io_address_family(_native); }
    const isResolved() : bool { return __k_io_address_resolved(_native) != 0; }
    const host() : char[]! {
        length : int = __k_io_address_host_length(_native);
        text : char[]! = new char[length + 1];
        copied : int = __k_io_address_copy_host(_native, &text[0], length + 1);
        if (copied < 0) {
            delete text;
            error : IoError = IoError::fromErrno(-copied, 0L);
            error.raiseSync();
        }
        return text;
    }
}
}
