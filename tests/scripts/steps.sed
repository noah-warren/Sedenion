# steps(expr): step-by-step working for diff, integrate, solve and limit
sym x

steps(diff(x^2*sin(x), x))
# expect: d/dx (x^2*sin(x))    [product rule: (uv)' = u'v + uv']
# expect:   d/dx sin(x) = cos(x)    [sin'(u) = cos(u)]
# expect:   d/dx (x^2) = 2x    [power rule: (x^n)' = n x^(n-1)]
# expect:   = x^2*cos(x) + 2x*sin(x)
# expect: x^2*cos(x) + 2x*sin(x)

steps(integrate(x*e^x, x))
# expect: ∫ x*e^x dx    [integration by parts: u = x, dv = e^x dx, v = e^x]
# expect:   ∫ e^x dx = e^x    [∫ e^u du = e^u]
# expect:   ∫ e^x dx = e^x    [∫ e^u du = e^u]
# expect:   = x*e^x - e^x
# expect: x*e^x - e^x

steps(integrate(2x*cos(x^2), x))
# expect: ∫ 2x*cos(x^2) dx    [constant multiple: ∫ c f dx = c ∫ f dx]
# expect:   ∫ x*cos(x^2) dx    [substitution u = x^2, du = 2x dx]
# expect:     ∫ cos(u)/2 du    [constant multiple: ∫ c f dx = c ∫ f dx]
# expect:       ∫ cos(u) du = sin(u)    [table: ∫ cos(u) du = sin(u)]
# expect:       = sin(u)/2
# expect:     = sin(x^2)/2
# expect:   = sin(x^2)
# expect: sin(x^2)

steps(integrate(x^2, x, 0, 3))
# expect: ∫ from 0 to 3 of x^2 dx    [fundamental theorem of calculus]
# expect:   ∫ x^2 dx = x^3/3    [power rule: ∫ u^n du = u^(n+1)/(n+1)]
# expect:   F(3) - F(0) = 9 - 0
# expect:   = 9
# expect: 9

steps(solve(x^2 - 5x + 6 == 0, x))
# expect: solve x^2 - 5x + 6 == 0 for x
# expect:   polynomial of degree 2 in x
# expect:   factor: (x - 3)*(x - 2) = 0
# expect:   x - 3 = 0 gives x = 3
# expect:   x - 2 = 0 gives x = 2
# expect:   ⇒ x = 2, x = 3
# expect: {2, 3}

# squaring introduces x = 0, which the check rejects
steps(solve(sqrt(x+1) == x - 1, x))
# expect: solve sqrt(x + 1) == x - 1 for x
# expect:   move everything to one side: -x + sqrt(x + 1) + 1 = 0
# expect:   isolate the square root: sqrt(x + 1) = x - 1
# expect:   square both sides (this can add extraneous roots): x + 1 = x^2 - 2x + 1
# expect:   expand: -x^2 + 3x = 0
# expect:   polynomial of degree 2 in x
# expect:   factor: -x*(x - 3) = 0
# expect:   x - 3 = 0 gives x = 3
# expect:   x = 0 gives x = 0
# expect:   reject x = 0: it does not satisfy the original equation
# expect:   ⇒ x = 3
# expect: {3}

steps(limit((1-cos(x))/x^2, x, 0))
# expect: lim x→0 (-cos(x) + 1)/x^2    [0/0 form: L'Hôpital's rule]
# expect:   numerator' = sin(x)
# expect:   denominator' = 2x
# expect:   lim x→0 sin(x)/(2x)    [0/0 form: L'Hôpital's rule]
# expect:     numerator' = cos(x)
# expect:     denominator' = 2
# expect:     lim x→0 cos(x)/2 = 1/2    [direct substitution]
# expect:     = 1/2
# expect:   = 1/2
# expect: 1/2

# a function value is also accepted, and the result is still returned
r = steps(() -> diff(x^3, x))
# expect: d/dx (x^3) = 3x^2    [power rule: (x^n)' = n x^(n-1)]
r + 1
# expect: 3x^2 + 1

steps(2 + 3)
# expect: (no step-by-step working for this computation)
# expect: 5

# recording is switched off after an error
steps(solve(sin(x) + x^x == 0, x))
# expect-error: cannot solve
