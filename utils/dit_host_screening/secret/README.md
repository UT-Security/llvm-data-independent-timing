# Composite hosts: real public workload + real secret payload

Measures **oracle placement vs always-on DIT** on four hosts. Results and
analysis: `docs/results/dit-oracle-composites.md`.

## Build

```sh
# libsecp256k1 (the secret payload)
git clone --depth 1 https://github.com/bitcoin-core/secp256k1.git
cd secp256k1 && clang -O2 -c -DECMULT_WINDOW_SIZE=15 -DECMULT_GEN_KB=86 \
    -I src -I include src/secp256k1.c src/precomputed_ecmult.c src/precomputed_ecmult_gen.c
ar rcs libsecp256k1.a secp256k1.o precomputed_ecmult.o precomputed_ecmult_gen.o

# hosts (adjust -I paths to your checkouts)
clang -O2 -I<lua>/src        -I<secp>/include -I. host_lua.c     secret_payload.c <lua>/src/liblua.a libsecp256k1.a -o host_lua -lm
clang -O2 -I<sqlite>         -I<secp>/include -I. host_sqlite.c  secret_payload.c <sqlite>/sqlite3.c libsecp256k1.a -o host_sqlite -DSQLITE_THREADSAFE=0
clang -O2 -I<cpy>/Include -I<cpy> -I<secp>/include -I. host_cpython.c secret_payload.c libsecp256k1.a <cpy>/libpython3.16.a -liconv -ldl -framework CoreFoundation -o host_cpython
clang -O2 -I<qjs>            -I<secp>/include -I. host_quickjs.c secret_payload.c <qjs>/libquickjs.a libsecp256k1.a -o host_quickjs -lm -lpthread
```

`work.js` is built by concatenating Octane `base.js richards.js deltablue.js
splay.js` and appending `work_js_driver_tail.js`.

CPython needs `PYTHONHOME=<cpy>` and `PYTHONPATH=<cpy>/Lib:<cpy>/build/lib.<plat>`.

## Run

```sh
python3 run_oracle.py 16 2      # 16 reps, 2 burn-in -> ../out/oracle.csv
python3 analyze_oracle.py
```

Each host binary also runs standalone: `./host_lua work.lua <mode> 17 1`, where
mode is `0`=off, `1`=always-on, `2`=oracle.

## Design rules that matter

- **One binary per host, DIT mode from argv.** No codegen differs between arms,
  so `dit-measurement-traps` trap 7b cannot apply.
- **Arm order is rotated every rep.** A fixed order silently penalised whichever
  arm ran last by ~1.3% and made QuickJS look like it recovered only 67.6% when
  the true figure is ~100%. The `off2` arm - a second run of the identical `off`
  arm - measures that floor on every run.
- **`SIGS=0` is the sanity control.** With zero signatures the oracle arm
  executes zero `MSR DIT` and does zero secret work, so it must equal `off`.
  If it does not, the rig is biased, not the CPU.
- **Coverage is structural.** The secret key is one static read only inside
  `secret_sign_n` between the enable and disable, so the oracle protects 100% of
  secret work by construction - the check that SQLCipher's retracted "+8.15%"
  failed.

`oracle_rotated.csv` is the run behind the published table. The earlier
fixed-order run was overwritten; its numbers survive only as quoted in the
results doc.
