module rtti_image_local;

struct P { value : int; }

f(value : P&) : int { return value.value + 202; }
ordinary(value : P&) : int { return f(value) + 3; }

class C {
    public C() {}
    public value() : int { return 202; }
}
