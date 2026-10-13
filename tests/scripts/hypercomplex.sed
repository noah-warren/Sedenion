# Quaternions, octonions and sedenions (Cayley-Dickson construction)
qi = quat(0, 1, 0, 0)
qj = quat(0, 0, 1, 0)
qk = quat(0, 0, 0, 1)
qi*qj                               # expect: k
qj*qi                               # expect: -k
qi^2                                # expect: -1
qi*qj*qk                            # expect: -1
q = quat(1, 2, 3, 4)
q                                   # expect: 1 + 2i + 3j + 4k
q^-1                                # expect: 1/30 - (1/15)i - (1/10)j - (2/15)k
q * q^-1                            # expect: 1
conj(q)                             # expect: 1 - 2i - 3j - 4k
|quat(1, 1, 1, 1)|                  # expect: 2
re(q)                               # expect: 1
im(q)                               # expect: [2, 3, 4]
components(q)                       # expect: [1, 2, 3, 4]
quat(2, [1, 0, 0])                  # expect: 2 + i
commutator(qi, qj)                  # expect: 2k
type(q)                             # expect: quaternion
q == quat(1, 2, 3, 4)               # expect: true

# rotations: exact for rational multiples of π
rotate([1, 0, 0], [0, 0, 1], π/2)   # expect: [0, 1, 0]
rotate([1, 0, 0], [0, 0, 1], π/3)   # expect: [1/2, sqrt(3)/2, 0]

# octonions are not associative
e = [basis(8, k) | k ∈ 1..7]
type(e[1])                          # expect: octonion
associator(e[1], e[2], e[3]) == 0   # expect: true
associator(e[1], e[2], e[4])        # expect: 2e7

# sedenions have zero divisors
a = basis(16, 1) + basis(16, 10)
b = basis(16, 4) - basis(16, 15)
a != 0 and b != 0                   # expect: true
a * b == 0                          # expect: true
type(a)                             # expect: sedenion
