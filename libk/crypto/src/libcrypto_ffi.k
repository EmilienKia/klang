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
/*
 * FFI bindings for OpenSSL libcrypto functions.
 *
 * This file provides foreign function interface (FFI) bindings to the OpenSSL libcrypto library.
 * It allows K code to call native C functions from the OpenSSL library for cryptographic operations.
 */


@ffi::Extern("C", null, "OpenSSL_version_num")
private __openSslVersionNumber() : unsigned long;

private struct ENGINE {}
private struct EVP_MD {}
private struct EVP_MD_CTX {}

@ffi::Extern("C")
private __k_EVP_MD_fetch(name : const unsigned byte*) : EVP_MD*;
@ffi::Extern("C")
private EVP_MD_free(md : EVP_MD*);

@ffi::Extern("C")
private EVP_md5() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha1() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha224() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha256() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha384() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha512() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha512_224() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha512_256() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha3_224() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha3_256() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha3_384() : EVP_MD*;
@ffi::Extern("C")
private EVP_sha3_512() : EVP_MD*;

@ffi::Extern("C")
private EVP_MD_CTX_new() : EVP_MD_CTX*;
@ffi::Extern("C")
private EVP_MD_CTX_free(ctx : EVP_MD_CTX*);
@ffi::Extern("C")
private EVP_DigestInit_ex(ctx : EVP_MD_CTX*, md : EVP_MD*, engine : ENGINE*) : int;
@ffi::Extern("C")
private EVP_DigestUpdate(ctx : EVP_MD_CTX*, data : unsigned byte*, len : unsigned int) : int;
@ffi::Extern("C", null, "__k_EVP_MD_CTX_get_size")
private EVP_MD_CTX_get_size(ctx : EVP_MD_CTX*) : int; // Wrapped cause inlined in OpenSSL's libcrypto.
@ffi::Extern("C")
private EVP_DigestFinal_ex(ctx : EVP_MD_CTX*, md : unsigned byte*, len : unsigned int*) : int;