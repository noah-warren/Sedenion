# Records keep insertion order, compare equal regardless of order, and update in place.
r = {a: 1, b: 2, a: 3}
r                                   # expect: {a: 3, b: 2}
r.a                                 # expect: 3
r["b"]                              # expect: 2
r.c = 4
r.a += 1
r                                   # expect: {a: 4, b: 2, c: 4}
{c: 4, a: 4, b: 2} == r             # expect: true
{:}                                 # expect: {:}
type(r)                             # expect: record
keys(r)                             # expect: ["a", "b", "c"]
values(r)                           # expect: [4, 2, 4]
items({a: 1})                       # expect: [["a", 1]]
has(r, "c")                         # expect: true
has(r, "missing")                   # expect: false
"a" ∈ r                             # expect: true
"z" ∈ r                             # expect: false
record("k", 9, "k", 8)              # expect: {k: 8}
{1 + 1: "two"}                      # expect: {2: "two"}

ks = ""
for k ∈ {b: 1, a: 2} do ks = ks + k end
ks                                  # expect: ba

r2 = copy(r)
r2.a = 0
r.a                                 # expect: 4

try
  r.missing
catch e
  print(e)
end
# expect: record has no field "missing"
