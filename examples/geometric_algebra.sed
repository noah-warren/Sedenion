# A multivector in Cl(p, q). The first p basis vectors square to +1, the next q to -1.
# * is the geometric product, · the inner product, and wedge() the exterior product.

e1 = blade(3, 0, 1)
e2 = blade(3, 0, 2)
e3 = blade(3, 0, 3)

print("e1 e2 = ", e1 * e2, ",   e2 e1 = ", e2 * e1)
print("e1 ∧ e2 = ", wedge(e1, e2), ",   e1 · e2 = ", e1 · e2)
print("e1^2 = ", e1 * e1)

# A rotor turns the e1-e2 plane. sandwich(R, v) is R v reverse(R).
B = blade(3, 0, 1, 2)
R = rotor(B, π/2)
print("R = ", R)
print("R e1 R~ = ", sandwich(R, e1))
print("R e3 R~ = ", sandwich(R, e3))

# Cl(1, 1), as in a 1+1 spacetime: space squares to +1 and time to -1.
space = blade(1, 1, 1)
time = blade(1, 1, 2)
print("space^2 = ", space * space, ",   time^2 = ", time * time)
print(mv([1, 2, 3, 4], 1, 1))
