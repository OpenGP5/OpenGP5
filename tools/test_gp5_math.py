#!/usr/bin/env python3
"""Compare the DSP platform's maths functions (platform/dsp/gp5_math.c) with the computer's C library.
Needs a C compiler (cc). Prints the worst error per function; exits 1 if one is outside its limit."""
import pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FUNCS = ["sinf", "cosf", "tanf", "expf", "exp2f", "logf", "log2f", "log10f", "powf", "tanhf", "floorf", "fmodf"]
TEST = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
''' + "".join(f"float gp5_{f}(float{', float' if f in ('powf', 'fmodf') else ''});\n" for f in FUNCS) + r'''
static double worst; static float wx, wy;
static void cmp(float got, double want, float x, float y, int rel) {
    double e = rel ? fabs(got - want) / (fabs(want) > 1e-30 ? fabs(want) : 1e-30) : fabs(got - want);
    if (e > worst) { worst = e; wx = x; wy = y; }
}
#define RUN(name, lo, hi, n, expr, ref, rel, lim) do { worst = 0; \
    for (int i = 0; i <= n; i++) { float x = lo + (hi - lo) * i / (float)n; cmp(expr, ref, x, 0, rel); } \
    printf("%-7s %-5s worst %.2e at %g  limit %.0e %s\n", name, rel ? "rel" : "abs", worst, wx, lim, worst <= lim ? "ok" : "FAIL"); \
    bad |= worst > lim; } while (0)
int main(void) {
    int bad = 0;
    RUN("sinf", -100.f, 100.f, 200000, gp5_sinf(x), sin(x), 0, 2e-6);
    RUN("cosf", -100.f, 100.f, 200000, gp5_cosf(x), cos(x), 0, 2e-6);
    RUN("tanf", -1.5f, 1.5f, 100000, gp5_tanf(x), tan(x), 1, 5e-6);
    RUN("expf", -80.f, 80.f, 200000, gp5_expf(x), exp(x), 1, 2e-6);
    RUN("exp2f", -120.f, 120.f, 200000, gp5_exp2f(x), exp2(x), 1, 1e-6);
    RUN("logf", 1e-6f, 1e6f, 200000, gp5_logf(x), log(x), 0, 2e-6);
    RUN("log2f", 1e-30f, 10.f, 200000, gp5_log2f(x), log2(x), 0, 2e-6);
    RUN("log10f", 1e-6f, 1e6f, 200000, gp5_log10f(x), log10(x), 0, 1e-6);
    RUN("powf", 0.01f, 100.f, 100000, gp5_powf(x, 1.7f), pow(x, 1.7), 1, 3e-6);
    RUN("tanhf", -12.f, 12.f, 200000, gp5_tanhf(x), tanh(x), 1, 2e-6);
    RUN("floorf", -1000.f, 1000.f, 100003, gp5_floorf(x), floor(x), 0, 0.0);
    RUN("fmodf", -50.f, 50.f, 100003, gp5_fmodf(x, 3.3f), fmod(x, (double)3.3f), 0, 1e-5);
    return bad;
}
'''

def main():
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        (d / "t.c").write_text(TEST)
        ren = [f"-D{f}=gp5_{f}" for f in FUNCS]
        subprocess.run(["cc", "-O1", "-fno-builtin", "-c", *ren, f"-I{ROOT/'platform/dsp'}", str(ROOT / "platform/dsp/gp5_math.c"),
                        "-o", str(d / "m.o")], check=True)
        subprocess.run(["cc", "-O1", str(d / "t.c"), str(d / "m.o"), "-lm", "-o", str(d / "t")], check=True)
        return subprocess.run([str(d / "t")]).returncode

if __name__ == "__main__":
    sys.exit(main())
