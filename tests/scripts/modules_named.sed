# import "file" as M keeps the exports on a record. A bare import copies them into globals.
import "modules/algebra" as A
A.double(21)                        # expect: 42
A.tag.kind                          # expect: algebra
has(A, "double")                    # expect: true
has(A, "secret")                    # expect: false

import "modules/algebra"
double(3)                           # expect: 6
tag.kind                            # expect: algebra

try
  secret
catch e
  print(e)
end
# expect: undefined variable 'secret' (declare symbols with: sym secret)
