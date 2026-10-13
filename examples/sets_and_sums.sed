# Sets, ranges, comprehensions, sums and products

A = {1, 2, 3, 4, 5, 6}
B = {x | x ∈ 1..20, x % 3 == 0}
A ∪ B ; A ∩ B ; A \ B ; |A × B|
issubset({2, 4}, A)

# Inclusion-exclusion: numbers up to 1000 divisible by 3 or 5
count(k -> k % 3 == 0 or k % 5 == 0, 1..999)
sum(filter(k -> k % 3 == 0 or k % 5 == 0, 1..999))

# Closed forms checked against sums
sym n
∑(k = 1..100) k^3 == (100 * 101 / 2)^2
∏(k = 1..10) (1 + 1/k)

# Basel problem partial sum vs π^2/6
s = ∑(k = 1..10000) 1.0/k^2
s - N(π^2/6)

# Wallis product for π/2
2 * ∏(k = 1..2000) (4k^2 / (4k^2 - 1)) * 1.0

# The power set of a small set and its size
P = powerset({"a", "b", "c"})
P
|P|

# Ranges with steps, including rational steps
list(0..1 step 1/4)
[sin(t) | t ∈ 0..π step π/4]
