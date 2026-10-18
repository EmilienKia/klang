# K Guides

Practical overviews and workflows for commonly used K subsystems:

- [Memory, storage, and ownership](memory.md) — storage duration, all six
  addressers, `new`/owners, `Shared`/`Weak`, and explicit `UniSlot`/`MultiSlot`
  lifetimes.
- [Files and file systems](file-system.md) — paths, filesystem providers,
  entries, streams, and channels.
- [Date and time](time.md) — timeline, civil time, zones, calendars, and
  formatting.
- [Calling native C code](ffi.md) — direct and runtime FFI, ABI types,
  strings, native handles, struct layouts, ownership, and linking. See also
  [How-To: Wrap a Native C Library](../howtos/wrap-native-c-libraries.md).

For normative language and standard-library contracts, see the
[language reference](../spec/language/index.md) and
[standard-library reference](../spec/stdlib/index.md).
