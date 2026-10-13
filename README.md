<div align="center">

# Sedenion

A programming language for mathematics, written in C++17 with no external dependencies.
You write ordinary programs — functions, loops, records, and modules — and the values
stay exact: `1/3` is a rational, `sqrt(8)` is `2sqrt(2)`, and after `sym x` the
expression `x^2 + 1` is a formula. A script is a sequence of statements. An expression
on its own line prints its value. A function can take optional arguments and gather the
extra ones into a list, and a record can carry functions that are called as methods.

</div>

---

## The problem

Serious math on a computer is split across tools that do not share a value.

Python will turn `1/3 + 1/7` into `0.47619047619047616` unless you stop and import a fraction type. NumPy inverts an integer matrix into decimals. SymPy can keep a square root exact, and Pint can convert kilometers per hour, and they rarely agree to live in the same expression. Mathematica holds this together, inside a proprietary notebook. Julia and MATLAB can too, after you assemble the packages or buy the toolbox.

The derivation is the part that usually stays on paper. A CAS will hand you the root. The page a teacher, a referee, or your own notes would accept is a separate product, when it exists at all.

Sedenion is one language for that whole job. A fraction stays a fraction. A square root stays a radical. A formula stays a formula. A speed knows it is a speed. And `steps(...)` prints the working, including the root it threw away.

```text
> 1/3 + 1/7
10/21
> sqrt(8)
2sqrt(2)
> [1, 2; 3, 4]^-1
[ -2     1
 3/2  -1/2]
> sym x ; solve(x^2 - 5x + 6 == 0, x)
{2, 3}
```

---

## The same question, two ways

<table>
<tr>
<td width="50%" valign="top">

**Before the math can start**

```python
from fractions import Fraction
import numpy as np

print(Fraction(1, 3) + Fraction(1, 7))
print(np.linalg.inv([[1.0, 2.0], [3.0, 4.0]]))
```

```text
10/21
[[-2.   1. ]
 [ 1.5 -0.5]]
```

The fraction took an import. The inverse gave the decimals up.

</td>
<td width="50%" valign="top">

**In Sedenion**

```text
1/3 + 1/7

[1, 2; 3, 4]^-1
```

```text
10/21
[ -2     1
 3/2  -1/2]
```

Same questions. The exact object is the ordinary value, so there is nothing to opt into.

</td>
</tr>
</table>

---

## What changes when the math is the language

### The answer you meant is the answer you get

`1/3` is a rational. `sqrt(8)` is `2sqrt(2)`, and `sqrt(4 + 2sqrt(3))` denests to `1 + sqrt(3)`. `2^200` is the integer, all of it. A decimal happens when you write one, or when you ask `float`.

That is the difference from a numeric language, where exactness is a mode you enter. Here exactness is the value, and approximation is the request.

### The working comes back with the result

`steps` prints the derivation. `working` hands it to your program as text and LaTeX, so a script can keep the reasoning, not only the root.

```text
> steps(solve(sqrt(x + 1) == x - 1, x))
solve sqrt(x + 1) == x - 1 for x
  move everything to one side: -x + sqrt(x + 1) + 1 = 0
  isolate the square root: sqrt(x + 1) = x - 1
  square both sides (this can add extraneous roots): x + 1 = x^2 - 2x + 1
  expand: -x^2 + 3x = 0
  polynomial of degree 2 in x
  factor: -x*(x - 3) = 0
  x - 3 = 0 gives x = 3
  x = 0 gives x = 0
  reject x = 0: it does not satisfy the original equation
  ⇒ x = 3
{3}
```

### Units, formulas, and matrices are the same expression

`100[km] / 2[h] to [m/s]` is a speed, and adding meters to seconds is an error. `sym x` turns `x` into a formula you can differentiate, integrate, and solve. `A^-1` on an integer matrix comes back in fractions. `quat(0, 1, 0, 0) * quat(0, 0, 1, 0)` is `k`.

You write it the way the page looks: `2x`, `x²`, `√x`, `|x|`, `n!`, `∑`, `∈`. Every one of those has an ASCII spelling.

It is also a programming language. Functions take optional arguments and a rest parameter, records carry methods, and a file you `import` keeps its own globals. The examples under `examples/` are programs, not transcripts.

---

## Where it wins

**Against the Python scientific stack.** SymPy, NumPy, and Pint each solve a piece, and you become the interpreter between them. Sedenion has one value, so a radical, a matrix of rationals, a unit, and a symbolic integral do not have to be introduced to each other. You give up the ecosystem. You get the expression.

**Against Mathematica.** Mathematica is the broad, polished standard, and it should be when you need that breadth. Sedenion is the language you can read, build, and ship: C++17, no dependencies, one binary. The derivation is a function in the program. `working(expr)` returns `{result, text, latex}`.

**Against Julia.** Julia is the right tool when the job is speed and packages. Rationals, symbols, and units there are libraries you compose (`//`, Symbolics, Unitful). In Sedenion they are the syntax. `1/3`, `sym x`, and `100[km] / 2[h]` need no import.

**Against MATLAB.** MATLAB is built for numeric matrices, and the symbolic work lives in a toolbox. Invert an integer matrix in Sedenion and the entries come back as fractions. `A^-1` is the expression you wanted to write.

---

## Run it

A C++17 compiler and CMake 3.16 or newer. Nothing else to install.

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

On Windows with Visual Studio the program is `build/Release/sedenion.exe`. With a single-configuration generator it is `build/sedenion`.

```text
sedenion                      # REPL
sedenion examples/tour.sed    # a script
sedenion -e "factorint(2^64 + 1)"
```

In the REPL, `help()` lists the builtins and `help("solve")` explains one.

---

## Look at a real program

| If you want to see… | Open |
|---|---|
| The language in one sitting | [`examples/tour.sed`](examples/tour.sed) |
| A solution with the working shown | [`examples/step_by_step.sed`](examples/step_by_step.sed) |
| Fractions, roots, and denesting | [`examples/radicals.sed`](examples/radicals.sed) |
| An inverse that stays exact | [`examples/linear_algebra.sed`](examples/linear_algebra.sed) |
| Speeds, constants, and dimension checks | [`examples/physics.sed`](examples/physics.sed) |
| Quaternions through sedenions | [`examples/hypercomplex.sed`](examples/hypercomplex.sed) |

The reference is [docs/LANGUAGE.md](docs/LANGUAGE.md). The name is the 16-dimensional number: quaternions, octonions, and sedenions are values in the language, beside the rationals and the formulas.

<details>
<summary>Repository map</summary>

| Directory | What lives there |
|---|---|
| `src/lexer`, `src/parser` | UTF-8 lexer and Pratt parser |
| `src/compiler`, `src/vm` | Bytecode compiler, stack machine, garbage collector |
| `src/numeric` | Big integers, rationals, radicals |
| `src/cas` | Simplification, calculus, solving, steps, rewrite rules, LaTeX |
| `src/linalg`, `src/sets`, `src/calculus`, `src/units` | Matrices, sets, numerical calculus, physical units |
| `src/hyper`, `src/ga` | Hypercomplex numbers and geometric algebra |
| `tests/scripts` | Language tests, each checked against `# expect:` comments |

</details>
