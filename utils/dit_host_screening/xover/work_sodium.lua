-- PUBLIC lane: binary-trees, the body that screened at +14.52% always-on DIT.
-- Allocation-and-pointer-chase heavy, so the cost lands in the interpreter's own
-- dispatch loop rather than in the guest arithmetic.
--
-- The secret call is interleaved into the public stream at ARG_PERIOD, so the
-- work between crypto calls is real interpreter execution.

local N      = tonumber(ARG_DEPTH)  or 16
local PERIOD = tonumber(ARG_PERIOD) or 64
local NPER   = tonumber(ARG_NPER)   or 1

local function bottomup(d)
  if d > 0 then return { bottomup(d - 1), bottomup(d - 1) } end
  return {}
end

local function check(t)
  if t[1] then return 1 + check(t[1]) + check(t[2]) end
  return 1
end

local total, sacc = 0, 0
local long = bottomup(N)

local d = 4
while d <= N do
  local iters = 1 << (N - d + 4)
  local c = 0
  for k = 1, iters do
    c = c + check(bottomup(d))
    if NPER > 0 and (k % PERIOD) == 0 then sacc = sacc + secret(NPER) end
  end
  total = total + c
  d = d + 2
end

total = total + check(long)
print("WORK binary_trees checksum " .. total .. " sacc " .. (sacc % 1000000))
