# Public names of a small library. Imported by modules_named.sed.
# `twice` and `secret` stay private; `double` can still call `twice`.
export double, tag

fn twice(x) = 2x
double(x) = twice(x)
tag = {kind: "algebra"}
secret = 5
