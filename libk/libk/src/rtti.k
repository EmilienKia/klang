/*
 * K Language standard library — base module
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

/**
 * Base RTTI type information descriptor.
 * All type descriptors will implement this interface.
 */
const interface TypeInfo {
    /**
     * Return the name of the type.
     * This name is the same as the name of the type in the source code without any context qualification.
     */
    const getName() : const char[]?;
}

/**
 * RTTI class descriptor.
 * This class is used to describe classes at runtime.
 */
final const class Class : TypeInfo {
    name : const char[]?;

    private Class(name : const char[]?) : name(name) {}

    /**
     * Return the name of the class.
     */
    const getName() : const char[]? {
        return name;
    }
}

