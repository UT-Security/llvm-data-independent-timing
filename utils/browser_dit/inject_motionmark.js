// Appended to MotionMark's developer.html so the page reports its own score back
// to the harness, matching the protocol inject.js uses for Speedometer: POST a
// JSON object to /result and let run_one_mm.py kill the browser.
//
// MotionMark's debug runner (developer.html) is used rather than the shipping
// index.html because the shipping suite has NO filter test - it is Multiply,
// Canvas Arcs, Leaves, Paths, Canvas Lines, Images, Design, Suits. The filter
// tests ("CSS bouncing filter circles", "Focus 2.0") live only in the debug
// runner's "HTML suite".
//
// Automation hook is benchmarkController.startBenchmarkImmediatelyIfEncoded(),
// which the page calls on load: it builds options from the query string and
// selects tests by case-insensitive REGEX over the name with \W stripped. Note
// it REPLACES the option set rather than merging, so the URL must carry every
// parameter benchmarkDefaultParameters would have supplied.
//
// Completion hook is benchmarkController.showResults(); results hang off
// benchmarkController.runnerClient.scoreCalculator.
(function () {
    "use strict";

    var posted = false;
    var startedAt = Date.now();

    function post(obj) {
        if (posted)
            return;
        posted = true;
        obj.wallMs = Date.now() - startedAt;
        obj.ua = navigator.userAgent;
        try {
            fetch("/result", {
                method: "POST",
                keepalive: true,
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(obj),
            });
        } catch (e) {
            location.href = "/result?error=" + encodeURIComponent(String(e));
        }
    }

    // A run that never starts looks exactly like a run that never finishes, and
    // the most likely cause is a suite-name/test-name regex matching nothing -
    // in which case startBenchmarkImmediatelyIfEncoded returns false and the
    // page just sits on the form. Report that as an error rather than letting
    // the harness time out with no explanation.
    var started = false;
    var pageErrors = [];
    window.addEventListener("error", function (e) {
        pageErrors.push(String(e.message) + " @" + e.filename + ":" + e.lineno);
    });
    // The guard must outlast a real run or it reports a false negative on a
    // benchmark that is merely still going: duration is warmup + ramp + the
    // test interval, so scale it off the interval actually requested rather
    // than a fixed constant. (A fixed 20s guard failed every test-interval=15
    // run while `matched:1` proved the test had been selected correctly.)
    var interval = 30;
    try {
        var m = /[?&]test-interval=(\d+)/.exec(location.search);
        if (m)
            interval = parseInt(m[1], 10);
    } catch (e) { /* keep the default */ }
    var guardMs = Math.max(60000, interval * 3000 + 45000);

    setTimeout(function () {
        if (started)
            return;
        // Report enough state to tell the three failure modes apart: the page
        // scripts never ran, initialize() threw before reaching the autostart,
        // or the suite/test regex matched nothing.
        var diag = {};
        try {
            diag.hasController = typeof globalThis.benchmarkController !== "undefined";
            diag.hasSuitesManager = typeof globalThis.suitesManager !== "undefined";
            diag.suiteCount = typeof Suites !== "undefined" ? Suites.length : null;
            diag.suiteNames = typeof Suites !== "undefined"
                ? Suites.map(function (s) { return s.name; }) : null;
            diag.optionsParsed = globalThis.benchmarkController
                ? globalThis.benchmarkController.options : null;
            if (globalThis.suitesManager && globalThis.suitesManager.suitesFromQueryString) {
                var o = globalThis.benchmarkController.options || {};
                diag.matched = globalThis.suitesManager
                    .suitesFromQueryString(o["suite-name"], o["test-name"]).length;
            }
        } catch (e) {
            diag.diagError = String(e);
        }
        diag.pageErrors = pageErrors;
        post({ ok: false, error: "benchmark never started", diag: diag });
    }, guardMs);

    var poll = setInterval(function () {
        var c = globalThis.benchmarkController;
        if (!c || c.__ditPatched)
            return;
        c.__ditPatched = true;
        clearInterval(poll);

        var origStart = c._startBenchmark && c._startBenchmark.bind(c);
        if (origStart) {
            c._startBenchmark = function () {
                started = true;
                return origStart.apply(c, arguments);
            };
        }
        // initialize() may already have called _startBenchmark before this
        // script got to patch it, so the patch alone can miss a live run.
        // runnerClient is created by the run itself and is the durable signal.
        setInterval(function () {
            if (c.runnerClient)
                started = true;
        }, 200);

        var origShow = c.showResults.bind(c);
        c.showResults = function () {
            var rv;
            try {
                rv = origShow();
            } catch (e) {
                /* still report below */
            }
            try {
                var sc = c.runnerClient && c.runnerClient.scoreCalculator;
                var score = sc ? sc.score : null;
                // One test per run (the selector breaks after the first match in
                // a suite), so the overall score IS that test's score. Bounds are
                // MotionMark's own bootstrap CI and are worth keeping: an
                // adaptive benchmark's spread is part of the reading.
                post({
                    ok: !!(score && isFinite(score) && score > 0),
                    score: score,
                    scoreLowerBound: sc ? sc.scoreLowerBound : null,
                    scoreUpperBound: sc ? sc.scoreUpperBound : null,
                    testNames: (function () {
                        try {
                            return (c.suites || []).map(function (s) {
                                return s.name + ": " + (s.tests || []).map(function (t) {
                                    return t.name;
                                }).join(",");
                            });
                        } catch (e) { return null; }
                    })(),
                });
            } catch (e) {
                post({ ok: false, error: "extract: " + String(e) });
            }
            return rv;
        };
    }, 50);
})();
