# IN-PROGRESS: Exception Support

**Feature:** Full exception mechanism — `throw`, `try-catch`, `throws` clause, compile-time
exception contract verification, stack unwinding with destructor calls.

**Status:** 🔧 Implementation in progress — Core codegen complete, KDI/stdlib/docs remaining

---

## Checklist

- [x] Step 1: Keywords (THROW, TRY, CATCH, THROWS)
- [x] Step 2: AST nodes (throw_statement, try_catch_statement, catch_clause, throws in function_decl)
- [x] Step 3: Parser — throw & try-catch statements
- [x] Step 4: Parser — throws clause on functions
- [x] Step 5: Model — throw_statement
- [x] Step 6: Model — try_catch_statement, catch_clause
- [x] Step 7: Model — function throws spec
- [x] Step 8: Model builder
- [x] Step 9: Visitor & dump
- [x] Step 10: Diagnostic codes (exception-specific model/gen errors)
- [x] Step 11: Symbol & type resolution for throws/catch
- [x] Step 12: Exception contract checker
- [x] Step 13: Codegen — throw statement
- [x] Step 14: Codegen — try-catch statement (catch-all, no type matching yet)
- [x] Step 15: Codegen — invoke/landingpad/cleanup (stack unwinding)
- [x] Step 16: Type-based catch dispatch + nested try-catch propagation
- [x] Step 17: KDI support (throws spec serialization)
- [x] Step 18: Standard library (Exception, MemoryException)
- [ ] Step 19: Documentation (grammar, summary, stdlib doc)
- [ ] Step 20: Tests
- [x] All existing tests pass
- [x] All new exception tests pass (22 tests, 65 assertions)

---

## Design Summary

Key points:
- Exceptions are classes deriving from `::k::Exception`
- `throws` clause on functions is mandatory for any function that can throw
- Compile-time static verification of exception contracts
- LLVM Itanium ABI-based implementation (invoke/landingpad, __cxa_throw, etc.)
- Stack unwinding properly destroys local objects
- Catch by reference only
- No `finally`, no rethrow, no exception masking in this phase (see TODO.md)

Syntax:
```
// Throw
throw MyException("message");

// Function declaration with throws
myFunc(a: int) : Result throws IOException, ParseException { ... }

// Try-catch
try {
    riskyCall();
} catch (e : IOException&) {
    // handle
} catch (const e : Exception&) {
    // fallback
}
```

---

*Delete this file when feature is complete.*



