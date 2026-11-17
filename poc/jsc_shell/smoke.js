// Smoke test for the jsc payload. The jsc REPL evaluates one line at a
// time, so every check is a single line. Send with:
//   (cat jsc.elf; cat smoke.js) | nc $PS5_HOST 9021
print("smoke: start");
print("arith:   " + (6 * 7 === 42 ? "PASS" : "FAIL"));
print("json:    " + (JSON.parse(JSON.stringify({a: [1, 2, {b: "c"}]})).a[2].b === "c" ? "PASS" : "FAIL"));
print("regexp:  " + (/^(\w+)@(\w+)\.com$/.exec("ps5@sony.com")[2] === "sony" ? "PASS" : "FAIL"));
print("unicode: " + ("Ação".normalize("NFD").length === 6 ? "PASS" : "FAIL"));
print("intl:    " + (new Intl.NumberFormat("de-DE").format(1234567.5) === "1.234.567,5" ? "PASS" : "FAIL (" + new Intl.NumberFormat("de-DE").format(1234567.5) + ")"));
print("date:    " + (new Date(Date.UTC(2026, 10, 2)).toISOString() === "2026-11-02T00:00:00.000Z" ? "PASS" : "FAIL"));
print("typed:   " + (new Float64Array([1.5, 2.5]).reduce((a, b) => a + b) === 4 ? "PASS" : "FAIL"));
print("classes: " + (new (class A { get x() { return 7; } })().x === 7 ? "PASS" : "FAIL"));
var t0 = Date.now(); var s = 0; for (var i = 0; i < 1e7; i++) s += i; print("loop 1e7: " + (Date.now() - t0) + " ms (interpreter, no JIT)");
Promise.resolve(42).then(v => print("promise: " + (v === 42 ? "PASS" : "FAIL")));
print("smoke: end");
