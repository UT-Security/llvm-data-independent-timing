    f(current);
    current = current.right;
  }
};

// ---- composite driver: PUBLIC Octane work interleaved with SECRET signing ----
// One signature per benchmark "request", the shape of a JS service issuing a
// signed token per unit of work.
var __acc = 0;
SplaySetup();
for (var round = 0; round < 140; round++) {
  runRichards();
  if (SIGS) __acc += sign(SIGS);
  deltaBlue();
  if (SIGS) __acc += sign(SIGS);
  SplayRun();
  if (SIGS) __acc += sign(SIGS);
}
SplayTearDown();
globalThis.__result = __acc % 1000000;
