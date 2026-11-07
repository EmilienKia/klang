/*
 * K Language standard library -- crypto capabilities
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
module k::crypto;

private struct NativeDigestCapabilities {}
private struct NativeSymmetricCapabilities {}

@ffi::Extern("C") private __k_cap_digest_new(status: int*) : NativeDigestCapabilities*;
@ffi::Extern("C") private __k_cap_digest_count(catalog: const NativeDigestCapabilities*) : unsigned int;
@ffi::Extern("C") private __k_cap_digest_size(catalog: const NativeDigestCapabilities*,
    index: unsigned int) : unsigned int;
@ffi::Extern("C") private __k_cap_digest_copy(catalog: const NativeDigestCapabilities*,
    index: unsigned int, out: unsigned byte*);
@ffi::Extern("C") private __k_cap_digest_free(catalog: NativeDigestCapabilities*);

@ffi::Extern("C") private __k_cap_symmetric_new(status: int*) : NativeSymmetricCapabilities*;
@ffi::Extern("C") private __k_cap_symmetric_count(catalog: const NativeSymmetricCapabilities*) : unsigned int;
@ffi::Extern("C") private __k_cap_symmetric_size(catalog: const NativeSymmetricCapabilities*,
    index: unsigned int, field: int) : unsigned int;
@ffi::Extern("C") private __k_cap_symmetric_copy(catalog: const NativeSymmetricCapabilities*,
    index: unsigned int, field: int, out: unsigned byte*);
@ffi::Extern("C") private __k_cap_symmetric_value(catalog: const NativeSymmetricCapabilities*,
    index: unsigned int, field: int) : unsigned int;
@ffi::Extern("C") private __k_cap_symmetric_free(catalog: NativeSymmetricCapabilities*);

private capabilityDigestName(catalog: const NativeDigestCapabilities*, index: unsigned int)
        : String throws(CryptoException) {
    size: unsigned int = __k_cap_digest_size(catalog, index);
    if (size == 0xFFFFFFFFu) throw CryptoOperationException();
    bytes: unsigned byte[]! = new unsigned byte[size + 1u];
    __k_cap_digest_copy(catalog, index, &bytes[0u]);
    return String(bytes);
}

private capabilitySymmetricName(catalog: const NativeSymmetricCapabilities*,
        index: unsigned int, field: int) : String throws(CryptoException) {
    size: unsigned int = __k_cap_symmetric_size(catalog, index, field);
    if (size == 0xFFFFFFFFu) throw CryptoOperationException();
    bytes: unsigned byte[]! = new unsigned byte[size + 1u];
    __k_cap_symmetric_copy(catalog, index, field, &bytes[0u]);
    return String(bytes);
}

private capabilityAppendUnique(values: Vector<String>&, value: const String&) {
    i: unsigned int = 0u;
    while (i < values.size()) {
        if (values[i] == value) return;
        ++i;
    }
    values.append(value);
}

/**
 * An exact, available configuration for the full SymmetricCipher.create
 * overload. XTS keyBits is the size of one component key; sizes are in bytes.
 */
public final struct SymmetricCipherConfiguration {
    friend CryptoCapabilities;
private:
    _algorithm: SymmetricAlgorithm;
    _mode: SymmetricMode;
    _padding: SymmetricPadding;
    _keyBits: unsigned int;
    _nonceSize: unsigned int;
    _tagSize: unsigned int;

    SymmetricCipherConfiguration(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
            mode: const SymmetricMode&, nonceSize: unsigned int, tagSize: unsigned int,
            padding: const SymmetricPadding&)
        : _algorithm(algorithm), _mode(mode), _padding(padding), _keyBits(keyBits),
          _nonceSize(nonceSize), _tagSize(tagSize) {}
public:
    SymmetricCipherConfiguration(other: const SymmetricCipherConfiguration&)
        : _algorithm(other._algorithm), _mode(other._mode), _padding(other._padding),
          _keyBits(other._keyBits), _nonceSize(other._nonceSize), _tagSize(other._tagSize) {}

    const algorithm() : SymmetricAlgorithm { return String(_algorithm); }
    const mode() : SymmetricMode { return String(_mode); }
    const padding() : SymmetricPadding { return String(_padding); }
    const keyBits() : unsigned int { return _keyBits; }
    const nonceSize() : unsigned int { return _nonceSize; }
    const tagSize() : unsigned int { return _tagSize; }
}

private struct CapabilityConfigurationNode {
public:
    value: SymmetricCipherConfiguration;
    next: CapabilityConfigurationNode! = null;

    CapabilityConfigurationNode(configuration: const SymmetricCipherConfiguration&)
        : value(configuration) {}
}

private class CapabilityConfigurationIterator : public ConstIterator<SymmetricCipherConfiguration> {
    current: const CapabilityConfigurationNode*;
public:
    CapabilityConfigurationIterator(first: const CapabilityConfigurationNode*) : current(first) {}

    override next() : OptionalConstRef<SymmetricCipherConfiguration> {
        result: OptionalConstRef<SymmetricCipherConfiguration>;
        if (current != null) {
            result.set(current->value);
            current = current->next;
        }
        return result;
    }
}

private class CapabilityConfigurationSequence : public Sequence<SymmetricCipherConfiguration> {
    head: CapabilityConfigurationNode! = null;
    tail: CapabilityConfigurationNode* = null;
public:
    CapabilityConfigurationSequence() -> default;
    CapabilityConfigurationSequence(other: const CapabilityConfigurationSequence&) -> delete;
    operator =(other: const CapabilityConfigurationSequence&) -> delete;

    ~CapabilityConfigurationSequence() {
        while (head != null) {
            next: CapabilityConfigurationNode! = head.next;
            head = next;
        }
        tail = null;
    }

    append(configuration: const SymmetricCipherConfiguration&) {
        node: CapabilityConfigurationNode! = new CapabilityConfigurationNode(configuration);
        if (tail == null) {
            tail = node;
            head = node;
        } else {
            tail->next = node;
            tail = tail->next;
        }
    }

    override const constIterator() : ConstIterator<SymmetricCipherConfiguration>! {
        return new CapabilityConfigurationIterator(head);
    }
}

/**
 * Eager, immutable snapshot of the digest and symmetric capabilities visible
 * at acquisition time. Each returned sequence owns its values independently.
 */
public final class CryptoCapabilities {
private:
    _digests: Vector<String>;
    _configurations: CapabilityConfigurationSequence;

    CryptoCapabilities() -> default;
    CryptoCapabilities(other: const CryptoCapabilities&) -> delete;
    operator =(other: const CryptoCapabilities&) -> delete;

public:
    static snapshot() : CryptoCapabilities! throws(CryptoException) {
        result: CryptoCapabilities! = new CryptoCapabilities();
        digests: NativeDigestCapabilities* = null;
        symmetric: NativeSymmetricCapabilities* = null;
        try {
            status: int = 0;
            digests = __k_cap_digest_new(&status);
            if (status != 0 || digests == null) throw CryptoOperationException();
            status = 0;
            symmetric = __k_cap_symmetric_new(&status);
            if (status != 0 || symmetric == null) throw CryptoOperationException();

            count: unsigned int = __k_cap_digest_count(digests);
            i: unsigned int = 0u;
            while (i < count) {
                name: String = capabilityDigestName(digests, i);
                capabilityAppendUnique(result._digests, name);
                ++i;
            }

            count = __k_cap_symmetric_count(symmetric);
            i = 0u;
            while (i < count) {
                algorithm: String = capabilitySymmetricName(symmetric, i, 0);
                mode: String = capabilitySymmetricName(symmetric, i, 1);
                padding: String = capabilitySymmetricName(symmetric, i, 2);
                keyBits: unsigned int = __k_cap_symmetric_value(symmetric, i, 0);
                nonceSize: unsigned int = __k_cap_symmetric_value(symmetric, i, 1);
                tagSize: unsigned int = __k_cap_symmetric_value(symmetric, i, 2);
                configuration: SymmetricCipherConfiguration = SymmetricCipherConfiguration(
                    algorithm, keyBits, mode, nonceSize, tagSize, padding);
                result._configurations.append(configuration);
                ++i;
            }
            return result;
        } finally {
            if (symmetric != null) __k_cap_symmetric_free(symmetric);
            if (digests != null) __k_cap_digest_free(digests);
        }
    }

    const digestAlgorithms() : Sequence<MessageDigestAlgorithm>! {
        result: Vector<String>! = new Vector<String>();
        i: unsigned int = 0u;
        while (i < _digests.size()) {
            result.append(_digests[i]);
            ++i;
        }
        return result;
    }

    const symmetricAlgorithms() : Sequence<SymmetricAlgorithm>! {
        result: Vector<String>! = new Vector<String>();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            name: String = current.get().algorithm();
            capabilityAppendUnique(result, name);
            current = it.next();
        }
        return result;
    }

    const symmetricModes() : Sequence<SymmetricMode>! {
        result: Vector<String>! = new Vector<String>();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            name: String = current.get().mode();
            capabilityAppendUnique(result, name);
            current = it.next();
        }
        return result;
    }

    const symmetricModes(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int)
            : Sequence<SymmetricMode>! {
        result: Vector<String>! = new Vector<String>();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            configuration: const SymmetricCipherConfiguration& = current.get();
            if (configuration.algorithm() == algorithm && configuration.keyBits() == keyBits) {
                name: String = configuration.mode();
                capabilityAppendUnique(result, name);
            }
            current = it.next();
        }
        return result;
    }

    const symmetricPaddings() : Sequence<SymmetricPadding>! {
        result: Vector<String>! = new Vector<String>();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            name: String = current.get().padding();
            capabilityAppendUnique(result, name);
            current = it.next();
        }
        return result;
    }

    const symmetricPaddings(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
            mode: const SymmetricMode&) : Sequence<SymmetricPadding>! {
        result: Vector<String>! = new Vector<String>();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            configuration: const SymmetricCipherConfiguration& = current.get();
            if (configuration.algorithm() == algorithm && configuration.keyBits() == keyBits &&
                    configuration.mode() == mode) {
                name: String = configuration.padding();
                capabilityAppendUnique(result, name);
            }
            current = it.next();
        }
        return result;
    }

    const symmetricConfigurations() : Sequence<SymmetricCipherConfiguration>! {
        result: CapabilityConfigurationSequence! = new CapabilityConfigurationSequence();
        it: ConstIterator<SymmetricCipherConfiguration>! = _configurations.constIterator();
        current: OptionalConstRef<SymmetricCipherConfiguration> = it.next();
        while (current.hasValue()) {
            result.append(current.get());
            current = it.next();
        }
        return result;
    }
}
