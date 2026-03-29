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

/**
 * __k_annotation_get_type — Extract the RTTI AnnotationType reference from an annotation.
 *
 * In K, every annotation instance starts with a vptr (field 0) that points to
 * its vtable.  The vtable's slot 0 contains a pointer to the RTTI global, which
 * is a genuine ::k::AnnotationType instance.
 *
 * @param ann  Pointer to the K annotation instance (the 'this' reference, passed as void*).
 * @return     Pointer to the ::k::AnnotationType instance (RTTI global).
 */
void* __k_annotation_get_type(void* ann) {
    /* ann -> field 0 = vptr (pointer to vtable) */
    void** vptr = *(void***)ann;
    /* vtable slot 0 = pointer to RTTI global (::k::AnnotationType instance) */
    return vptr[0];
}

/**
 * __k_class_get_functions — Extract the functions array from a Class RTTI global.
 *
 * In the RTTI struct layout, the functions array is stored at field index 10
 * (after the 3 vptrs, name, fullName, bases, nested, enclosing, flags, annotations).
 * The struct layout is:
 *   { ptr vptr, ptr vptr_at, ptr vptr_ti, ptr name, ptr fullName,
 *     ptr bases, ptr nested, ptr enclosing, i32 flags, ptr annotations, ptr functions }
 *
 * IMPORTANT: this function takes a Class& (not AggregateType&) so that the K
 * compiler passes a pointer to the START of the RTTI struct, without any
 * base-class pointer adjustment.  If we used AggregateType&, the pointer
 * would be shifted by +8 bytes to the AggregateType sub-object, causing
 * fields[10] to read past the end of the struct.
 *
 * On 64-bit systems, fields 0-7 are ptrs (64 bytes), field 8 is i32 with
 * 4 bytes of padding (total 8 bytes), field 9 is ptr (8 bytes), field 10
 * is ptr (8 bytes).  Field 10 is at byte offset 80 = 10 * sizeof(void*).
 *
 * @param rtti  Pointer to the Class RTTI global (start of struct).
 * @return      Pointer to the functions K-array (Function?[]?), or null.
 */
void* __k_class_get_functions(void* rtti) {
    if (!rtti) return (void*)0;
    void** fields = (void**)rtti;
    /* Field 10 = functions (after 3 vptrs + name + fullName + bases + nested + enclosing + flags_slot + annotations) */
    return fields[10];
}

/**
 * __k_iface_get_functions — Extract the functions array from an Interface RTTI global.
 *
 * Same logic and layout as __k_class_get_functions, but declared separately
 * so that the K extern signature takes Interface& (avoiding AggregateType&
 * pointer adjustment).
 *
 * @param rtti  Pointer to the Interface RTTI global (start of struct).
 * @return      Pointer to the functions K-array (Function?[]?), or null.
 */
void* __k_iface_get_functions(void* rtti) {
    if (!rtti) return (void*)0;
    void** fields = (void**)rtti;
    return fields[10];
}
