# Phase 5: numerical calculus
round(nderiv(t -> t^3, 2), 8)                         # expect: 12.0
round(nintegrate(t -> sin(t), 0, π), 10)              # expect: 2.0
round(nintegrate(t -> e^(-t^2), -inf, inf)^2, 10)     # expect: 3.1415926536
round(nintegrate(t -> 1/t^2, 1, inf), 10)             # expect: 1.0
round(root(t -> cos(t) - t, 0, 1), 10)                # expect: 0.7390851332
round(newton(t -> t^2 - 2, 1), 12)                    # expect: 1.414213562373
round(minimize(t -> (t - 3)^2 + 1, 0, 5), 6)          # expect: 3.0
round(ode((t, y) -> y, 1, 0, 1), 8)                   # expect: 2.71828183
sym x
round(nintegrate(x^2, x, 0, 3), 10)                   # expect: 9.0
