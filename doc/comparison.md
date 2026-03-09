# QBE ↔ Fyra Comparison Guide

This guide shows **side‑by‑side translations** between **QBE IL** and **Fyra IL**, with commentary. It includes small idioms (arithmetic, conditionals, loops, calls, memory) and ends with full mini‑programs (factorial & fibonacci).

> **Conventions used here**
>
> * **QBE** syntax is taken from the official docs.
> * **Fyra** follows the updated colon type syntax:
>
>   * Results are in SSA: `%r = op ... : T` where `T ∈ {w,l,s,d}`.
>   * Memory ops use **suffix types**: `loadX`, `loaduX`, `loadsX`, `storeX` with `X ∈ {b,h,w,l,s,d}`.
>   * For `storeb`/`storeh`, the value is `w`-typed; pointers are integers (usually `l`).
>   * Comparisons: `%r = <COND> %a, %b : w` where `COND ∈ {eq,ne,sgt,slt,sge,sle,ugt,ult,uge,ule, gt,lt,ge,le,o,uo}`.
>   * Control flow: `jmp`, `jnz cond, @t, @f`, `ret`, `hlt`.
>   * Calls: `%dst = call $callee(%a1 : T1, ...) : R` (with colon type syntax).
>   * `%r = phi @L1 %v1, @L2 %v2, ... : T`.

---

## 0) Quick reference: QBE ↔ Fyra mapping highlights

| Category              | QBE                                                                    | Fyra                                                                                                                                                             |                       |
| --------------------- | ---------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- |
| Add                   | `%r =w add %a, %b`                                                     | `%r = add %a, %b : w`                                                                                                                                               |
| Sub                   | `%r =w sub %a, %b`                                                     | `%r = sub %a, %b : w`                                                                                                                                               |
| Mul                   | `%r =w mul %a, %b`                                                     | `%r = mul %a, %b : w`                                                                                                                                               |
| Div (signed/unsigned) | `%r =w div %a, %b` / `udiv`                                            | `%r = div %a, %b : w` / `udiv`                                                                                                                                      |
| Rem (signed/unsigned) | `rem` / `urem`                                                         | `rem` / `urem` with `: w`                                                                                                                                                   |
| Bitwise               | `and/or/xor` (wl)                                                      | `and/or/xor` with `: type`                                                                                                                                                |
| Shifts                | `sar/shr/shl`                                                          | `sar/shr/shl` with `: type`                                                                                                                                                    |
| Load                  | `%r =d loadd %p` / `%r =w loaduw %p`                                   | `%r = load %p : d` / `%r = loaduw %p : w`                                                                                                                           |
| Store                 | `storew %val, %ptr`                                                    | `store %val, %ptr : w`                                                                                                                         |
| Blit                  | `blit %dst, %src, N` (N const)                                         | `blit %dst, %src, %count` (count may be reg)                                                                                                                     |
| Stack alloc           | `alloc{4,8,16} %size` → returns pointer                                | `%r = alloc %size : l` (N∈{4,8,16})                                                                                                                                |
| Compare (eq)          | `%r =w ceqw %a, %b`                                                    | `%r = eq %a, %b : w`                                                 |
| Compare (signed <)    | `%r =w csltw %a, %b`                                                   | `%r = slt %a, %b : w`                                                 |
| Floats compare        | `cgts/cges/...` or ordered `cod`/unordered `cuod`                      | `gt/ge/...` or `co/cuo` with `: w`                                                                                                                                          |
| Cast/Copy             | `cast`, `copy`                                                         | `cast`, `copy` with `: type`                                                                                                                                                   |
| Int extend            | `extub/extsb/extuh/extsh/extuw/extsw`                                  | `extub/extsb/...` with `: type`                                                                                                                                  |
| Float/int convert     | `swtof, sltof, uwtof, ultof, stosi, stoui, dtosi, dtoui, truncd, exts` | Same ops with `: type` syntax                                                                                                                            |
| Call                  | `%r =w call $f(w %a)` (optional result)                                | `%r = call $f(%a : w) : w`                                                                                                                                          |
| Variadic              | `vastart %ap` / `%r =w vaarg %ap`                                      | `vastart %ap` / `%r = vaarg %ap : w`                                                                                                                                                             |
| Phi                   | `%r =w phi @L1 %v1, @L2 %v2`                                           | `%r = phi @L1 %v1, @L2 %v2 : w`                                                                                                                                             |

> **Note**: The table above uses QBE mnemonics literally to avoid confusion. Fyra’s compare instruction is written `c<cond><type>`, e.g., `ceqw`, `csltw`, `cgtd`.

---

## 1) Arithmetic & simple return

**High‑level**

```c
int add(int a, int b) { return a + b; }
```

**QBE**

```qbe
function w $add(w %a, w %b) {
@start
    %c =w add %a, %b
    ret %c
}
```

**Fyra**

```fyra
function $add(%a : w, %b : w) : w {
@start
    %c = add %a, %b : w
    ret %c : w
}
```

**Notes**

* Fyra uses colon type syntax; function header uses `: w` for return type clarity.

---

## 2) Conditionals (if/else) with SSA `phi`

**High‑level**

```c
int f(int x) {
  int y;
  if (x) y = 1; else y = 2;
  return y;
}
```

**QBE**

```qbe
function w $f(w %x) {
@start
    jnz %x, @ift, @iff
@ift
    jmp @ret
@iff
    jmp @ret
@ret
    %y =w phi @ift 1, @iff 2
    ret %y
}
```

**Fyra**

```fyra
function $f(%x : w) : w {
@start
    jnz %x, @ift, @iff
@ift
    jmp @ret
@iff
    jmp @ret
@ret
    %y = phi @ift 1, @iff 2 : w
    ret %y : w
}
```

**Notes**

* Identical control‑flow and `phi` form.
* Alternative without phi (both ILs): store both branches to stack then load at join; less SSA‑pure but sometimes simpler in a frontend.

---

## 3) Integer comparisons & branching

**High‑level**

```c
int cmp(int a, int b) { return a < b; } // signed
```

**QBE**

```qbe
function w $cmp(w %a, w %b) {
@start
    %r =w csltw %a, %b   # signed less-than, word
    ret %r
}
```

**Fyra**

```fyra
function $cmp(%a : w, %b : w) : w {
@start
    %r = slt %a, %b : w   # Fyra uses simplified comparison names
    ret %r : w
}
```

**Unsigned**: use `cultw` in QBE; `cultw` (or `c ult w`) in Fyra.

**Floats**: `cgts/cgtd` (`cgt s|d` in Fyra). Ordered/unordered: `cod/cuod` in QBE; `o/uo` in Fyra.

---

## 4) Loops (countdown while)

**High‑level**

```c
int sum_down(int n) {
  int s = 0;
  while (n) { s += n; n--; }
  return s;
}
```

**QBE**

```qbe
function w $sum_down(w %n) {
@start
    %s =w copy 0
    jmp @loop
@loop
    %s =w add %s, %n
    %n =w sub %n, 1
    jnz %n, @loop, @end
@end
    ret %s
}
```

**Fyra**

```fyra
function $sum_down(%n : w) : w {
@start
    %s = copy 0 : w
    jmp @loop
@loop
    %s = add %s, %n : w
    %n = sub %n, 1 : w
    jnz %n, @loop, @end
@end
    ret %s : w
}
```

**Notes**

* QBE allows this non‑SSA style (self‑assign) and repairs SSA. Fyra supports the same syntax and also allows pure SSA using `phi` (shown below).

**Loop with explicit `phi`**

```fyra
function $sum_down_phi(%n0 : w) : w {
@start
    jmp @loop
@loop
    %n = phi @start %n0, @loop %n1 : w
    %s = phi @start 0, @loop %s1 : w
    %s1 = add %s, %n : w
    %n1 = sub %n, 1 : w
    jnz %n1, @loop, @end
@end
    ret %s1 : w
}
```

---

## 5) Function calls (direct, with/without result)

**High‑level**

```c
int add(int,int);
int g(void) { return add(40,2); }
void sink(int);
void h(void) { sink(7); }
```

**QBE**

```qbe
function w $g() {
@start
    %r =w call $add(w 40, w 2)
    ret %r
}

function $h() {
@start
    call $sink(w 7)
    ret
}
```

**Fyra**

```fyra
function $g() : w {
@start
    %r = call $add(40 : w, 2 : w) : w
    ret %r : w
}

function $h() : void {
@start
    call $sink(7 : w) : w
    ret
}
```

**Notes**

* Both allow omitted result for `void` call sites.
* Fyra’s bracketed destination makes the optionality explicit.

---

## 6) Memory: loads, stores, stack allocation, blit

### 6.1 Address‑taking local (stack) and load/store

**High‑level**

```c
int box(int x) {
  int y;
  y = x;
  return y;
}
```

**QBE**

```qbe
function w $box(w %x) {
@start
    %p =l alloc8 4          # get 8‑aligned 4 bytes
    storew %x, %p           # store word
    %y =w loadw %p          # load (any sign; sugar for loadsw)
    ret %y
}
```

**Fyra**

```fyra
function $box(%x : w) : w {
@start
    %p = alloc 4 : l          # pointer is integer‑typed (l on 64‑bit)
    store %x, %p : w          # store with colon type syntax
    %y = load %p : w          # load with colon type syntax
    ret %y : w
}
```

### 6.2 Sub‑word accesses and sign/zero‑extension

**High‑level**

```c
int widen_signed(char b) { return (int)b; }
int widen_unsigned(unsigned char b) { return (unsigned)b; }
```

**QBE**

```qbe
function w $widen_signed(w %b) {
@start
    %bw =w extsb %b    # sign‑extend sb→w
    ret %bw
}

function w $widen_unsigned(w %b) {
@start
    %bw =w extub %b    # zero‑extend ub→w
    ret %bw
}
```

**Fyra**

```fyra
function $widen_signed(%b : w) : w {
@start
    %bw = extsb %b : w    # sign‑extend sb→w
    ret %bw : w
}

function $widen_unsigned(%b : w) : w {
@start
    %bw = extub %b : w    # zero‑extend ub→w
    ret %bw : w
}
```

### 6.3 `blit` copies

**High‑level**

```c
struct P { int x, y; };
void copy(struct P* dst, struct P* src) { *dst = *src; }
```

**QBE**

```qbe
# Assume 2 * 4 bytes = 8 bytes
function $copy(l %dst, l %src) {
@start
    blit %dst, %src, 8    # byte count must be an immediate constant
    ret
}
```

**Fyra**

```fyra
function $copy(%dst : l, %src : l) : void {
@start
    blit %dst, %src, 8    # Fyra also permits a register count if desired
    ret
}
```

---

## 7) Floating‑point ops & conversions

**High‑level**

```c
double f(double x, float y) { return x + (double)y; }
```

**QBE**

```qbe
function d $f(d %x, s %y) {
@start
    %yd =d exts %y          # s → d
    %z  =d add %x, %yd
    ret %z
}
```

**Fyra**

```fyra
function $f(%x : d, %y : s) : d {
@start
    %yd = exts %y : d          # s → d
    %z = add %x, %yd : d
    ret %z : d
}
```

**Float/int conversions** (examples): `swtof`/`uwtof` (w→s), `sltof`/`ultof` (l→s), `stosi`/`stoui` (s→w), `dtosi`/`dtoui` (d→w), `truncd` (d→s), `exts` (s→d), `cast` (bitcast, same width).

---

## 8) Ordered / unordered float comparisons

**High‑level**

```c
int ok(double x) { return x == x; } // false for NaN
```

**QBE**

```qbe
function w $ok(d %x) {
@start
    %r =w cod %x, %x   # ordered?
    ret %r
}
```

**Fyra**

```fyra
function $ok(%x : d) : w {
@start
    %r = co %x, %x : w  # ordered comparison with colon syntax
    ret %r : w
}
```

**Notes**

* `cod`/`cuod` in QBE; `o`/`uo` in Fyra.

---

## 9) Variadic example (`printf`)

**High‑level**

```c
int main() { printf("n=%d\n", 7); return 0; }
```

**QBE**

```qbe
data $fmt = { b "n=%d\n", b 0 }

export function w $main() {
@start
    call $printf(l $fmt, ..., w 7)
    ret 0
}
```

**Fyra**

```fyra
data $fmt = { b "n=%d\n", b 0 }

export function $main() : w {
@start
    call $printf($fmt : l, ..., 7 : w) : w
    ret 0 : w
}
```

**Notes**

* For manual vararg walking, use `vastart %ap` then `%v =w vaarg %ap`.

---

## 10) Pure SSA idioms with `phi`

### 10.1 If‑else expression result

```fyra
# r = cond ? a : b
@start
    jnz %cond, @t, @f
@t
    jmp @join
@f
    jmp @join
@join
    %r = phi @t %a, @f %b : w
```

### 10.2 Loop carried state

```fyra
@start
    jmp @L
@L
    %i = phi @start 0, @L %i1 : w
    %s = phi @start 0, @L %s1 : w
    %s1 = add %s, %i : w
    %i1 = add %i, 1 : w
    jnz %i1, @L, @end
@end
    ret %s1 : w
```

---

## 11) End‑to‑end mini‑programs

### 11.1 Factorial (recursive)

**High‑level**

```c
int fact(int n){ return n<=1 ? 1 : n*fact(n-1); }
```

**QBE**

```qbe
export function w $fact(w %n) {
@start
    %cmp =w cslew %n, 1
    jnz %cmp, @base, @recur
@base
    ret 1
@recur
    %n1 =w sub %n, 1
    %r  =w call $fact(w %n1)
    %p  =w mul %n, %r
    ret %p
}
```

**Fyra**

```fyra
export function $fact(%n : w) : w {
@start
    %cmp = sle %n, 1 : w
    jnz %cmp, @base, @recur
@base
    ret 1 : w
@recur
    %n1 = sub %n, 1 : w
    %r = call $fact(%n1 : w) : w
    %p = mul %n, %r : w
    ret %p : w
}
```

### 11.2 Factorial (iterative with `phi`)

**QBE/Fyra (updated with colon syntax)**

```fyra
export function $fact_iter(%n0 : w) : w {
@start
    jmp @loop
@loop
    %n = phi @start %n0, @loop %n1 : w
    %a = phi @start 1, @loop %a1 : w
    %done = sle %n, 1 : w
    jnz %done, @end, @body
@body
    %a1 = mul %a, %n : w
    %n1 = sub %n, 1 : w
    jmp @loop
@end
    ret %a : w
}
```

### 11.3 Fibonacci (iterative)

**High‑level**

```c
int fib(int n){ int a=0,b=1; while(n--){ int t=a+b; a=b; b=t; } return a; }
```

**QBE**

```qbe
export function w $fib(w %n0) {
@start
    jmp @L
@L
    %n =w phi @start %n0, @L %n1
    %a =w phi @start 0,   @L %a1
    %b =w phi @start 1,   @L %b1
    %done =w ceqw %n, 0
    jnz %done, @end, @body
@body
    %t  =w add %a, %b
    %a1 =w copy %b
    %b1 =w copy %t
    %n1 =w sub %n, 1
    jmp @L
@end
    ret %a
}
```

**Fyra**

```fyra
export function $fib(%n0 : w) : w {
@start
    jmp @L
@L
    %n = phi @start %n0, @L %n1 : w
    %a = phi @start 0, @L %a1 : w
    %b = phi @start 1, @L %b1 : w
    %done = eq %n, 0 : w
    jnz %done, @end, @body
@body
    %t = add %a, %b : w
    %a1 = copy %b : w
    %b1 = copy %t : w
    %n1 = sub %n, 1 : w
    jmp @L
@end
    ret %a : w
}
```

---

## 12) Extra idioms & tips

* **Short‑circuit `&&` / `||`**: translate to control‑flow with `jnz` and `phi` to avoid evaluating RHS when not needed.
* **Pointer arithmetic**: emit `add`/`sub` on integer pointers (`l` on 64‑bit). For scaled indices, multiply by element size first.
* **Signed vs unsigned**: choose the right compare (`cslt*` vs `cult*`) and division (`div` vs `udiv`).
* **Subtyping**: a `l` value is valid where `w` is expected (low 32 bits used). The reverse requires an explicit extend.
* **`cast`**: bit‑reinterpretation only; widths must match.
* **`blit` size**: in QBE, the byte count must be an **immediate constant**. Keep it small or call `memcpy`. Fyra allows a register count but back‑ends may still lower to loops for large sizes.

---

## 13) Checklist when lowering high‑level code to Fyra

1. **Decide SSA style**: use `phi` or permissive self‑reassign with QBE‑style repair. Prefer `phi` for clarity.
2. **Choose integer signedness** for comparisons/divisions/remainders.
3. **Remember memory op argument order** in Fyra: `storeX %ptr, %val`; `loadX %dst =Y %ptr`.
4. **Keep pointers as integers** (`l` on 64‑bit) and do arithmetic explicitly.
5. **Minimize `blit` sizes** or call a runtime helper.
6. **Be explicit on conversions**: `ext*`, `truncd`, `exts`, `*to*` ops.
7. **Model `void` calls** with no destination in `call`.

---

### Appendix A — Common compare mnemonic pairs

| Intent               | QBE     | Fyra                   |
| -------------------- | ------- | ---------------------- |
| `a == b` (w)         | `ceqw`  | `eq ... : w`           |
| `a != b` (w)         | `cnew`  | `ne ... : w`           |
| `a < b` signed (w)   | `csltw` | `slt ... : w`          |
| `a < b` unsigned (w) | `cultw` | `ult ... : w`          |
| `a >= b` signed (l)  | `csgel` | `sge ... : w`          |
| `x > y` (double)     | `cgtd`  | `gt ... : w`           |
| ordered (double)     | `cod`   | `co ... : w`           |
| unordered (single)   | `cuos`  | `cuo ... : w`          |


