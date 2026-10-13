# A required parameter follows an optional one.
fn bad(a = 1, b) = a + b
# expect-error: parameters with defaults must follow the required parameters
