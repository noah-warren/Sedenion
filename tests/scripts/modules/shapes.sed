# Imported by geometry.sed through a relative path, and imports it back (a cycle)
export circleArea
import "geometry.sed"

circleArea(r) = π * r^2
