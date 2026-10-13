# Phase 7: physical units
v = 100[km] / 2[h]
v                             # expect: 50 km/h
v to [m/s]                    # expect: 13.8888888888889 m/s
3[m] + 50[cm]                 # expect: 3.5 m
5[km] to [m]                  # expect: 5000 m
10[N] * 2[m]                  # expect: 20 J
1[kg] * 9.81[m/s^2]           # expect: 9.81 N
(2[h] + 30[min]) to [min]     # expect: 150 min
1[mi] to [km]                 # expect: 1.609344 km
sqrt(16[m^2])                 # expect: 4 m
1[km] / 1[m]                  # expect: 1000
sin(30[°])                    # expect: 1/2
2[m] < 3[ft]                  # expect: false
unitof(si(5[km/h]))           # expect: m/s
