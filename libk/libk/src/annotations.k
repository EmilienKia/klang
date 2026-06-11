/*
 * K Language standard library -- base module
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

namespace annotations {

/**
 * Specify how long an annotation instance is retained.
 *
 * Apply `@Retention` to an annotation type to control whether instances of
 * that annotation remain available only during compilation (`SOURCE`) or are
 * also emitted into runtime metadata (`RUNTIME`).
 *
 * If `@Retention` is omitted on an annotation type, the default policy is
 * `RUNTIME`.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Retention {
    /**
     * Enumerate the supported retention policies.
     *
     * `SOURCE` keeps the annotation only in the compiler model.
     * `RUNTIME` keeps the annotation in emitted runtime metadata.
     */
    enum Policy {
        SOURCE;
        RUNTIME;
    }

    /**
     * Define the retention policy for the annotated annotation type.
     */
    policy : Policy = Policy::RUNTIME;
}

/**
 * Mark an annotation type as inheritable by subclasses.
 *
 * When `@Inherited` is applied to an annotation type, subclasses implicitly
 * inherit that annotation from their base class unless they provide their own
 * explicit instance of the same annotation type.
 *
 * This rule applies to class inheritance only, not to interfaces.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Inherited {
}

/**
 * Restrict the element kinds on which an annotation may appear.
 *
 * When `@Target` is present on an annotation type, the compiler enforces that
 * the annotation may only be applied to the listed element kinds.
 *
 * If `@Target` is absent, the annotation may be applied to any supported
 * element kind.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Target {
    /**
     * Enumerate the program element kinds that may carry annotations.
     */
    enum ElementType {
        CLASS;
        INTERFACE;
        ANNOTATION;
        FUNCTION;
        CONSTRUCTOR;
        PARAMETER;
    }

    /**
     * List the element kinds on which the annotated annotation type is allowed.
     */
    value : ElementType[];
}

/**
 * Mark a function or constructor as compiler intrinsic.
 *
 * When `@Intrinsic` is applied, the compiler skips normal body generation and
 * dispatches to a built-in code-generation path identified by `name`.
 *
 * This annotation is source-only and is not exported in KDI or RTTI.
 */
@Retention(Policy::SOURCE)
@Target({ElementType::FUNCTION, ElementType::CONSTRUCTOR})
annotation Intrinsic {
    /**
     * Identify the compiler intrinsic implementation to use.
     */
    name : const char[];
}

} // namespace annotations
