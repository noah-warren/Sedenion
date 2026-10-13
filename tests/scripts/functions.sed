# Optional parameters, rest parameters, and method calls.

# A default is evaluated inside the function, so it can use earlier parameters.
add(x, y = 1) = x + y
add(2)                                          # expect: 3
add(2, 5)                                       # expect: 7

fn later(a, b = a + 1)
  return b
end
later(4)                                        # expect: 5
later(4, 9)                                     # expect: 9

# nil is a value the caller passed. The default runs when the argument is absent.
fn shown(x = 0) = x
shown()                                         # expect: 0
print(shown(nil))                               # expect: nil

base = 10
fn shifted(a, b = a + base) = b
shifted(1)                                      # expect: 11

# The rest parameter is a list of every argument after the fixed ones.
fn total(first, ...rest) = first + sum(rest)
total(10)                                       # expect: 10
total(10, 1, 2, 3)                              # expect: 16

pack(...xs) = xs
pack()                                          # expect: []
pack(4, 5)                                      # expect: [4, 5]

fn mix(a, b = 1, ...rest) = a + b + sum(rest)
mix(5)                                          # expect: 6
mix(5, 6, 7, 8)                                 # expect: 26

fn optrest(a = 1, ...xs) = a + sum(xs)
optrest()                                       # expect: 1
optrest(4, 5)                                   # expect: 9

fn greet(name, punct = "!")
  return name + punct
end
greet("Ada")                                    # expect: Ada!

# Too few required arguments, or extras without a rest parameter, is an error.
try
  add()
catch e
  print(e)
end
# expect: add expects 1 to 2 arguments, got 0

try
  add(1, 2, 3)
catch e
  print(e)
end
# expect: add expects 1 to 2 arguments, got 3

try
  total()
catch e
  print(e)
end
# expect: total expects at least 1 argument, got 0

# A call written on a field passes the record as the first argument.
fn norm(self) = hypot(self.x, self.y)
point = {x: 3, y: 4, norm: norm}
point.norm()                                    # expect: 5
type(point.norm)                                # expect: function

fn widen(self, k = 2) = self.x * k
box = {x: 5, widen: widen}
box.widen()                                     # expect: 10
box.widen(3)                                    # expect: 15

# The same function, called under its own name, receives only the arguments written there.
g = box.widen
g(box)                                          # expect: 10

fn scaled(self, x) = self.x * x
shape = {x: 3, scaled: scaled}
shape.scaled(4)                                 # expect: 12
h = shape.scaled
try
  h(4)
catch e
  print(e)
end
# expect: scaled expects 2 arguments, got 1

fn tag(self, ...xs) = len(xs)
rec = {tag: tag}
rec.tag()                                       # expect: 0
rec.tag(1, 2)                                   # expect: 2

# Indexing with brackets is an ordinary call.
fn inc(x) = x + 1
fns = [inc]
fns[1](10)                                      # expect: 11
