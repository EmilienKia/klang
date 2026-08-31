# 4. Structs and Object-Oriented Programming

Structs group state and behavior. Fields use the same `name : Type` syntax as
local variables, and methods can access fields through an implicit `this`.

```k
module shapes;

struct Rectangle {
    width : int;
    height : int;

    Rectangle(width : int, height : int) : width(width), height(height) {
    }

    area() : int {
        return width * height;
    }
}

main() : int {
    rectangle : Rectangle(6, 4);
    k::io::stdout.println(rectangle.area());
    return 0;
}
```

The constructor initializes `width` and `height`; `area` returns `24`.

## Fields, methods, and visibility

Fields are public by default. Use `private:`, `protected:`, and `public:` to
control a group of declarations:

```k
struct Counter {
private:
    value : int = 0;

public:
    increment() {
        value += 1;
    }

    const get() : int {
        return value;
    }
}
```

A `const` member function cannot modify its fields. External code can call
`counter.get()`, but cannot access `counter.value`.

## Classes, interfaces, and inheritance

Use `class` when runtime polymorphism is required. Class methods are virtual
by default. An `interface` defines a polymorphic contract:

```k
interface Measurable {
    abstract area() : int;
}

class Square : public Measurable {
    side : int;

    Square(side : int) : side(side) {
    }

    override area() : int {
        return side * side;
    }
}

printArea(shape : Measurable&) {
    k::io::stdout.println(shape.area());
}

main() : int {
    square : Square(5);
    printArea(square);
    return 0;
}
```

`Square` fulfills the `Measurable` contract and can be passed where
`Measurable&` is expected. Use `override` when implementing an inherited
virtual method; it lets the compiler check that the method actually overrides
one.

## Designated initialization

For simple struct values, name fields directly:

```k
point : Rectangle { .width = 3, .height = 7 };
```

Designated initialization is useful for clear configuration-like values. Use a
constructor instead when the type must enforce invariants or perform setup.

**Next:** [Standard library essentials](05-standard-library-essentials.md)
