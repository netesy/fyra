#!/usr/bin/env python3
"""Execution regressions for local calls and x64 scalar floating point."""

import pathlib
import os
import re
import subprocess
import sys
import tempfile


COMPILER, CC = sys.argv[1:3]


def compile_and_run(name: str, source: str, expected: int, required=(), opt="-O2"):
    with tempfile.TemporaryDirectory(prefix=f"fyra-{name}-") as td:
        root = pathlib.Path(td)
        src, asm, exe = root / f"{name}.fyra", root / f"{name}.s", root / name
        src.write_text(source)
        subprocess.run([COMPILER, str(src), opt, "--target", "x64-linux-bin", "-o", str(asm)], check=True)
        text = asm.read_text()
        for spelling in required:
            assert spelling in text, f"{name}: missing assembly evidence {spelling!r}"
        harness = root / "harness.c"
        harness.write_text('#include <stdio.h>\nvoid print_checksum(long long x){printf("checksum: %lld\\n",x);}\n')
        subprocess.run([CC, "-no-pie", str(asm), str(harness), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], text=True, capture_output=True, check=True)
        assert result.stdout.strip() == f"checksum: {expected}", (name, result.stdout, result.stderr)


# Compact annotations are deliberate: this is the smallest historical failure.
# The large helper remains a real non-tail call after O2; helper2 establishes
# transitive reachability and two call sites share helper.
compile_and_run("calls", r"""
function $helper2(%x:i16):i64 {
@entry
  %w = extsh %x:i64
  %a = add %w,11:i64
  ret %a:i64
}
function $helper(%x:i16,%y:i64):i64 {
@entry
  %a = call $helper2(%x):i64
  %b = add %a,%y:i64
  %c = mul %b,3:i64
  %d = xor %c,%y:i64
  %e = add %d,7:i64
  %f = mul %e,5:i64
  %g = sub %f,%a:i64
  %h0 = xor %g,17:i64
  %h1 = add %h0,19:i64
  %h2 = xor %h1,23:i64
  %h3 = add %h2,29:i64
  %h4 = xor %h3,31:i64
  %h5 = add %h4,37:i64
  %h6 = xor %h5,41:i64
  %h7 = add %h6,43:i64
  %h8 = xor %h7,47:i64
  %h9 = add %h8,53:i64
  %h10 = xor %h9,59:i64
  %h11 = add %h10,61:i64
  %h12 = xor %h11,67:i64
  %h13 = add %h12,71:i64
  %h14 = xor %h13,73:i64
  %h15 = add %h14,79:i64
  %h16 = xor %h15,83:i64
  %h17 = add %h16,89:i64
  %h18 = xor %h17,97:i64
  %h19 = add %h18,101:i64
  ret %h19:i64
}
export function $main(%argc:i32):i32 {
@entry
  %arg16 = truncd %argc:i16
  %arg64 = extsw %argc:i64
  %y0 = add %arg64,99:i64
  %y1 = add %arg64,199:i64
  %x1 = add %arg16,4:i16
  %a = call $helper(%arg16,%y0):i64
  %b = call $helper(%x1,%y1):i64
  %sum = add %a,%b:i64
  %out = add %sum,1:i64
  call $print_checksum(i64 %out)
  ret 0:i32
}
""", 5711, ("helper:", "helper2:", "call helper"))

compile_and_run("transitive_calls", r"""
function $leaf(%x:i32):i32 { @entry %r = add %x,2:i32 ret %r:i32 }
function $middle(%x:i32):i32 { @entry %r = call $leaf(%x):i32 %s = mul %r,3:i32 ret %s:i32 }
export function $main():i32 { @entry %r = call $middle(5):i32 call $print_checksum(i32 %r) ret 0:i32 }
""", 21, ("call leaf", "call middle"), opt="-O0")


# 0/0 and +/-1/0 construct exceptional values without non-portable literals.
# Ordered comparisons must reject unordered operands; != must accept them.
compile_and_run("fp64", r"""
export function $main() : i32 {
@entry
  %a = fadd d_1.5,d_2.25:f64
  %b = fsub %a,d_0.5:f64
  %c = fmul %b,d_2.0:f64
  %nan = fdiv d_0.0,d_0.0:f64
  %pinf = fdiv d_1.0,d_0.0:f64
  %ninf = fdiv d_-1.0,d_0.0:f64
  %eq = eq %c,d_6.5:i32
  %nan_ne = ne %nan,%nan:i32
  %nan_lt = lt %nan,d_1.0:i32
  %inf_gt = gt %pinf,%c:i32
  %ninf_lt = lt %ninf,%c:i32
  %zero_eq = eq d_0.0,d_-0.0:i32
  %s0 = add %eq,%nan_ne:i32
  %s1 = add %s0,%nan_lt:i32
  %s2 = add %s1,%inf_gt:i32
  %s3 = add %s2,%ninf_lt:i32
  %s4 = add %s3,%zero_eq:i32
  call $print_checksum(i32 %s4)
  ret 0:i32
}
""", 5, ("addsd", "subsd", "mulsd", "ucomisd"))

compile_and_run("fp32", r"""
export function $main() : i32 {
@entry
  %a = fadd f32_1.5,f32_2.0:f32
  %b = fsub %a,f32_0.5:f32
  %c = fmul %b,f32_2.0:f32
  %ok = eq %c,f32_6.0:i32
  call $print_checksum(i32 %ok)
  ret 0:i32
}
""", 1, ("addss", "subss", "mulss", "ucomiss"))

compile_and_run("fp_call", r"""
function $fp_helper(%x:f64,%n:i32):f64 {
@entry
  %y = fmul %x,d_2.0:f64
  %z = swtof %n:f64
  %r = fadd %y,%z:f64
  ret %r:f64
}
export function $main():i32 {
@entry
  %x = copy d_1.5:f64
  %r = call $fp_helper(%x,2):f64
  %ok = eq %r,d_5.0:i32
  call $print_checksum(i32 %ok)
  ret 0:i32
}
""", 1, ("call fp_helper", "movsd %xmm0"), opt="-O0")


def spill_pressure(fp_type: str, literal_prefix: str, move: str):
    """Force 20 source values live across a later reduction and execute it."""
    lines = ["export function $main(%argc:i32):i32 {", "@entry"]
    for index in range(20):
        lines += [f"  %i{index} = add %argc,{index}:i32",
                  f"  %f{index} = swtof %i{index}:{fp_type}"]
    current = "%f0"
    for index in range(1, 20):
        lines.append(f"  %s{index} = fadd {current},%f{index}:{fp_type}")
        current = f"%s{index}"
    lines += [f"  %ok = eq {current},{literal_prefix}210.0:i32",
              "  call $print_checksum(i32 %ok)", "  ret 0:i32", "}"]

    with tempfile.TemporaryDirectory(prefix=f"fyra-spill-{fp_type}-") as td:
        root = pathlib.Path(td)
        src, asm, exe = root / "spill.fyra", root / "spill.s", root / "spill"
        src.write_text("\n".join(lines) + "\n")
        env = dict(os.environ, FYRA_REGALLOC_DIAG="1")
        built = subprocess.run(
            [COMPILER, str(src), "-O0", "--target", "x64-linux-bin", "-o", str(asm)],
            check=True, text=True, capture_output=True, env=env)
        spills = built.stdout.count("[RegAlloc spill-decision]")
        reloads = built.stdout.count("[RegAlloc reload]")
        assert spills >= 1 and reloads >= 1, (fp_type, spills, reloads, built.stdout)
        assembly = asm.read_text()
        assert re.search(rf"{move} %xmm15, -[0-9]+\(%rbp\)", assembly)
        assert re.search(rf"{move} -[0-9]+\(%rbp\), %xmm15", assembly)
        harness = root / "harness.c"
        harness.write_text('#include <stdio.h>\nvoid print_checksum(long long x){printf("checksum: %lld\\n",x);}\n')
        subprocess.run([CC, "-no-pie", str(asm), str(harness), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], text=True, capture_output=True, check=True)
        assert result.stdout.strip() == "checksum: 1", (fp_type, result.stdout, result.stderr)


spill_pressure("f32", "f32_", "movss")
spill_pressure("f64", "d_", "movsd")
