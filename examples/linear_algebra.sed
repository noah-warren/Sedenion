# Linear algebra: exact when the entries are exact

# The Hilbert matrix is famously ill-conditioned, but exact arithmetic doesn't care
H = matrix(4, 4, (i, j) -> 1/(i + j - 1))
H
det(H)
inv(H)                       # all integers
inv(H) * H == identity(4)

# Solve a linear system three ways
A = [2, 1, -1; -3, -1, 2; -2, 1, 2]
b = [8; -11; -3]
A \ b
solve(A, b)
rref(augment(A, b))

# Eigenvalues come from the characteristic polynomial
M = [4, 1; 2, 3]
charpoly(M)
eigvals(M)
eigvecs(M)

# Rotation matrices compose by multiplication; R(θ)^n = R(nθ)
R(t) = [cos(t), -sin(t); sin(t), cos(t)]
R(π/6)
R(π/6)^3 == R(π/2)

# Null space and rank of a singular matrix
S = [1, 2, 3; 4, 5, 6; 7, 8, 9]
rank(S)
nullspace(S)

# Floating-point matrices use partial pivoting
F = [0.001, 1; 1, 1]
F \ [1; 2]

# Vector operations
u = [1, 2, 3] ; v = [4, 5, 6]
u · v ; u × v ; norm(u) ; (u × v) · u
