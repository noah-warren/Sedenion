# Symbolic and numerical calculus side by side
sym x, n

f = x^2 * sin(x)
df = diff(f, x)
df
integrate(df, x)                    # back to f

# Definite integrals: exact where possible, numeric otherwise
integrate(x * e^(-x), x, 0, inf)
integrate(1/(1 + x^2), x, -inf, inf)
nintegrate(t -> e^(-t^2), 0, 1)
integrate(e^(-x^2), x, 0, 1)        # no elementary antiderivative: falls back to numeric

# Limits
limit(sin(x)/x, x, 0)
limit((1 + 1/x)^x, x, inf)
limit(x * ln(x), x, 0)

# Taylor series and how good they are
p = taylor(cos(x), x, 0, 6)
p
N(subs(p, x, 1/2)) - cos(0.5)

# Newton's method written by hand, using the symbolic derivative
fn newtonSym(g, x0, steps)
  dg = diff(g, x)
  t = x0
  for k in 1..steps do t = t - N(subs(g, x, t)) / N(subs(dg, x, t)) end
  return t
end
newtonSym(x^3 - 2x - 5, 2, 6)
root(t -> t^3 - 2t - 5, 2, 3)

# Partial fractions turn a rational integral into logs
r = (3x + 5)/(x^2 + 3x + 2)
apart(r, x)
integrate(r, x)

# An ODE: logistic growth y' = y (1 - y), y(0) = 1/10
y1 = ode((t, y) -> y * (1 - y), 0.1, 0, 5)
exact = 1 / (1 + 9e^(-5))
y1 - N(exact)

# Optimization: the box of maximum volume from a 10x10 sheet
V = x * (10 - 2x)^2
crit = solve(diff(V, x) == 0, x)
crit
subs(V, x, 5/3)
