/*
 * K Language standard library — RTTI C runtime helpers
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

/**
 * __k_object_get_class — Extract the RTTI Class reference from an object.
 *
 * In K, every class instance starts with a vptr (field 0) that points to its
 * vtable.  The vtable's slot 0 contains a pointer to the RTTI global, which
 * is a genuine ::k::Class instance.
 *
 * In K's runtime model, all references and views are materialized as void*.
 * This function dereferences the vptr chain and returns the ::k::Class pointer.
 *
 * @param obj  Pointer to the K object (the 'this' reference, passed as void*).
 * @return     Pointer to the ::k::Class instance (RTTI global).
 */
void* __k_object_get_class(void* obj) {
    /* obj -> field 0 = vptr (pointer to vtable) */
    void** vptr = *(void***)obj;
    /* vtable slot 0 = pointer to RTTI global (::k::Class instance) */
    return vptr[0];
}


