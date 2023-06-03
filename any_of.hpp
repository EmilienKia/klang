// Copyright 2022, Emilien KIA
// https://github.com/EmilienKia/any_of
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef __ANY_OF_HPP__
#define __ANY_OF_HPP__

#include <optional>
#include <tuple>
#include <variant>

#ifndef ANY_OF_NS
#define ANY_OF_NS anyof
#endif // ANY_OF_NS

namespace ANY_OF_NS {

    template<typename Base, typename ...Types>
    class any_of;

    template<typename Base, typename ...Types>
    class any_of_opt;

} // ANY_OF_NS

namespace std {

    template<class T, class Base, class... Types >
    constexpr bool holds_alternative(const ::ANY_OF_NS::any_of<Base, Types...>& v) noexcept;

    template<class T, class Base, class... Types >
    constexpr bool holds_alternative(const ::ANY_OF_NS::any_of_opt<Base, Types...>& v) noexcept;

}



namespace ANY_OF_NS {

    /**
     * @brief Non-nillable strong union type.
     * 
     * any_of is a non-nillable strong union type. It acts as an C/C++ union or C++ std::variant but with data member
     * which must derive from a same common type.
     * any_of is able to embed one instance of a list of types, all these types shall be or override a common type.
     * The embedded object can be directly accessed with the base common type without having to write any casting,
     * type reinterpretation nor visiting helper.
     * 
     * @tparam Base The base common type with which embed object can be directly accessed.
     * @tparam ...Types List of concrete types the union can embed. They must all derive (or be) from the Base type.
    */
    template<typename Base, typename ...Types>
    class any_of {
//        static_assert((std::is_base_of_v<Base, Types> && ...), "any_of must hold inherited Types from Base type");

    public:
        using self_t = any_of<Base, Types...>;
        using variant_t = std::variant<Types...>;
        using types_t = std::tuple<Types...>;

        using any_of_opt_t = any_of_opt<Base, Types...>;


    protected:
        template<class T, class HABase, class... HATypes>
        friend constexpr bool std::holds_alternative(const ::ANY_OF_NS::any_of<HABase, HATypes...>& v) noexcept;

        template<typename, typename ...>
        friend
        class any_of;

        template<typename, typename ...>
        friend
        class any_of_opt;

        using first_type_t = typename std::tuple_element<0, types_t>::type;

        variant_t _variant;

    public:
        /**
         * @brief Default constructor.
         * Default constructor, initializing the any_of with the default constructor of the first embed type.
         */
        constexpr any_of() noexcept(std::is_nothrow_default_constructible<first_type_t>::value) = default;

        /**
         * @brief Copy constructor.
         * @param other Other any_of object from which copy the state and value.
         */
        constexpr any_of(const self_t& other) : _variant(other._variant) {}

        /**
         * @brief Move constructor.
         * @param other Other any_of object from which move the state and value.
         */
        constexpr any_of(self_t&& other) noexcept(std::is_nothrow_move_constructible<variant_t>::value): _variant(
                std::move(other._variant)) {}

        ~any_of() = default;

        /**
         * @brief Converting copy constructor.
         * Converting constructor, initializing by copying the any_of with the given alternative object.
         * @tparam T Type of the alternative to construct with.
         * @param t Alternative object to copy.
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        explicit constexpr any_of(const T& t) noexcept(std::is_nothrow_constructible<variant_t>::value) : _variant(t) {}

        /**
         * @brief Converting move constructor.
         * Converting constructor, initializing by moving the any_of with the given alternative object.
         * @tparam T Type of the alternative to construct with.
         * @param t Alternative object to copy.
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        explicit constexpr any_of(T&& t) noexcept(std::is_nothrow_constructible<variant_t>::value) : _variant(std::forward<T>(t)) {}

        /**
         * @brief Conversion constructor.
         * Conversion constructor from an any_of holding other types. Useful only when both any_of types embed at least one common type.
         * @tparam OtherBase Base type of other any_of.
         * @tparam OtherTypes Type list of other any_of.
         * @param other The other any_of to construct from.
         * @throws std::bad_variant_access If the type of object hold by other any_of is not supported.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        any_of(const any_of<OtherBase, OtherTypes...>& other) : _variant(std::visit([](auto&& arg) -> variant_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr ((std::is_same_v<T, Types> || ...)) {
                return variant_t(arg);
            } else {
                throw std::bad_variant_access{};
            }
        }, other._variant)) {
        }

        /**
         * @brief Conversion constructor.
         * Conversion constructor from an any_of_opt holding other types. Useful only when both any_of types embed at least one common type.
         * @tparam OtherBase Base type of other any_of.
         * @tparam OtherTypes Type list of other any_of.
         * @param other The other any_of to construct from.
         * @throws std::bad_variant_access If the type of object hold by other any_of is not supported or if other doesn't hold any value.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        any_of(const any_of_opt<OtherBase, OtherTypes...>& other) : _variant(std::visit([](auto&& arg) -> variant_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                throw std::bad_variant_access{};
            } else if constexpr ((std::is_same_v<T, Types> || ...)) {
                return variant_t(arg);
            } else {
                throw std::bad_variant_access{};
            }
        }, other._variant)) {
        }


        // TODO Add in-place constructors

        /**
         * @brief Copy assignment
         * Assign the content of other any_of by copying its value.
         * @param other Other any_of to copy.
         * @return *this
         */
        constexpr self_t& operator=(const self_t& other) {
            _variant = other._variant;
            return *this;
        }

        /**
         * @brief Move assignment.
         * Assign the content of the other any_of by moving its value.
         * @param other Other any_of to move.
         * @return *this
         */
        constexpr self_t& operator=(self_t&& other) noexcept(std::is_nothrow_move_assignable<variant_t>::value) {
            _variant = std::forward<self_t::variant_t>(other._variant);
            return *this;
        }

        /**
         * Conversion copy assignment.
         * Assign the other as any_of content by copying it.
         * @tparam T Type of object to copy.
         * @param t Object to copy.
         * @return *this
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        constexpr self_t& operator=(const T& t) {
            _variant = t;
            return *this;
        }

        /**
         * Conversion move assignment.
         * Assign the other as any_of content by moving it.
         * @tparam T Type of object to move.
         * @param t Object to move.
         * @return *this
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        constexpr self_t& operator=(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value) {
            _variant = t;
            return *this;
        }

        /**
         * Conversion assignment.
         * Assign the content of an any_of containing other types. Useful only when both any_of types embed at least one common type.
         * @tparam OtherBase Base type of other any_of.
         * @tparam OtherTypes Type list of other any_of.
         * @param other The other any_of to construct from.
         * @return *this
         * @throws std::bad_variant_access If the type of object hold by other any_of is not supported or if other doesn't hold any value.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        self_t& operator=(const any_of<OtherBase, OtherTypes...>& other) {
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr ((std::is_same_v<T, Types> || ...)) {
                    this->operator=(arg);
                } else {
                    throw std::bad_variant_access{};
                }
            }, other._variant);
            return *this;
        }

        /**
         * @brief Index of the held alternative.
         * Returns the zero-based index of the alternative that is currently held.
         * @return Zero-based index of the held alternative, npos if valueless_by_exception.
         */
        constexpr std::size_t index() const noexcept {
            return _variant.index();
        }

        /**
         * @brief Check if the any_of is not holding a value.
         * Returns true if, and only if, the any_of doesn't held any value. Can occur only when a constructor fails and throw an exception. 
         * @return true if it doesn't held any value, false otherwise.
         */
        constexpr bool valueless_by_exception() const noexcept {
            return _variant.valueless_by_exception();
        }

        /**
         * @brief Emplace a new value.
         * @tparam T Type of the new value to emplace.
         * @tparam ...Args Argument types of emplaced object constructo parameters.
         * @param ...args Arguments to forward to constructor of object to emplace.
         * @return Reference to emplaced object.
         */
        template<class T, class... Args>
        constexpr T& emplace(Args&& ... args) {
            return _variant.template emplace<T>(args...);
        }

        // TODO Add initializer-list and variant-alternative emplaces

        /**
         * @brief Swap two any_of.
         * @param rhs Other any_of to swap with.
         */
        constexpr void swap(self_t& rhs) noexcept(((std::is_nothrow_move_constructible_v<Types> &&
                                                    std::is_nothrow_swappable_v<Types>) && ...)) {
            _variant.swap(rhs._variant);
        }


        //
        // Specific interface:
        //

        /**
         * @brief Common type accessor.
         * Access to the held object throw the common type.
         * @return Reference to the held object.
         */
        Base& value() {
            return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Reference to the held object.
         */
        const Base& value() const {
            return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Pointer to the held object.
         */
        Base* operator->() {
            return &value();
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Pointer to the held object.
         */
        const Base* operator->() const {
            return &value();
        }

        /**
         * @brief Test the type of the held object.
         * @tparam Type Type to test.
         * @return True if the held object is of the tested type, false otherwise.
         */
        template<typename Type>
        constexpr bool is() const noexcept;

        /**
         * @brief Type-based value accessor.
         * Return the reference to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Reference of the held value, if of the requested type.
         * @throw std::bad_variant_access if held value is not of the requested type.
         */
        template<class Type>
        constexpr Type& get() {
            return std::get<Type>(_variant);
        }

        /**
         * @brief Type-based value accessor.
         * Return the reference to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Reference of the held value, if of the requested type.
         * @throw std::bad_variant_access if held value is not of the requested type.
         */
        template<class Type>
        constexpr const Type& get() const {
            return std::get<Type>(_variant);
        }

        /**
         * @brief Type-based non-throwing accessor.
         * Return a pointer to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Pointer to the held value, if of the requested type, nullptr otherwise.
         */
        template<class Type>
        constexpr std::add_pointer_t<Type> get_if() noexcept {
            return std::get_if<Type, Types...>(&_variant);
        }

        /**
         * @brief Type-based non-throwing accessor.
         * Return a pointer to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Pointer to the held value, if of the requested type, nullptr otherwise.
         */
        template<class Type>
        constexpr std::add_pointer_t<const Type> get_if() const noexcept {
            return std::get_if<const Type, Types...>(&_variant);
        }

    };

    /**
     * @brief Nillable strong union type.
     *
     * any_of_opt is a nillable strong union type. It acts as an C/C++ union or C++ std::variant but with data member
     * which must derive from a same common type.
     * any_of_opt is able to embed one instance of a list of types, all these types shall be or override a common type.
     * The embedded object can be directly accessed with the base common type without having to write any casting,
     * type reinterpretation nor visiting helper.
     *
     * @tparam Base The base common type with which embed object can be directly accessed.
     * @tparam ...Types List of concrete types the union can embed. They must all derive (or be) from the Base type.
    */
    template<typename Base, typename ...Types>
    class any_of_opt {
//        static_assert((std::is_base_of_v<Base, Types> && ...), "any_of_opt must hold inherited Types from Base type");

    public:
        using self_t = any_of_opt<Base, Types...>;
        using variant_t = std::variant<std::monostate, Types...>;
        using types_t = std::tuple<std::monostate, Types...>;

        using any_of_t = any_of<Base, Types...>;

        static constexpr size_t npos = -1;

    protected:
        template<class T, class HABase, class... HATypes>
        friend constexpr bool std::holds_alternative(const ::ANY_OF_NS::any_of_opt<HABase, HATypes...>& v) noexcept;

        template<typename, typename ...>
        friend
        class any_of_opt;

        template<typename, typename ...>
        friend
        class any_of;

        using first_type_t = typename std::tuple_element<0, types_t>::type;

        variant_t _variant;

    public:
        /**
         * @brief Default constructor.
         * Default constructor, initializing the any_of_opt with the nil state.
         */
        constexpr any_of_opt() noexcept/* () Unconditionnaly noexcept because monostate default construct */ = default;

        /**
         * @brief Copy constructor.
         * @param other Other any_of_opt object from which copy the state and value.
         */
        constexpr any_of_opt(const self_t& other) : _variant(other._variant) {}

        /**
         * @brief Move constructor.
         * @param other Other any_of_opt object from which move the state and value.
         */
        constexpr any_of_opt(self_t&& other) noexcept(std::is_nothrow_move_constructible<variant_t>::value): _variant(
                std::move(other._variant)) {}

        ~any_of_opt() = default;

        /**
         * @brief Converting copy constructor.
         * Converting constructor, initializing by copying the any_of_opt with the given alternative object.
         * @tparam T Type of the alternative to construct with.
         * @param t Alternative object to copy.
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        explicit constexpr
        any_of_opt(const T& t) noexcept(std::is_nothrow_constructible<variant_t>::value) : _variant(t) {}

        /**
         * @brief Converting move constructor.
         * Converting constructor, initializing by moving the any_of_opt with the given alternative object.
         * @tparam T Type of the alternative to construct with.
         * @param t Alternative object to copy.
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        explicit constexpr
        any_of_opt(T&& t) noexcept(std::is_nothrow_constructible<variant_t>::value) : _variant(std::forward<T>(t)) {}

        /**
         * @brief Nil state constructor.
         * Construct an empty any_of_opt by explicitly pass a nullptr.
         * @param A nullptr.
        */
        any_of_opt(std::nullptr_t) noexcept: any_of_opt() {}

        /**
         * @brief Conversion constructor.
         * Conversion constructor from an any_of holding other types. Useful only when both any_of_opt types embed at least one common type.
         * @tparam OtherBase Base type of other any_of_opt.
         * @tparam OtherTypes Type list of other any_of_opt.
         * @param other The other any_of to construct from.
         * @throws std::bad_variant_access If the type of object hold by other any_of_opt is not supported.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        any_of_opt(const any_of_opt<OtherBase, OtherTypes...>& other) : _variant(
                std::visit([](auto&& arg) -> variant_t {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        return variant_t();
                    } else if constexpr ((std::is_same_v<T, Types> || ...)) {
                        return variant_t(arg);
                    } else {
                        throw std::bad_variant_access{};
                    }
                }, other._variant)) {
        }

        /**
         * @brief Conversion constructor.
         * Conversion constructor from an any_of_opt holding other types. Useful only when both any_of_opt types embed at least one common type.
         * @tparam OtherBase Base type of other any_of_opt.
         * @tparam OtherTypes Type list of other any_of_opt.
         * @param other The other any_of_opt to construct from.
         * @throws std::bad_variant_access If the type of object hold by other any_of_opt is not supported or if other doesn't hold any value.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        any_of_opt(const any_of<OtherBase, OtherTypes...>& other) : _variant(std::visit([](auto&& arg) -> variant_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr ((std::is_same_v<T, Types> || ...)) {
                return variant_t(arg);
            } else {
                return variant_t{};
            }
        }, other._variant)) {
        }

        // TODO Add in-place constructors

        /**
         * @brief Copy assignment
         * Assign the content of other any_of_opt by copying its value.
         * @param other Other any_of_opt to copy.
         * @return *this
         */
        constexpr self_t& operator=(const self_t& other) {
            _variant = other._variant;
            return *this;
        }

        /**
         * @brief Move assignment.
         * Assign the content of the other any_of_opt by moving its value.
         * @param other Other any_of_opt to move.
         * @return *this
         */
        constexpr self_t& operator=(self_t&& other) noexcept(std::is_nothrow_move_assignable<variant_t>::value) {
            _variant = std::forward<self_t::variant_t>(other._variant);
            return *this;
        }

        /**
         * @brief Nil assignment.
         * Assign the nil state to this any_of_opt.
         * @param A nullptr.
         * @return *this
        */
        constexpr self_t& operator=(std::nullptr_t) noexcept {
            _variant = {};
            return *this;
        }

        /**
         * Conversion copy assignment.
         * Assign the other as any_of_opt content by copying it.
         * @tparam T Type of object to copy.
         * @param t Object to copy.
         * @return *this
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        constexpr self_t& operator=(const T& t) {
            _variant = t;
            return *this;
        }

        /**
         * Conversion move assignment.
         * Assign the other as any_of_opt content by moving it.
         * @tparam T Type of object to move.
         * @param t Object to move.
         * @return *this
         */
        template<typename T, std::enable_if_t<((std::is_same_v<T, Types>) || ...), bool> = true>
        constexpr self_t& operator=(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value) {
            _variant = t;
            return *this;
        }

        /**
         * Conversion assignment.
         * Assign the content of an any_of_opt containing other types. Useful only when both any_of_opt types embed at least one common type.
         * @tparam OtherBase Base type of other any_of_opt.
         * @tparam OtherTypes Type list of other any_of_opt.
         * @param other The other any_of_opt to construct from.
         * @return *this
         * @throws std::bad_variant_access If the type of object hold by other any_of_opt is not supported.
         */
        template<typename OtherBase, typename ...OtherTypes>
        // TODO Only allow it if union(Types and OtherTypes) is not empty
        self_t& operator=(const any_of_opt<OtherBase, OtherTypes...>& other) {
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr ((std::is_same_v<T, Types> || ...)) {
                    this->operator=(arg);
                } else {
                    throw std::bad_variant_access{};
                }
            }, other._variant);
            return *this;
        }


        /**
         * @brief Value holding test.
         * Test if the any_of_opt is holding a value.
         * @return true if any_of_opt is holding a value, false otherwise.
         */
        constexpr bool has_value() const noexcept {
            return _variant.index() != 0;
        }

        /**
         * @brief Value holding test.
         * Test if the any_of_opt is holding a value.
         * @return true if any_of_opt is holding a value, false otherwise.
         */
        constexpr explicit operator bool() const noexcept {
            return has_value();
        }

        /**
         * @brief Index of the held alternative.
         * Returns the zero-based index of the alternative that is currently held.
         * @return Zero-based index of the held alternative, npos if no value is held.
         */
        constexpr std::size_t index() const noexcept {
            if (_variant.index() == 0) {
                return npos;
            } else {
                return _variant.index() -
                       1; // It will return the index of type, ignoring the std::monostate (for optional) index.
            }
        }

        /**
         * @brief Reset any_of_opt to empty.
         * Force the any_of_opt to not hold any object.
         */
        constexpr void reset() noexcept {
            _variant = {};
        }

        /**
         * @brief Emplace a new value.
         * @tparam T Type of the new value to emplace.
         * @tparam ...Args Argument types of emplaced object constructo parameters.
         * @param ...args Arguments to forward to constructor of object to emplace.
         * @return Reference to emplaced object.
         */
        template<class T, class... Args>
        constexpr T& emplace(Args&& ... args) {
            return _variant.template emplace<T>(args...);
        }

        // TODO Add initializer-list and variant-alternative emplaces

        /**
         * @brief Swap two any_of_opt.
         * @param rhs Other any_of_opt to swap with.
         */
        constexpr void swap(self_t& rhs) noexcept(((std::is_nothrow_move_constructible_v<Types> &&
                                                    std::is_nothrow_swappable_v<Types>) && ...)) {
            _variant.swap(rhs._variant);
        }



        //
        // Specific interface:
        //

        /**
         * @brief Common type accessor.
         * Access to the held object throw the common type.
         * @return Reference to the held object.
         * @throw std::bad_optional_access if any_of_opt id empty (not holding anything).
         */
        Base& value() {
            if (_variant.index() == 0) {
                throw std::bad_optional_access{};
            } else {
                return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
            }
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Reference to the held object.
         * @throw std::bad_optional_access if any_of_opt id empty (not holding anything).
         */
        const Base& value() const {
            if (_variant.index() == 0) {
                throw std::bad_optional_access{};
            } else {
                return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
            }
        }

        /**
         * @brief Common type non-throwing accessor.
         * Access to the held object with the common type, returning fallback parameter if empty.
         * @return Reference to the held object, or the fallback parameter if empty.
         */
        Base& value_or(Base& other) noexcept {
            if (_variant.index() == 0) {
                return other;
            } else {
                return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
            }
        }

        /**
         * @brief Common type non-throwing accessor.
         * Access to the held object with the common type, returning fallback parameter if empty.
         * @return Reference to the held object, or the fallback parameter if empty.
         */
        const Base& value_or(const Base& other) const noexcept {
            if (_variant.index() == 0) {
                return other;
            } else {
                return std::visit([](auto&& arg) -> Base& { return (Base&) arg; }, _variant);
            }
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Pointer to the held object.
         */
        Base* operator->() {
            return &value();
        }

        /**
         * @brief Common type accessor.
         * Access to the held object with the common type.
         * @return Pointer to the held object.
         */
        const Base* operator->() const {
            return &value();
        }

        /**
         * @brief Test the type of the held object.
         * @tparam Type Type to test.
         * @return True if the held object is of the tested type, false otherwise.
         */
        template<typename Type>
        constexpr bool is() const noexcept;

        /**
         * @brief Type-based value accessor.
         * Return the reference to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Reference of the held value, if of the requested type.
         * @throw std::bad_variant_access if held value is not of the requested type.
         */
        template<class Type>
        constexpr Type& get() {
            return std::get<Type>(_variant);
        }

        /**
         * @brief Type-based value accessor.
         * Return the reference to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Reference of the held value, if of the requested type.
         * @throw std::bad_variant_access if held value is not of the requested type.
         */
        template<class Type>
        constexpr const Type& get() const {
            return std::get<Type>(_variant);
        }

        /**
         * @brief Type-based non-throwing accessor.
         * Return a pointer to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Pointer to the held value, if of the requested type, nullptr otherwise.
         */
        template<class Type>
        constexpr std::add_pointer_t<Type> get_if() noexcept {
            return std::get_if<Type, std::monostate, Types...>(&_variant);
        }

        /**
         * @brief Type-based non-throwing accessor.
         * Return a pointer to the held value, if it is of requested type.
         * @tparam Type Type of the held value to retrieve.
         * @return Pointer to the held value, if of the requested type, nullptr otherwise.
         */
        template<class Type>
        constexpr std::add_pointer_t<const Type> get_if() const noexcept {
            return std::get_if<const Type, std::monostate, Types...>(&_variant);
        }


    };


    template<typename Base, typename ...Types>
    template<typename Type>
    constexpr bool any_of<Base, Types...>::is() const noexcept {
        return std::holds_alternative<Type>(_variant);
    }

    template<typename Base, typename ...Types>
    template<typename Type>
    constexpr bool any_of_opt<Base, Types...>::is() const noexcept {
        return std::holds_alternative<Type>(_variant);
    }

} // namespace ANY_OF_NS

namespace std {

    template< class T, class Base, class... Types >
    constexpr bool holds_alternative(const ::ANY_OF_NS::any_of<Base, Types...>& v) noexcept {
        return std::holds_alternative<T, Types...>(v._variant);
    }


    template< class T, class Base, class... Types >
    constexpr bool holds_alternative(const ::ANY_OF_NS::any_of_opt<Base, Types...>& v) noexcept {
        return std::holds_alternative<T/*, std::monostate, Types...*/>(v._variant);
    }

} // namespace std



#endif // __ANY_OF_HPP__
