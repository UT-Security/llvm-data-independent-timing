/* Composite host: embedded CPython (PUBLIC) + libsecp256k1 signing (SECRET).
 *
 * The strongest real deployment story on the table: a Python web application
 * that signs a session cookie or JWT per request. Interpreter work dominates,
 * signing is a tiny rare chunk, and the signature is published - so the
 * declassification boundary is the protocol's, not the harness's.
 *
 * `_secret.sign(n)` is exposed to Python via PyImport_AppendInittab.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "secret_payload.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double g_secret_s;

static PyObject *py_sign(PyObject *self, PyObject *args) {
    int n = 1;
    if (!PyArg_ParseTuple(args, "|i", &n)) return NULL;
    double t0 = now_s();
    unsigned long r = secret_sign_n(n);
    g_secret_s += now_s() - t0;
    return PyLong_FromUnsignedLong(r & 0x7fffffffUL);
}

static PyMethodDef methods[] = {
    {"sign", py_sign, METH_VARARGS, "sign n messages with the secret key"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef moddef = {
    PyModuleDef_HEAD_INIT, "_secret", NULL, -1, methods, NULL, NULL, NULL, NULL
};

static PyObject *init_secret(void) { return PyModule_Create(&moddef); }

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "work.py";
    int mode = argc > 2 ? atoi(argv[2]) : DIT_OFF;
    const char *a1 = argc > 3 ? argv[3] : "1";

    secret_init(mode);

    PyImport_AppendInittab("_secret", init_secret);
    Py_Initialize();

    /* make the script's directory importable so the pyperformance bodies load */
    PyRun_SimpleString("import sys; sys.path.insert(0, '.'); sys.path.insert(0, '../py')");
    char buf[256];
    snprintf(buf, sizeof buf, "SIGS = %s", a1);
    PyRun_SimpleString(buf);

    FILE *f = fopen(script, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", script); return 1; }

    double t0 = now_s();
    int rc = PyRun_SimpleFile(f, script);
    double total = now_s() - t0;
    fclose(f);

    printf("HOST cpython mode=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "signs=%lu toggles=%lu dit_now=%lu rc=%d\n",
           mode, total, g_secret_s, 100.0 * g_secret_s / total,
           secret_count(), secret_toggles(), secret_dit_now(), rc);
    return rc != 0;
}
