# steps(expr) evaluates expr and prints how diff, integrate, solve and limit reached the answer

sym x

print("== Differentiation ==")
steps(diff(e^(3x) * ln(x^2 + 1), x))

print("\n== Integration ==")
steps(integrate(x^2 * e^x, x))
steps(integrate(1/(x^2 - 1), x))
steps(integrate(sin(x)^2, x, 0, π))

print("\n== Solving ==")
steps(solve(e^(2x) - 3e^x + 2 == 0, x))
steps(solve(sqrt(2x + 3) == x, x))

print("\n== Limits ==")
steps(limit((1 + 1/x)^x, x, ∞))
steps(limit((e^x - 1 - x)/x^2, x, 0))
