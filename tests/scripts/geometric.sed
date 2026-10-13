# Geometric algebra in Cl(p, q): geometric product, wedge, inner product, rotors.
e1 = blade(3, 0, 1)
e2 = blade(3, 0, 2)
e3 = blade(3, 0, 3)

e1 * e1                             # expect: 1
e1 * e2                             # expect: e12
e2 * e1                             # expect: -e12
e1^2                                # expect: 1
wedge(e1, e2)                       # expect: e12
wedge(e1, e1)                       # expect: 0
e1 · e1                             # expect: 1
e1 · e2                             # expect: 0
inner(e1, e2)                       # expect: 0
reverse(e1 * e2)                    # expect: -e12
grade(1 + 2*e1 + 3*e1*e2, 2)        # expect: 3e12
mv([1, 2, 3, 4])                    # expect: 1 + 2e1 + 3e2 + 4e12

B = blade(3, 0, 1, 2)
rotor(B, π/2)                       # expect: sqrt(2)/2 - (sqrt(2)/2)e12
sandwich(rotor(B, π/2), e1)         # expect: e2

# Cl(1, 1): e1^2 = +1 and e2^2 = -1
blade(1, 1, 1) * blade(1, 1, 1)     # expect: 1
blade(1, 1, 2) * blade(1, 1, 2)     # expect: -1

(e1 * e2) * e3 - e1 * (e2 * e3)     # expect: 0
