# Phase 4: sets, ranges, comprehensions, sums and products
{3, 1, 2} ∪ {5}               # expect: {1, 2, 3, 5}
{1, 2, 3} ∩ {2, 3, 4}         # expect: {2, 3}
{1, 2, 3} \ {2}               # expect: {1, 3}
{1, 2} union {2, 9}           # expect: {1, 2, 9}
3 ∈ {1, 2, 3} ; 5 ∉ {1, 2} ; 4 in 1..10
# expect: true
# expect: true
# expect: true
powerset({1, 2})              # expect: {{}, {1}, {1, 2}, {2}}
{1, 2} × {3, 4}               # expect: {[1, 3], [1, 4], [2, 3], [2, 4]}
|{1, 2, 3}|                   # expect: 3
∑(k = 1..100) k^2             # expect: 338350
∏(k = 1..5) k                 # expect: 120
sum(k^2, k = 1..10)           # expect: 385
∑(k = 1..10) 1/k              # expect: 7381/2520
{x^2 | x ∈ 1..5, x % 2 == 1}  # expect: {1, 9, 25}
[x * y | x ∈ 1..3, y ∈ 1..2]  # expect: [1, 2, 2, 4, 3, 6]
1..10 step 3                  # expect: 1..10 step 3
list(1..10 step 3)            # expect: [1, 4, 7, 10]
list(1..2 step 1/2)           # expect: [1, 3/2, 2]
list(5..1 step -2)            # expect: [5, 3, 1]
sum(1..1000000)               # expect: 500000500000
issubset({1, 2}, {1, 2, 3})   # expect: true
