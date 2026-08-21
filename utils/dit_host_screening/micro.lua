-- Isolate WHY spectralnorm is the most DIT-sensitive Lua workload (+26%).
-- I predicted it would be ~zero on the grounds that it is "flat float arrays,
-- embarrassingly parallel". It was the highest. Two candidate explanations:
--
--   (1) I mislabelled it. `s = s + A(i,j)*u[j]` is a loop-carried FP
--       accumulator, i.e. a SERIAL dependency chain after all - just an
--       arithmetic one rather than a pointer one.
--   (2) The float DIVIDE in A() is the sensitive operation.
--
-- These make opposite predictions, so they are separable:
--   serial_add   serial FP accumulator, no divide
--   par_add      same work, 4 independent accumulators = chain broken
--   serial_div   serial accumulator WITH a divide
--   par_div      divides with the chain broken
--
-- If (1): serial_* >> par_*, and divide makes little difference.
-- If (2): *_div >> *_add, regardless of serial vs parallel.

local which = arg[1] or "serial_add"
local N = tonumber(arg[2] or "") or 20000000

local x = {}
for i = 1, 1024 do x[i] = 1.0 + i * 0.5 end

local function serial_add()
  local s = 0.0
  for i = 1, N do s = s + x[(i & 1023) + 1] end
  return s
end

local function par_add()
  local a, b, c, d = 0.0, 0.0, 0.0, 0.0
  for i = 1, N, 4 do
    a = a + x[(i & 1023) + 1]
    b = b + x[((i + 1) & 1023) + 1]
    c = c + x[((i + 2) & 1023) + 1]
    d = d + x[((i + 3) & 1023) + 1]
  end
  return a + b + c + d
end

local function serial_div()
  local s = 0.0
  for i = 1, N do s = s + 1.0 / x[(i & 1023) + 1] end
  return s
end

local function par_div()
  local a, b, c, d = 0.0, 0.0, 0.0, 0.0
  for i = 1, N, 4 do
    a = a + 1.0 / x[(i & 1023) + 1]
    b = b + 1.0 / x[((i + 1) & 1023) + 1]
    c = c + 1.0 / x[((i + 2) & 1023) + 1]
    d = d + 1.0 / x[((i + 3) & 1023) + 1]
  end
  return a + b + c + d
end

local fns = { serial_add = serial_add, par_add = par_add,
              serial_div = serial_div, par_div = par_div }
print(string.format("MICRO %s %.6f", which, fns[which]()))
