# 6. Resources and Errors

K makes object lifetime explicit while still providing deterministic cleanup.
The most important tool is the owner type, `T!`.

## Owners and observers

`new T(...)` creates a heap object and returns an exclusive owner, `T!`. When
the owner leaves scope, the object is destroyed and freed automatically.

```k
module ownership;

struct Connection {
    open : bool = true;

    close() {
        open = false;
    }
}

main() : int {
    connection : Connection! = new Connection();
    connection->close();
    return 0;
}  // connection is deleted here
```

An owner is move-only. Assignment transfers ownership:

```k
first : Connection! = new Connection();
second : Connection! = first;
// first is now null; second owns the Connection.
```

Use `T*` for a nullable, mutable non-owning pointer; `T?` for a nullable,
immutable binding; `T&` for a non-null, immutable binding; and `T+` for a
non-null, rebindable link. Observers never extend an object's lifetime:

```k
owner : Connection! = new Connection();
view : Connection* = owner;
if (view) {
    view->close();
}
```

Do not retain `view` after `owner` is deleted or leaves scope. Prefer an owner
for lifecycle responsibility, and a reference or pointer only for temporary
access.

## Checked exceptions

Only `Throwable` subclasses may be thrown. Subclasses of `Exception` are
checked: a function that lets one escape must declare it in `throws(...)`.

```k
module errors;

class InvalidPort : public Exception {
    public InvalidPort() : Exception(1) {
    }
}

validatePort(port : int) : void throws(InvalidPort) {
    if (port < 1 || port > 65535) {
        throw InvalidPort();
    }
}

main() : int {
    try {
        validatePort(0);
    } catch (error : InvalidPort&) {
        k::io::stderr.println("Invalid port");
        return 1;
    }
    return 0;
}
```

Catch parameters must be non-null references, such as `InvalidPort&`. Put
more-specific catches before their base types. `FatalError` subclasses are
unchecked and need no `throws` declaration.

During a return, exception, `break`, or `continue`, K destroys local
aggregates and owners in reverse declaration order. This deterministic cleanup
is why keeping resources in local owner variables is preferable to manual
cleanup paths.

**Next:** [Modules and libraries](07-modules-and-libraries.md)
