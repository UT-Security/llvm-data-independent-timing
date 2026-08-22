-- PUBLIC and SECRET interleaved INSIDE the interpreter.
--
-- The loop body is public arithmetic; every K-th iteration also reads a byte of
-- the secret string. So K is the interleaving granularity, in units of VM
-- instructions rather than function calls: at K = 1 the interpreter alternates
-- public and secret work continuously and there is no boundary at which a mode
-- switch could sensibly be placed.
--
-- The two accumulators are kept SEPARATE and only combined at the end, so the
-- public arithmetic genuinely stays public and the pass has something to leave
-- alone. Mixing them per-iteration would make everything downstream secret and
-- the experiment would measure nothing.

local S = SECRET
local N = tonumber(ARG_N) or 2000000
local K = tonumber(ARG_K) or 1
local len = #S

local pub, sec = 0, 0
local byte = string.byte

for i = 1, N do
  pub = (pub * 1103515245 + i) % 2147483647          -- PUBLIC
  if (i % K) == 0 then
    sec = sec + byte(S, (i % len) + 1)               -- SECRET
  end
end

print("WORK typeb checksum " .. (pub % 1000000) .. " " .. (sec % 1000000))
