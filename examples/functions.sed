# Functions with optional parameters, a rest parameter, and methods.

fn powi(x, n = 2) = x^n
print(powi(10))
print(powi(2, 10))

fn total(first, ...rest) = first + sum(rest)
print(total(10))
print(total(10, 1, 2, 3))

fn norm(self) = hypot(self.x, self.y)
point = {x: 3, y: 4, norm: norm}
print(point.norm())

fn widen(self, k = 2) = self.scale * k
box = {scale: 3, widen: widen}
print(box.widen())
print(box.widen(4))
