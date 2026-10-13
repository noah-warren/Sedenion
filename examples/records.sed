# Records, interpolated strings, and a text file.

person = {name: "Ada", born: 1815}
person.field = "analysis"
print(person)
print("${person.name} was born in ${person.born}.")
print(format("{name} ({field})", person))

# Keys can be computed. A bare name in {a: v} is the text "a", not the variable a.
k = "born"
print(record(k, person[k]))
print({1 + 1: "two"})

# argv[1] is the script path when a file is run; further entries are the arguments.
assert(len(argv) >= 1)

path = "sedenion_example_note.txt"
writefile(path, "field: " + person.field + "\n")
appendfile(path, "name: " + person.name + "\n")
print(readfile(path))
removed = removefile(path)
assert(removed && exists(path) == false)
