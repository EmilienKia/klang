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

/*
 * Meta-annotation that specifies the retention policy of the annotated
 * annotation type.
 *
 * When applied to an annotation type, @Retention controls whether instances
 * of that annotation are kept only in the compiler's internal model (SOURCE)
 * or are also synthesized into the binary output as RTTI metadata (RUNTIME).
 *
 * If @Retention is not present on an annotation type, the default policy
 * is RUNTIME.
 *
 * Equivalent in purpose to java.lang.annotation.Retention.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Retention {
    /*
     * Describes where the annotation data is retained.
     *
     * SOURCE  -- The annotation is kept in the compiler model / AST only.
     *            It is NOT emitted into the generated binary (no RTTI constant).
     *            Useful for compile-time-only checks (e.g. @Target itself).
     *
     * RUNTIME -- The annotation is retained in the binary as an RTTI constant
     *            global, accessible via reflection at runtime.  This is the
     *            default policy when @Retention is absent.
     */
    enum Policy {
        SOURCE;
        RUNTIME;
    };

    /* The retention policy for the annotated annotation type. */
    policy : Policy = Policy::RUNTIME;
}

/*
 * Meta-annotation indicating that the annotated annotation type is
 * automatically inherited by subclasses.
 *
 * When a class is annotated with an @Inherited annotation, every subclass
 * of that class implicitly carries the same annotation instance -- unless
 * the subclass provides its own explicit instance of the same annotation
 * type, which then overrides the inherited one.
 *
 * @Inherited has no effect on interfaces; only class inheritance propagates
 * annotations.
 *
 * Equivalent in purpose to java.lang.annotation.Inherited.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Inherited {
}

/*
 * Meta-annotation that restricts the kinds of program elements to which the
 * annotated annotation type may be applied.
 *
 * When @Target is present on an annotation type, the compiler enforces that
 * the annotation may only appear on the listed element types.  Applying it
 * to an unlisted element type results in a compile-time error.
 *
 * If @Target is absent, the annotation may be applied to any element type.
 * If @Target is present but the value array is empty, the annotation cannot
 * be applied anywhere -- this is a compile-time error.
 *
 * Equivalent in purpose to java.lang.annotation.Target.
 */
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Target {
    /*
     * Enumerates the kinds of program elements that can carry annotations.
     *
     * CLASS      -- a class declaration (keyword 'class').
     * INTERFACE  -- an interface declaration (keyword 'interface').
     * ANNOTATION -- an annotation type declaration (keyword 'annotation').
     * FUNCTION   -- a function declaration (member or free function).
     * CONSTRUCTOR -- a constructor declaration.
     * PARAMETER  -- a function or constructor parameter.
     */
    enum ElementType {
        CLASS;
        INTERFACE;
        ANNOTATION;
        FUNCTION;
        CONSTRUCTOR;
        PARAMETER;
    };

    /* The set of element types on which the annotated annotation is allowed. */
    value : ElementType[];
}

} // namespace annotations
