-- Three Lua workloads chosen to SEPARATE the mechanism, not just to be slow.
--
--   binary_trees - allocation + tree traversal. Serial load-to-address chain
--                  through the object graph, plus GC tracing the same graph.
--                  Prediction: high DIT cost.
--   fannkuch     - integer permutation over a flat array. Data-dependent, but
--                  array-indexed and shallow; no pointer chain to speak of.
--                  Prediction: low.
--   spectralnorm - float arithmetic over flat arrays, embarrassingly parallel
--                  in the OoO sense. Prediction: ~zero, same as the SVG
--                  filters in dit-headroom-needs-serial-chains.
--
-- If the cost tracks the pointer-chasing column and not the "is it slow"
-- column, that is the serial-chain requirement confirmed inside a real
-- interpreter rather than a microbenchmark.

local which = arg[1] or "binary_trees"
local n = tonumber(arg[2] or "") or 0

local function binary_trees(N)
  N = N > 0 and N or 17
  local function bottomup(d)
    if d > 0 then return { bottomup(d - 1), bottomup(d - 1) } end
    return {}
  end
  local function check(t)
    if t[1] then return 1 + check(t[1]) + check(t[2]) end
    return 1
  end
  local total, long = 0, bottomup(N)
  local d = 4
  while d <= N do
    local iters = 1 << (N - d + 4)
    local c = 0
    for _ = 1, iters do c = c + check(bottomup(d)) end
    total = total + c
    d = d + 2
  end
  return total + check(long)
end

local function fannkuch(N)
  N = N > 0 and N or 10
  local p, q, s = {}, {}, {}
  for i = 1, N do p[i] = i; q[i] = i; s[i] = i end
  local sign, maxflips, sum = true, 0, 0
  while true do
    local q1 = p[1]
    if q1 ~= 1 then
      for i = 2, N do q[i] = p[i] end
      local flips = 1
      repeat
        local qq = q[q1]
        if qq == 1 then
          sum = sum + (sign and flips or -flips)
          if flips > maxflips then maxflips = flips end
          break
        end
        q[q1] = q1
        if q1 >= 4 then
          local i, j = 2, q1 - 1
          repeat q[i], q[j] = q[j], q[i]; i = i + 1; j = j - 1 until i >= j
        end
        q1 = qq; flips = flips + 1
      until false
    end
    if sign then
      p[2], p[1] = p[1], p[2]; sign = false
    else
      p[2], p[3] = p[3], p[2]; sign = true
      local done = true
      for i = 3, N do
        local sx = s[i]
        if sx ~= 1 then s[i] = sx - 1; done = false; break end
        s[i] = i
        local t = p[1]
        for j = 1, i do p[j] = p[j + 1] end
        p[i + 1] = t
      end
      if done then return maxflips + sum end
    end
  end
end

local function spectralnorm(N)
  N = N > 0 and N or 900
  local function A(i, j) local ij = i + j - 1; return 1.0 / (ij * (ij - 1) * 0.5 + i) end
  local u, v = {}, {}
  for i = 1, N do u[i] = 1 end
  for _ = 1, 10 do
    for i = 1, N do local s = 0; for j = 1, N do s = s + A(i, j) * u[j] end; v[i] = s end
    for i = 1, N do local s = 0; for j = 1, N do s = s + A(j, i) * v[j] end; u[i] = s end
  end
  local vBv, vv = 0, 0
  for i = 1, N do vBv = vBv + u[i] * v[i]; vv = vv + v[i] * v[i] end
  return math.floor(math.sqrt(vBv / vv) * 1e9)
end

local fns = { binary_trees = binary_trees, fannkuch = fannkuch, spectralnorm = spectralnorm }
print("LUABENCH " .. which .. " " .. tostring(fns[which](n)))
