# Rules are patterns you add yourself. Names other than pi, e and i are pattern variables.
sym x, y

rule difference_of_squares
  a^2 - b^2 => (a - b)*(a + b)
end

rule pythagorean
  sin(a)^2 => 1 - cos(a)^2
end

print(rewrite(x^2 - y^2))
print(rewrite(sin(x)^2 + sin(y)^2, "pythagorean"))
steps(rewrite(x^2 - y^2, "difference_of_squares"))

# assume tells simplify how to take square roots. forget drops the assumption.
assume x > 0
print(simplify(sqrt(x^2)))
assume y <= 0
print(simplify(sqrt(y^2)))
forget()

print(check(sin(x)^2 + cos(x)^2 == 1))
print(check(x == y))

# working() is the derivation as data: a result, the text, and a LaTeX copy.
w = working(integrate(x * e^x, x))
print(w.result)
print(w.text)
print(w.latex)
