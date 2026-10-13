# Phase 3: vectors and matrices with exact elimination
A = [1, 2; 3, 4]
det(A)                        # expect: -2
A * [1; 1]
# expect: [3
# expect:  7]
A'
# expect: [1  3
# expect:  2  4]
B = [2, 1, 1; 1, 3, 2; 1, 0, 0]
det(B)                        # expect: -1
rank(B)                       # expect: 3
B * inv(B) == identity(3)     # expect: true
A^-1 * A == identity(2)       # expect: true
rref([1, 2, 3; 4, 5, 6])
# expect: [1  0  -1
# expect:  0  1   2]
B \ [1; 2; 3]
# expect: [  3
# expect:    9
# expect:  -14]
[1, 2, 3] · [4, 5, 6]         # expect: 32
[1, 0, 0] × [0, 1, 0]         # expect: [0, 0, 1]
norm([3, 4])                  # expect: 5
trace(B)                      # expect: 5
eigvals([2, 0; 0, 3])         # expect: [2, 3]
charpoly([1, 2; 3, 4])        # expect: λ^2 - 5λ - 2
[1, 2] + [3, 4]               # expect: [4, 6]
2 * [1, 2]                    # expect: [2, 4]
B[2, 3] ; B[1]
# expect: 2
# expect: [2, 1, 1]
det([1/2, 1/3; 1/4, 1/5])     # expect: 1/60
A^2 == A * A                  # expect: true
size(B)                       # expect: [3, 3]
