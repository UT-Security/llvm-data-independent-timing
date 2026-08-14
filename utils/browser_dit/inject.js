// Appended to the local Speedometer 3.1 index.html so the page reports its own
// score back to the harness. Avoids needing geckodriver/selenium (neither is
// installed, and a WebDriver-controlled browser is one more thing that can
// differ between arms).
//
// Speedometer 3.1 has no "SpeedometerDone" event (that landed in 4.0), so we
// patch the two documented BenchmarkClient callbacks instead:
//   didFinishLastIteration(metrics) - success
//   handleError(error)              - failure
// This script is a classic (non-module) script, so it runs BEFORE the deferred
// main.mjs module that creates globalThis.benchmarkClient. Hence the poll.
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
            // Last resort: the harness also treats a navigation to /result as a
            // report, so a broken fetch still terminates the run.
            location.href = "/result?error=" + encodeURIComponent(String(e));
        }
    }

    var poll = setInterval(function () {
        var client = globalThis.benchmarkClient;
        if (!client || client.__ditPatched)
            return;
        client.__ditPatched = true;
        clearInterval(poll);

        var origDone = client.didFinishLastIteration.bind(client);
        client.didFinishLastIteration = function (metrics) {
            var rv;
            try {
                rv = origDone(metrics);
            } catch (e) {
                /* still report the score below */
            }
            try {
                var score = metrics && metrics.Score;
                var el = document.getElementById("result-number");
                post({
                    ok: !!(score && isFinite(score.mean) && score.mean > 0),
                    score: score ? score.mean : null,
                    delta: score ? score.delta : null,
                    percentDelta: score ? score.percentDelta : null,
                    values: score ? score.values : null,
                    displayed: el ? el.textContent : null,
                });
            } catch (e) {
                post({ ok: false, error: "extract: " + String(e) });
            }
            return rv;
        };

        var origErr = client.handleError ? client.handleError.bind(client) : null;
        client.handleError = function (error) {
            post({ ok: false, error: "benchmark: " + String((error && error.message) || error) });
            if (origErr)
                return origErr(error);
        };
    }, 50);
})();
