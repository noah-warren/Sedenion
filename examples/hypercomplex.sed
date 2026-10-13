# Beyond the complex numbers: quaternions, octonions and sedenions
# Each step of the Cayley-Dickson construction doubles the dimension and loses a property.

# Quaternions (4D): associative, but not commutative
qi = quat(0, 1, 0, 0)
qj = quat(0, 0, 1, 0)
qk = quat(0, 0, 0, 1)
print("i j = ", qi*qj, ",  j i = ", qj*qi)
print("i^2 = j^2 = k^2 = i j k = ", qi*qi, " ", qj*qj, " ", qk*qk, " ", qi*qj*qk)

q = quat(1, 2, 3, 4)
print("q = ", q, ",  |q| = ", |q|, ",  q^-1 = ", q^-1)

# Rotating a vector by 120° about the (1,1,1) axis cycles the coordinate axes
rotate([1, 0, 0], [1, 1, 1], 2π/3)
rotate([0, 0, 1], [0, 1, 0], π/2)

# Hamilton's product composes rotations: a quarter turn about z, written out by hand
θ = π/2
r = quat(cos(θ/2), [0, 0, sin(θ/2)])
v = quat(0, [1, 0, 0])
im(r * v * conj(r))

# Octonions (8D): not even associative
o = [basis(8, k) | k ∈ 1..7]
print("(e1 e2) e4 = ", (o[1]*o[2])*o[4], ",  e1 (e2 e4) = ", o[1]*(o[2]*o[4]))
print("associator: ", associator(o[1], o[2], o[4]))
# ... but still a normed division algebra: |ab| = |a||b|
a = octonion(1, 2, 0, -1, 3, 0, 1, 2)
b = octonion(0, 1, 1, 0, -2, 1, 0, 1)
|a*b| == |a|*|b|

# Sedenions (16D): zero divisors appear, so the norm is no longer multiplicative
s = basis(16, 1) + basis(16, 10)
t = basis(16, 4) - basis(16, 15)
print("s = ", s, ",  t = ", t)
print("s t = ", s*t, " although |s| = ", |s|, " and |t| = ", |t|)
