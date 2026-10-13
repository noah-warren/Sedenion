# Number theory with exact big integers

# Mersenne primes up to 2^127 - 1
mersenne = [p | p ∈ primes(130), isprime(2^p - 1)]
print("Mersenne exponents:", mersenne)
print("largest:", 2^last(mersenne) - 1)

# Factor a few numbers (Pollard rho handles the large ones)
for n in [360, 2^32 + 1, 600851475143, 10^18 + 9] do
  print(n, "=", factorint(n))
end

# Euler's totient and the Carmichael number 561
print("φ(561) =", totient(561), "and 561 =", factorint(561))
print("Fermat test fooled:", all(map(a -> powmod(a, 560, 561) == 1, [a | a ∈ 2..20, gcd(a, 561) == 1])))

# Exact rational arithmetic: the harmonic number H(30)
H = ∑(k = 1..30) 1/k
print("H(30) =", H)
print("      ≈", float(H))

# Continued fraction convergents of √2 approach it from both sides
fn convergent(n)
  x = 1
  for k in 1..n do x = 1 + 1/(1 + x) end
  return x
end
[convergent(k) | k ∈ 1..8]

# Fibonacci numbers and Cassini's identity F(n-1) F(n+1) - F(n)^2 = (-1)^n
n = 200
fib(n - 1) * fib(n + 1) - fib(n)^2

# Digits of 100!
d = digits(100!)
print("100! has", len(d), "digits with digit sum", sum(d))

# Pythagorean triples with c ≤ 30
{[a, b, c] | c ∈ 1..30, b ∈ 1..c, a ∈ 1..b, a^2 + b^2 == c^2, gcd(a, b) == 1}
