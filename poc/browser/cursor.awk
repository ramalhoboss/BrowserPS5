# Video frames per second while the cursor moves and while it is still.
# Input: a browser log with MEDIA_PROBE "video at X ms: t=.. frames=N
# dropped=M ... shown=S gap=G" lines and CURSOR_TEST "cursor test:
# moving/still from X ms", "cursor test: done at X ms" lines.
# frames: the <video>'s own count; shown: the page's frames the UIProcess
# took to the TV; gap: the longest wait between two of those (ms).
BEGIN { n = 0; m = 0 }  # numbers: edge[n] with n unset is edge[""]
function num(s, key,   r) {
  if (!match(s, key "[0-9.]+")) return -1
  r = substr(s, RSTART + length(key), RLENGTH - length(key))
  return r + 0
}
/cursor test: moving from/ { edge[n] = num($0, "from "); kind[n++] = "moving" }
/cursor test: still from/  { edge[n] = num($0, "from "); kind[n++] = "still" }
/cursor test: done at/     { edge[n] = num($0, "at ");   kind[n++] = "done" }
/video at [0-9]+ ms: t=/ {
  T[m] = num($0, "video at "); F[m] = num($0, "frames=")
  D[m] = num($0, "dropped="); V[m] = num($0, " t=")
  S[m] = num($0, "shown="); G[m] = num($0, "gap="); m++
}
# The first probe at or after time x.
function first_at(x,   i) { for (i = 0; i < m; i++) if (T[i] >= x) return i; return m - 1 }
function span(label, x, b,   s, i, g) {
  if (b <= x) return
  for (i = b; i > x && V[i] < V[x]; i--) {}  # the player started again: stop before
  b = i
  if (b <= x) return
  s = (T[b] - T[x]) / 1000
  g = 0
  for (i = x + 1; i <= b; i++) if (G[i] > g) g = G[i]
  printf "%-7s %6.1f-%6.1f s: video %5.1f frames/s (%3d dropped), TV %5.1f frames/s, longest gap %4d ms, video %5.1f s in %5.1f s\n", \
    label, T[x] / 1000, T[b] / 1000, (F[b] - F[x]) / s, D[b] - D[x], \
    S[x] >= 0 ? (S[b] - S[x]) / s : -1, g, V[b] - V[x], s
}
END {
  if (n > 0) span("before", first_at(edge[0] - 10000), first_at(edge[0]))
  for (i = 0; i + 1 < n; i++) span(kind[i], first_at(edge[i]), first_at(edge[i + 1]))
  if (detail) for (i = 1; i < m; i++)
    printf "  %7.1f s: video t=%6.1f frames=%5d (+%3d) dropped=%5d (+%3d) shown +%3d gap %4d\n", \
      T[i] / 1000, V[i], F[i], F[i] - F[i-1], D[i], D[i] - D[i-1], S[i] - S[i-1], G[i]
}
