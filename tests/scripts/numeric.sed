# Phase 2: exact numbers, bignums, complex numbers and core math
7 / 2                         # expect: 7/2
1/3 + 1/6                     # expect: 1/2
1/3 + 2/3                     # expect: 1
2^100                         # expect: 1267650600228229401496703205376
50!                           # expect: 30414093201713378043612608166064768844377641568960512000000000000
z = 3 + 4i ; |z|              # expect: 5
(1 + 2i) * (3 - i)            # expect: 5 + 5i
e^(i*π)                       # expect: -1
√16                           # expect: 4
sin(π/6) ; cos(π) ; tan(π/4)
# expect: 1/2
# expect: -1
# expect: 1
sin(30°)                      # expect: 1/2
ln(e^2) ; log(8, 2) ; exp(0)
# expect: 2
# expect: 3
# expect: 1
gcd(12, 18) ; lcm(4, 6)
# expect: 6
# expect: 12
isprime(97) ; isprime(2^61 - 1) ; isprime(2^67 - 1)
# expect: true
# expect: true
# expect: false
factorint(360)                # expect: [[2, 3], [3, 2], [5, 1]]
primes(30)                    # expect: [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]
binomial(10, 3)               # expect: 120
fib(100)                      # expect: 354224848179261915075
totient(36)                   # expect: 12
mean([1, 2, 3, 4])            # expect: 5/2
median([3, 1, 2])             # expect: 2
floor(7/2) ; ceil(-7/2) ; round(2.567, 2)
# expect: 3
# expect: -3
# expect: 2.57
div(-7, 2) ; -7 % 3
# expect: -4
# expect: 2
rationalize(0.75)             # expect: 3/4
float(1/4)                    # expect: 0.25
7.0 / 2                       # expect: 3.5
powmod(3, 200, 1000007)       # expect: 959082
