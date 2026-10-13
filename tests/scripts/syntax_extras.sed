# Parallel assignment, `not in`, symbolic ranges, Unicode operators, one-line blocks
fn gcd2(a, b)
  while b != 0 do
    a, b = b, a % b
  end
  return a
end
gcd2(48, 18)                          # expect: 6
p, q = 1, 2
p, q = q, p
[p, q]                                # expect: [2, 1]

3 not in {1, 2}                       # expect: true
{1, 2, 3} ∖ {1}                       # expect: {2, 3}
sq = t ↦ t^2
sq(5)                                 # expect: 25
∛27                                   # expect: 3
2 ≤ 3                                 # expect: true
x0 = 0.5
0 < x0 <= 1                           # expect: true
y = 3
y² + y⁻¹                              # expect: 28/3

list(0..2π step π/2)                  # expect: [0, π/2, π, 3π/2, 2π]
[sin(t) | t ∈ 0..π step π/2]          # expect: [0, 1, 0]
1..10^9                               # expect: 1..1000000000

fn f(n) if n < 2 then return n end return f(n - 1) + f(n - 2) end
f(15)                                 # expect: 610
fn inc(a) a + 1 end
inc(1)                                # expect: 2

count(k -> k % 3 == 0, 1..30)         # expect: 10
H = matrix(2, 2, (i, j) -> 1/(i + j - 1))
det(H)                                # expect: 1/12
e^(i*π)                               # expect: -1

sym x
if x^2 - 1 == (x - 1)*(x + 1) then print("same") end   # expect: same
limit(x * ln(x), x, 0)                # expect: 0
integrate(1/(1 + x^2), x, -inf, inf)  # expect: π
