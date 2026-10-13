# Sedenion

A programming language for mathematics, written in C++17 with no external dependencies.
You write ordinary programs — functions, loops, records, and modules — and the values
stay exact: `1/3` is a rational, `sqrt(8)` is `2sqrt(2)`, and after `sym x` the
expression `x^2 + 1` is a formula. A script is a sequence of statements. An expression
on its own line prints its value. A function can take optional arguments and gather the
extra ones into a list, and a record can carry functions that are called as methods.

```text
> 1/3 + 1/6
1/2
> 50!
30414093201713378043612608166064768844377641568960512000000000000
> A = [1, 2; 3, 4] ; A^-1
[ -2     1
 3/2  -1/2]
> sym x
> integrate(x * e^x, x)
x*e^x - e^x
> solve(x^2 - 5x + 6 == 0, x)
{2, 3}
> ∑(k = 1..100) k^2
338350
> 100[km] / 2[h] to [m/s]
13.8888888888889 m/s
> quat(0, 1, 0, 0) * quat(0, 0, 1, 0)
k
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

## Features

- **Exact numbers**: arbitrary-precision integers and rationals (hand-written bignum), exact
  square roots of rationals, plus floating-point reals and complex numbers, with automatic
  promotion between them.
- **Math-like syntax**: `2x`, `x²`, `√x`, `|x|`, `n!`, `A'`, `∑`, `∏`, `∈`, `∪`, `∩`, `π`, and
  an ASCII spelling for all of them.
- **Linear algebra**: exact Gaussian elimination, determinants, inverses, rank, RREF,
  null spaces, characteristic polynomials and eigenvalues.
- **Sets and ranges**: set algebra, comprehensions, power sets, cartesian products, and lazy
  ranges with arbitrary steps.
- **Symbolic algebra**: simplification, expansion, factoring over ℚ, differentiation,
  integration (table, substitution, parts, partial fractions), equation and system solving,
  limits and Taylor series.
- **Step-by-step working**: `steps(expr)` prints the rules `diff`, `integrate`, `solve` and
  `limit` applied (product and chain rules, substitution, integration by parts, partial
  fractions, factoring, the quadratic formula, rejected extraneous roots, L'Hôpital's rule).
  `working(expr)` returns that derivation as `{result, text, latex}`.
- **Rewrite rules and assumptions**: `rule` / `rewrite` for your own algebraic identities,
  `assume` / `forget` so `simplify` can take signed square roots, and `check` for a
  symbolic or sampled comparison.
- **Hypercomplex numbers**: quaternions, octonions and sedenions with exact arithmetic,
  quaternion rotations, commutators and associators.
- **Geometric algebra**: multivectors in Cl(p, q), the geometric, wedge and inner products,
  and rotors.
- **Records, modules and files**: `{a: 1, b: 2}`, string interpolation, `format`,
  `import "lib.sed" as M` / `export` with private module globals, and reading and writing text files.
- **Numerical calculus**: adaptive Gauss–Kronrod quadrature, Brent and Newton root finding,
  minimization, and an adaptive RK45 ODE solver.
- **Physical units**: dimension-checked arithmetic, conversions with `to`, SI prefixes, and
  physical constants.
- **Functions**: optional parameters (`fn f(x, n = 1)`), a rest parameter that gathers
  extra arguments into a list (`fn f(x, ...rest)`), and method calls (`point.norm()`)
  that pass the record as the first argument. A call on an imported module, `A.double(21)`,
  passes the arguments written in the parentheses.
- **A real language**: closures, recursion, loops, lists, records, strings, modules, a
  bytecode VM with a garbage collector, a REPL, and readable error messages with stack traces.

## Building

You need CMake 3.16 or newer and a C++17 compiler.

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

On Windows with Visual Studio the executable ends up in `build/Release/sedenion.exe`;
with single-configuration generators it is `build/sedenion`.

## Usage

```text
sedenion                   # REPL
sedenion examples/tour.sed # run a script
sedenion -e "factorint(2^64 + 1)"
```

Type `help()` in the REPL for a list of builtins by category, or `help("solve")` for one.

## Documentation and examples

- [docs/LANGUAGE.md](docs/LANGUAGE.md) is the language reference.
- `examples/` holds runnable programs: a language tour, number theory, linear algebra,
  calculus, physics with units, sets and sums, hypercomplex numbers, step-by-step working,
  records and files, rewrite rules, geometric algebra, exact radicals, and functions
  with optional parameters, rest parameters, and methods.

## Layout

| Directory | Contents |
|---|---|
| `src/lexer`, `src/parser` | UTF-8 lexer and Pratt parser |
| `src/compiler`, `src/vm` | bytecode compiler, stack VM, garbage collector |
| `src/numeric` | BigInt, Rational, and the numeric tower |
| `src/linalg`, `src/sets`, `src/calculus`, `src/units` | domain libraries |
| `src/cas` | the computer algebra system, including steps, rules and LaTeX |
| `src/hyper` | quaternions, octonions and sedenions |
| `src/ga` | geometric algebra |
| `src/stdlib` | builtin functions |
| `tests/unit` | C++ unit tests |
| `tests/scripts` | `.sed` scripts checked against `# expect:` comments |
