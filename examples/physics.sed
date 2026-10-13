# Physics with units: dimensions are checked, conversions are exact

# Kinematics: stopping distance at 100 km/h with 7 m/s^2 of braking
v = 100[km/h]
a = 7[m/s^2]
d = v^2 / (2a)
d
d to [ft]

# Energy of a falling object
m = 2[kg]
h = 10[m]
E = m * constant("g") * h
E
E to [kWh]
E to [cal]

# Orbital period of the ISS (altitude 420 km)
G = constant("G")
M = 5.972e24[kg]
r = 6371[km] + 420[km]
T = 2π * sqrt(r^3 / (G * M))
T to [min]

# Photon energy of green light
λ = 532[nm]
Ephoton = constant("h") * constant("c") / λ
Ephoton to [eV]

# Electric circuits: P = V^2 / R
V = 230[V]
R = 50[Ω]
V / R
V^2 / R
(V^2 / R) * 3[h] to [kWh]

# Angles are dimensionless units
sin(30[°]) ; cos(60°) ; 90° to [rad]

# Unit errors are caught
fn trySum()
  return 1[m] + 1[s]
end
print("adding meters to seconds fails, as it should")
