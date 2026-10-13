# try / catch: built-in errors, error(value), nesting, and control flow out of try blocks
try
  x = 1/0
catch e
  print("caught:", e)
end
# expect: caught: division by zero

# error(v) hands the value itself to the handler
try
  error([404, "not found"])
catch e
  print(e[1], e[2])
end
# expect: 404 not found

# the variable is optional, and execution continues after the handler
try error("x") catch print("handled") end
print("after")
# expect: handled
# expect: after

# errors raised deep inside calls, including calls made by builtins, are caught
fn risky(k)
  if k == 3 then error("bad k = " + str(k)) end
  return k^2
end
try
  ys = map(risky, 1..5)
catch e
  print(e)
end
# expect: bad k = 3

# a function can recover and return a value
fn safeDiv(a, b)
  try
    return a / b
  catch
    return nil
  end
end
safeDiv(1, 4)                            # expect: 1/4
safeDiv(1, 0) == nil                     # expect: true

# nested handlers: the inner one catches first, rethrowing reaches the outer one
try
  try
    error("inner")
  catch e
    error("outer saw " + e)
  end
catch e2
  print(e2)
end
# expect: outer saw inner

# break and continue leave the try block cleanly
total = 0
for k in 1..10 do
  try
    if k == 5 then break end
    if k % 2 == 0 then continue end
    total = total + k
  catch
  end
end
total                                    # expect: 4
try error("still caught after the loop") catch e print(e) end
# expect: still caught after the loop

# return from inside try does not leave a stale handler behind
fn early() try return 1 catch return 2 end end
early()                                  # expect: 1
try
  undefinedThing + 1
catch e
  print("handler ok")
end
# expect: handler ok

# unit and math errors are ordinary errors too
try 1[m] + 1[s] catch e print(e) end
# expect: cannot add quantities with different dimensions (m and s)

# an uncaught error still stops the script
error("unhandled")
# expect-error: unhandled
