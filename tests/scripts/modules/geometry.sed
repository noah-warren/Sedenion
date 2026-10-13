# A small module: imported by tests/scripts/imports.sed.
# Its assignments stay here. Only exported names are visible to the importer.
export hypot, triangleArea, loads
import "shapes"

fn hypot(a, b) = sqrt(a^2 + b^2)
fn triangleArea(a, b, c)
  s = (a + b + c) / 2
  return sqrt(s * (s - a) * (s - b) * (s - c))
end
loads = 1
