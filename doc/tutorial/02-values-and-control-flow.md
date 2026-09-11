# 2. Values and Control Flow

This chapter uses K's primitive types, arrays, and statements to calculate the
sum of the even numbers in a fixed array.

```k
module values;

sumEven(values : int[]) : int {
    total : int = 0;

    for (index : int = 0; index < values.size; index += 1) {
        value : int = values[index];
        if (value % 2 == 0) {
            total += value;
        }
    }

    return total;
}

main() : int {
    values : int[] {3, 4, 7, 10};
    result : int = sumEven(values);
    k::io::stdout.println(result);
    return 0;
}
```

It prints `14`.

## Variables and primitive types

Declare a variable with its name, a colon, and its type:

```k
enabled : bool = true;
attempts : int = 3;
distance : double = 12.5d;
initial : char = 'K';
const maximum : int = 100;
```

The common integer types are `byte`, `short`, `int`, and `long`, with
`unsigned` variants. `float` and `double` are IEEE 754 floating-point types;
`bool` holds `true` or `false`; `char` stores one Unicode code point. Use
suffixes where a narrower or wider literal matters: `10s`, `42L`, `7u`, and
`3.14d`.

`const` requires initialization and prevents later assignment:

```k
const port : int = 8080;
// port = 9090;  // Error: a const variable cannot be assigned.
```

## Arrays

K has fixed-size, stack-allocated arrays. The size may be inferred from an
initializer:

```k
numbers : int[3] {10, 20, 30};
moreNumbers : int[] {40, 50, 60};

first : int = numbers[0];
count : unsigned int = numbers.size;
```

An `int[]` parameter is an unsized array view. It lets `sumEven` above accept
arrays of any fixed size without copying their elements. Array indexing is
bounds-checked.

## Conditions and loops

`if` and `else` select between branches:

```k
labelScore(score : int) : int {
    if (score >= 50) {
        return 1;
    } else {
        return 0;
    }
}
```

Use `while` when the loop is controlled by a condition, and `for` when the
initialization, condition, and update naturally belong together:

```k
countdown(start : int) {
    current : int = start;
    while (current > 0) {
        k::io::stdout.println(current);
        current -= 1;
    }
}

sumTo(limit : int) : int {
    total : int = 0;
    for (i : int = 1; i <= limit; i += 1) {
        total += i;
    }
    return total;
}
```

K supports familiar arithmetic, comparison, logical, and compound-assignment
operators, including `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `&&`, `||`,
`+=`, and `-=`. `break` exits the innermost loop; `continue` starts its next
iteration.

**Next:** [Functions and arrays](03-functions-and-arrays.md)
