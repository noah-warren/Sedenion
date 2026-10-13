# Phase 6: symbolic algebra and calculus
sym x, y
expand((x + 1)^2)                       # expect: x^2 + 2x + 1
expand((x - y)*(x + y))                 # expect: x^2 - y^2
expand((x + y)^3)                       # expect: x^3 + 3x^2*y + 3x*y^2 + y^3
simplify(sin(x)^2 + cos(x)^2)           # expect: 1
simplify((x^2 - 1)/(x - 1))             # expect: x + 1
factor(x^3 - 6x^2 + 11x - 6)            # expect: (x - 3)*(x - 2)*(x - 1)
factor(x^4 - 1)                         # expect: (x - 1)*(x + 1)*(x^2 + 1)
factor(2x^2 + 4x + 2)                   # expect: 2(x + 1)^2
factor(expand((x^3 + x + 1)*(x^3 - x + 1)))   # expect: (x^3 + x + 1)*(x^3 - x + 1)
factor(x^5 + x + 1)                     # expect: (x^2 + x + 1)*(x^3 - x^2 + 1)
factor(x^4 + 4)                         # expect: (x^2 - 2x + 2)*(x^2 + 2x + 2)
factor(x^4 - 10x^2 + 1)                 # expect: x^4 - 10x^2 + 1
factor(x^12 - 1)                        # expect: (x - 1)*(x + 1)*(x^2 + 1)*(x^2 + x + 1)*(x^2 - x + 1)*(x^4 - x^2 + 1)
factor(x^4 + x^2*y^2 + y^4)             # expect: (x^2 + x*y + y^2)*(x^2 - x*y + y^2)
x/2 + x/3                               # expect: 5x/6
2(x + 1)                                # expect: 2(x + 1)
x*x*x                                   # expect: x^3
sin(π/3)                                # expect: sqrt(3)/2

# derivatives
diff(x^3, x)                            # expect: 3x^2
diff(sin(x) * x^2, x)                   # expect: x^2*cos(x) + 2x*sin(x)
diff(e^(2x), x)                         # expect: 2e^(2x)
diff(ln(x), x)                          # expect: 1/x
diff(atan(x), x)                        # expect: 1/(x^2 + 1)
diff(sqrt(x), x)                        # expect: 1/(2sqrt(x))
diff(x^5, x, 2)                         # expect: 20x^3

# integrals
integrate(x^2, x)                       # expect: x^3/3
integrate(sin(x), x)                    # expect: -cos(x)
integrate(cos(2x), x)                   # expect: sin(2x)/2
integrate(x * e^x, x)                   # expect: x*e^x - e^x
integrate(1/(1 + x^2), x)               # expect: atan(x)
integrate(2x*cos(x^2), x)               # expect: sin(x^2)
integrate(x^2, x, 0, 3)                 # expect: 9
integrate(sin(x), x, 0, π)              # expect: 2
integrate(e^(-x), x, 0, inf)            # expect: 1

# equations
solve(x^2 - 5x + 6 == 0, x)             # expect: {2, 3}
solve(x^2 + 1 == 0, x)                  # expect: {-i, i}
solve(x^2 - 2 == 0, x)                  # expect: {-sqrt(2), sqrt(2)}
solve(x^4 - 5x^2 + 4 == 0, x)           # expect: {-2, -1, 1, 2}
solve(e^x == 5, x)                      # expect: {ln(5)}
solve(sin(x) == 1/2, x)                 # expect: {π/6, 5π/6}
solve([x + y == 3, x - y == 1], [x, y]) # expect: [x == 2, y == 1]

# limits and series
limit(sin(x)/x, x, 0)                   # expect: 1
limit((1 + 1/x)^x, x, inf)              # expect: e
limit((x^2 - 1)/(x - 1), x, 1)          # expect: 2
taylor(e^x, x, 0, 4)                    # expect: x^4/24 + x^3/6 + x^2/2 + x + 1
subs(x^2 + y, x, 3)                     # expect: y + 9
apart(1/(x^2 - 1), x)                   # expect: 1/(2(x - 1)) - 1/(2(x + 1))
together(1/x + 1/y)                     # expect: (x + y)/(x*y)
f(t) = t^2 + 1
expand(f(x + 1))                        # expect: x^2 + 2x + 2
