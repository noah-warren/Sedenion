# Exact square roots of rationals. Perfect squares stay ordinary numbers.
sqrt(16)                            # expect: 4
sqrt(8)                             # expect: 2sqrt(2)
sqrt(2) * sqrt(8)                   # expect: 4
sqrt(12)                            # expect: 2sqrt(3)
sqrt(1/2)                           # expect: sqrt(2)/2
sqrt(4/9)                           # expect: 2/3
sqrt(2)^2                           # expect: 2
sqrt(2) + sqrt(8)                   # expect: 3sqrt(2)
(sqrt(3) + sqrt(2)) * (sqrt(3) - sqrt(2))
# expect: 1
1 / sqrt(2)                         # expect: sqrt(2)/2
1 / (sqrt(2) + 1)                   # expect: -1 + sqrt(2)
sqrt(2) - sqrt(2)                   # expect: 0
sqrt(4 + 2*sqrt(3))                 # expect: 1 + sqrt(3)
|1 - sqrt(2)|                       # expect: -1 + sqrt(2)
sqrt(2) < sqrt(3)                   # expect: true
sqrt(2) < 2                         # expect: true
float(sqrt(4))                      # expect: 2.0
type(sqrt(2))                       # expect: radical
