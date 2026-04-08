## TODO and wish list

### K Language

- Review casting algorithm and implicit casting strategy (char[]! -> const char[]?  ou  char[]! -> const char[], etc.)
- Add temporary object explicit construction (incl in return expr) — **struct form done**, **struct designated init done** (`S{.x=val}`), array temporary `T[]{init}` pending
- Add return type covariance
- Add "virtual" symbols (parent, self, etc.)
- Add typed enums
- Add unions, typed unions
- Add state classes
- Add templates
- Better private visibility support
- Improve log and debug messages
- Add in-comment documentation support (e.g. for doc generation)
- Varargs
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
- Add static conditional statements and static compiler value definitions

### libk
- Refactor libk C functions wrapping to reduce intermediate method counts
- Move math stub functions
- Add following to base libk:
  - containers 
  - maths
  - time/date
  - log facade
  - filesystem
  - process
  - threading
  - networking
- Add sub libraries for:
  - security (crypto, authn, authz)
  - net (http)
  - serialization
  - database client
  - message-bus client

