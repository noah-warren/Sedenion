# A quick tour of Sedenion (the syntax preview from the design)

# definitions
f(x) = x^2 + 1
g = x -> 2x + 3                 # lambda; 2x is implicit multiplication
a = 1/3 + 1/6                   # exact: 1/2
f(3) ; g(4) ; a
z = 3 + 4i ; |z|                # 5

# control flow (keyword blocks, because {} is reserved for sets)
fn fact(n)
  if n <= 1 then return 1 end
  return n * fact(n - 1)
end
for k in 1..10 do print(k^2) end
fact(50)                        # bignum result

# linear algebra
A = [1, 2; 3, 4]
det(A) ; A^-1 ; A' ; A * [1; 1]

# sets, ranges, sums
S = {1, 2, 3} ∪ {3, 4} ; 2 ∈ S
∑(k = 1..100) k^2               # or sum(k^2, k = 1..100)
{x^2 | x ∈ 1..5, x % 2 == 1}    # set comprehension

# symbolic (sym declares symbols)
sym x, y
diff(sin(x) * x^2, x)
integrate(x * e^x, x)
factor(x^3 - 6x^2 + 11x - 6)
solve(x^2 - 5x + 6 == 0, x)     # {2, 3}
expand((x + y)^3)

# numerical calculus
nintegrate(t -> sin(t), 0, π)
root(t -> cos(t) - t, 0, 1)

# units (bracket syntax avoids clashing with variables named m, s, ...)
v = 100[km] / 2[h]
v to [m/s]                      # 13.888... [m/s]
