/*
 * K Language standard library -- crypto module
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
module k::crypto;

/**
 * Base checked exception for cryptographic operation failures.
 */
public class CryptoException : Exception {
public:
    CryptoException() : Exception(600) {}
    CryptoException(code: int) : Exception(code) {}
    CryptoException(code: int, cause: Throwable?) : Exception(code, cause) {}
}

/**
 * Thrown when a message digest operation fails.
 */
public class MessageDigestException : CryptoException {
public:
    MessageDigestException() : CryptoException(601) {}
    MessageDigestException(code: int) : CryptoException(code) {}
    MessageDigestException(code: int, cause: Throwable?) : CryptoException(code, cause) {}
}

/**
 *  Marker interface for all crypto objects.
 *
 * This interface is used to identify objects that are part of the crypto module.
 * It does not define any methods or properties, but serves as a common type for all crypto-related classes and interfaces.
 */
interface CryptoObject {
}

/**
 * Return the version number reported by the linked OpenSSL libcrypto.
 */
openSslVersionNumber() : unsigned long {
    return __openSslVersionNumber();
}
