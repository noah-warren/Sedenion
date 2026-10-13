# import "file": paths are relative to the importing file, ".sed" is optional,
# and each file runs once even when imported repeatedly or in a cycle
loadCount = 0
import "modules/geometry"
import "modules/geometry.sed"
import "modules/shapes"
loadCount                       # expect: 0
loads                           # expect: 1
hypot(3, 4)                     # expect: 5
triangleArea(3, 4, 5)           # expect: 6
circleArea(2)                   # expect: 4π

try
  import "modules/missing"
catch e
  print(e)
end
# expect: import: cannot find 'modules/missing'

try
  import "modules/broken"
catch e
  print(e)
end
# expect: broken.sed:2:1: syntax error: expected an expression at end of line
