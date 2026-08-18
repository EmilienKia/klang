// libk/libk/src/functional.k
// K standard library functional programming aliases (namespace k::functional)
//
// Copyright 2023-2026 Emilien Kia

module k;

namespace functional {

// ═══════════════════════════════════════════════════════════════════════════
// Function aliases — inspired by Java java.util.function package
// ═══════════════════════════════════════════════════════════════════════════

// Function<T, R> — a callable that accepts one input and produces an output
// T is the input type, R is the return type
// Equivalent to: *(T):R
template<typename T, typename R>
alias Function : *(T):R;

// Predicate<T> — a callable that accepts one input and returns a boolean
// Equivalent to: *(T):bool
template<typename T>
alias Predicate : *(T):bool;

// Consumer<T> — a callable that accepts one input and returns nothing
// Equivalent to: *(T)
template<typename T>
alias Consumer : *(T);

// Supplier<T> — a callable that takes no inputs and returns an output
// Equivalent to: *():T
template<typename T>
alias Supplier : *():T;

// UnaryOperator<T> — a callable that accepts one input of type T and returns T
// Equivalent to: *(T):T
template<typename T>
alias UnaryOperator : *(T):T;

// BinaryOperator<T> — a callable that accepts two inputs of type T and returns T
// Equivalent to: *(T, T):T
template<typename T>
alias BinaryOperator : *(T, T):T;

// BiFunction<T, U, R> — a callable that accepts two inputs and produces an output
// T and U are the input types, R is the return type
// Equivalent to: *(T, U):R
template<typename T, typename U, typename R>
alias BiFunction : *(T, U):R;

// BiPredicate<T, U> — a callable that accepts two inputs and returns a boolean
// Equivalent to: *(T, U):bool
template<typename T, typename U>
alias BiPredicate : *(T, U):bool;

// BiConsumer<T, U> — a callable that accepts two inputs and returns nothing
// Equivalent to: *(T, U)
template<typename T, typename U>
alias BiConsumer : *(T, U);

// ═══════════════════════════════════════════════════════════════════════════
// Composable function aliases (for future feature: compose/andThen)
// These are here for reference and future use, but the implementation of
// compose() and andThen() methods is deferred to a later release.
// ═══════════════════════════════════════════════════════════════════════════

// Notes:
// - compose(f, g) = x -> f(g(x))  [apply g first, then f]
// - andThen(f, g) = x -> g(f(x))  [apply f first, then g]
// These are standard in functional programming libraries and can be added
// as extension methods on callable types once the basics are complete.

} // namespace functional
