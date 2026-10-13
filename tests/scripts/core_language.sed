# Phase 1: variables, functions, closures, control flow, strings and lists
s = "héllo"
len(s)                        # expect: 5
s[2]                          # expect: é
s * 2                         # expect: héllohéllo
"a" + "b"                     # expect: ab

L = [3, 1, 2]
sort(L)                       # expect: [1, 2, 3]
L[-1]                         # expect: 2
L[1..2]                       # expect: [3, 1]
push(L, 9) ; L                # expect: [3, 1, 2, 9]
# expect: [3, 1, 2, 9]

map(x -> x^2, 1..5)           # expect: [1, 4, 9, 16, 25]
filter(x -> x % 2 == 0, 1..10) # expect: [2, 4, 6, 8, 10]
reduce((a, b) -> a + b, 1..10) # expect: 55

fn fib2(n) if n < 2 then return n end return fib2(n-1) + fib2(n-2) end
fib2(20)                      # expect: 6765

fn counter()
  c = 0
  return fn() c = c + 1 ; return c end
end
ct = counter()
ct() ; ct() ; ct()
# expect: 1
# expect: 2
# expect: 3

k = 0
while k < 3 do k = k + 1 end
k                             # expect: 3

total = 0
for j in 1..10 do
  if j == 5 then continue end
  if j > 8 then break end
  total = total + j
end
total                         # expect: 31

f(x) = x^2 + 1
g = x -> 2x + 3
f(3) ; g(4)
# expect: 10
# expect: 11
2(3 + 1)                      # expect: 8

x = 7
if x > 5 then print("big") elif x > 2 then print("medium") else print("small") end
# expect: big

type(1/2) ; type(2^100) ; type(1.5) ; type(2i) ; type({1}) ; type("s")
# expect: rational
# expect: integer
# expect: real
# expect: complex
# expect: set
# expect: string

str(12) + "!"                 # expect: 12!
zip([1, 2], ["a", "b"])       # expect: [[1, "a"], [2, "b"]]
