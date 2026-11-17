// JIT benchmark for the jsc payload (docs/04-jit.md): the same lines run on
// the interpreter-only jsc (out/jsc.elf) and on the JIT build, and the times
// are compared. The jsc REPL evaluates one line at a time, so every test is a
// single line. Send with:
//   .\send.ps1 -Ps5Host IP -Payload jsc_shell\out\jsc-jit.elf -InputFile jsc_shell\bench.js
print("bench: start");
var o = typeof jscOptions === "function" ? jscOptions() : {}; print("options: useJIT " + o.useJIT + ", useBaselineJIT " + o.useBaselineJIT + ", useDFGJIT " + o.useDFGJIT + ", useFTLJIT " + o.useFTLJIT + ", useRegExpJIT " + o.useRegExpJIT + ", usePollingTraps " + o.usePollingTraps);
function time(name, f) { var t0 = preciseTime(); var r = f(); var ms = (preciseTime() - t0) * 1000; print(name + ": " + ms.toFixed(1) + " ms (" + r + ")"); return ms; }
var total = 0;
total += time("loop 1e7", function () { var s = 0; for (var i = 0; i < 1e7; i++) s += i; return s; });
total += time("objects 2e6", function () { class P { constructor(x, y) { this.x = x; this.y = y; } add(o) { return new P(this.x + o.x, this.y + o.y); } } var p = new P(0, 0), d = new P(1, 2); for (var i = 0; i < 2e6; i++) p = p.add(d); return p.x + p.y; });
total += time("strings 2e5", function () { var a = []; for (var i = 0; i < 2e5; i++) a.push("item-" + i.toString(16)); var s = a.join(","); return s.split(",").filter(function (x) { return x.indexOf("f") >= 0; }).length; });
total += time("regexp 2e5", function () { var re = /^([a-z]+)-(\d+)@([a-z]+)\.(com|pt)$/; var n = 0; for (var i = 0; i < 2e5; i++) if (re.test("user-" + i + "@mail." + (i & 1 ? "com" : "pt"))) n++; return n; });
total += time("json 2e3", function () { var obj = {list: []}; for (var i = 0; i < 200; i++) obj.list.push({id: i, name: "n" + i, tags: ["a", "b", "c"], ok: i % 2 === 0}); var n = 0; for (var j = 0; j < 2000; j++) n += JSON.parse(JSON.stringify(obj)).list.length; return n; });
total += time("sort 2e5", function () { var a = []; var x = 12345; for (var i = 0; i < 2e5; i++) { x = (x * 1103515245 + 12345) & 0x7fffffff; a.push(x); } a.sort(function (p, q) { return p - q; }); return a[0] <= a[1] ? "sorted" : "NOT sorted"; });
total += time("closures 1e6", function () { var a = []; for (var i = 0; i < 1e6; i++) a.push(i); return a.map(function (v) { return v * 2; }).filter(function (v) { return v % 3 === 0; }).reduce(function (s, v) { return s + v; }, 0); });
function hot(n) { var s = 0; for (var i = 0; i < n; i++) s = (s + i * 3) % 1000003; return s; } for (var k = 0; k < 2000; k++) hot(1000); print("hot(): " + (typeof numberOfDFGCompiles === "function" ? numberOfDFGCompiles(hot) + " DFG compile(s)" : "no numberOfDFGCompiles"));
print("total: " + total.toFixed(1) + " ms");
print("bench: end");
