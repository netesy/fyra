# PHPish Frontend Test Matrix

Feature | Expected behavior | Sample program (toy frontend syntax)
---|---|---
Unary negation (`-x`) | Produces arithmetic negation | `fn main(){ x = -5; return x + 9; }` => `4`
Unary logical not (`!x`) | Converts truthy/falsy to `0/1` inversion | `fn main(){ return !0 + !7; }` => `1`
Short-circuit `&&` | RHS not evaluated when LHS is false | `fn main(){ if 0 && (1/0) { return 1; } else { return 9; } }` => `9`
Short-circuit `||` | RHS not evaluated when LHS is true | `fn main(){ if 1 || (1/0) { return 7; } else { return 0; } }` => `7`
`break` in loop | Exits nearest loop | `for i = 0; i < 10; i = i + 1 { if i == 8 { break; } ... }`
`continue` in loop | Skips to next iteration | `for ... { if i % 2 == 1 { continue; } ... }`
Recursion | Function can call itself and return correct result | `fn fact(n){ if n <= 1 { return 1; } return n * fact(n-1); }`
Array literal | Allocates contiguous array and initializes elements | `a = [3,5,7,11];`
Array indexing read | Reads element by computed index | `return a[1] + a[3];`
Array indexing write | Writes element by computed index | `a[2] = 10; return a[2];`
Boolean literals (`true`/`false`) | Parse as canonical truthy/falsy constants (`1/0`) | `fn main(){ if true { return 5; } return 0; }` => `5`
Compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) | Performs read-modify-write update on scalar variable | `x = 10; x += 5; x *= 2; return x;` => `30`
Increment/decrement (`++`, `--`) | Mutates scalar variable by one | `i = 4; i++; i--; return i;` => `4`
Mutual recursion | Two functions calling each other produce expected result | `is_even`/`is_odd` recursion pair
Bitwise and shift ops (`&`, `|`, `^`, `<<`, `>>`) | Preserves integer bit semantics and precedence | `x=6&3; y=1<<4; ...`
Do-while loop | Executes body at least once and checks condition after body | `do { i++; } while i < 5;`
Function arity diagnostics | Reject mismatched call arity at frontend/lowering stage | `fn f(a,b)...; f(1);`
Constant array bounds diagnostics | Reject compile-time constant out-of-bounds array indices | `a=[1,2,3]; return a[5];`
Undefined variable diagnostics | Reject use-before-definition and out-of-scope-like unresolved names | `return missing_var;`
Runtime/interop syscall path | Lowers syscall ABI path and validates runtime interop return value | `pid = syscall(39,0,0,0,0,0,0); return pid > 0;`
Backend matrix parity (same source) | Same frontend source compiles in textual ASM mode + in-memory ELF mode across x64/AArch64/RISC-V (execute on x64) | `matrix_arith`, `matrix_control`
Functions + variables + arithmetic | Standard expression lowering works across calls | `fn calc(a,b){ ... } fn main(){ return calc(6,5); }`
If / elif / else | Correct branch selected | `if n<3 ... elif n<4 ... else ...`
While loop | Repeated execution while condition true | `while i <= 5 { ... }`
For loop | init/cond/step lowered correctly | `for i = 0; i < 5; i = i + 1 { ... }`
Malformed input diagnostics | Parser rejects invalid syntax | `fn nope( { return 1; }`
Voilet frontend WAT/WASM validity | Same frontend source lowers to valid WAT text and valid WASM binary header | `test_voilet_frontend` (`arith_and_call`, `if_while`)
