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
 * Base interface for all cryptographic keys.
 */
interface Key : CryptoObject {
}

/**
 * Interface representing a public key in asymmetric cryptography.
 */
interface PublicKey : Key {
}

/**
 * Interface representing a private key in asymmetric cryptography.
 */
interface PrivateKey : Key {
}
