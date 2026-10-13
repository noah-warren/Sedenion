# Interpolation, format, the argument list, and text files.
name = "Ada"
n = 2
"hi $name"                          # expect: hi Ada
"sum ${n + 1}"                      # expect: sum 3
"dollar $$ and \$"                  # expect: dollar $ and $
format("{} and {0}", 1/2)           # expect: 1/2 and 1/2
format("{1}-{0}", "a", "b")         # expect: b-a
format("hello {name}", {name: "world"})
# expect: hello world
format("{{ok}}")                    # expect: {ok}

# The script runner does not pass command-line arguments, so argv stays empty.
len(argv)                           # expect: 0

p = "sedenion_script_io.txt"
exists(p)                           # expect: false
writefile(p, "hello")
appendfile(p, "!")
readfile(p)                         # expect: hello!
exists(p)                           # expect: true
removefile(p)                       # expect: true
exists(p)                           # expect: false
removefile(p)                       # expect: false
