# User rewrite rules, assumptions, numeric checks, and working() as a value.
sym x, y

rule diffsq
  a^2 - b^2 => (a - b)*(a + b)
end
rewrite(x^2 - y^2)                  # expect: (x + y)*(x - y)

rule pyth
  sin(a)^2 => 1 - cos(a)^2
end
rewrite(sin(x)^2 + sin(y)^2, "pyth")
# expect: -cos(x)^2 - cos(y)^2 + 2
rewrite(sin(x)^2 + sin(y)^2, "diffsq")
# expect: sin(x)^2 + sin(y)^2

steps(rewrite(x^2 - y^2, "diffsq"))
# expect: rewrite x^2 - y^2
# expect:   diffsq: x^2 - y^2  =>  (x + y)*(x - y)
# expect:   = (x + y)*(x - y)
# expect: (x + y)*(x - y)

assume x > 0
simplify(sqrt(x^2))                 # expect: x
assume y <= 0
simplify(sqrt(y^2))                 # expect: -y
forget(x)
simplify(sqrt(x^2))                 # expect: sqrt(x^2)
forget()

check(sin(x)^2 + cos(x)^2 == 1)     # expect: true
check(x^2 == x*x)                   # expect: true
check(x == 0)                       # expect: false

w = working(diff(x^2, x))
w.result                            # expect: 2x
"power rule" ∈ w.text               # expect: true
"2 x" ∈ w.latex                     # expect: true
