-- PUBLIC half: binary-trees, the workload that screened at +14.52% always-on
-- DIT. Interleaved with SECRET signing so the two genuinely alternate.
--
-- ARG1 = tree depth, ARG2 = signatures per outer iteration.

local N = tonumber(ARG1) or 17
local SIGS = tonumber(ARG2) or 1

local function bottomup(d)
  if d > 0 then return { bottomup(d - 1), bottomup(d - 1) } end
  return {}
end

local function check(t)
  if t[1] then return 1 + check(t[1]) + check(t[2]) end
  return 1
end

local total, sigacc = 0, 0
local long = bottomup(N)

local d = 4
while d <= N do
  local iters = 1 << (N - d + 4)
  local c = 0
  for k = 1, iters do
    c = c + check(bottomup(d))
    -- secret work interleaved into the public stream
    if SIGS > 0 and (k % 64) == 0 then sigacc = sigacc + sign(SIGS) end
  end
  total = total + c
  d = d + 2
end

total = total + check(long)
print("WORK binary_trees checksum " .. total .. " sigacc " .. (sigacc % 1000000))
