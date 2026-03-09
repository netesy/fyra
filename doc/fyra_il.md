# Fyra Intermediate Language (IL)

The **Fyra Intermediate Language (IL)** is a compact, SSA-based intermediate representation for compilers. It is designed to be **minimal, orthogonal, and efficient**, while still being **expressive enough** to translate high-level constructs directly.

Inspired by **QBE IL**, Fyra uses a **three-address, SSA-based design** with explicit typing and extends the QBE instruction set with additional optimizations and target-specific features.

---

## 1. Identifiers & Syntax

* `$name` → Global symbol (function, global data).
* `%tmp` → Function-local SSA temporary.
* `@L1` → Basic block label.
* Comments: `# text`

**Function Declaration Syntax:**
```fyra
function $name(%param1 : T1, %param2 : T2, ...) : ReturnType {
@label
    instructions...
}
```

**Export Functions:**
```fyra
export function $main() : w {
    # exported functions are visible to linker
}
```

---

## 2. Types

| Code | Meaning                      | Size | Notes                     |
| ---- | ---------------------------- | ---- | ------------------------- |
| `w`  | 32-bit integer               | 4 B  | signed/unsigned           |
| `l`  | 64-bit integer               | 8 B  | signed/unsigned, pointers |
| `s`  | 32-bit float                 | 4 B  | IEEE-754                  |
| `d`  | 64-bit float                 | 8 B  | IEEE-754                  |
| `b`  | 8-bit integer (memory only)  | 1 B  | zero/sign-extend          |
| `h`  | 16-bit integer (memory only) | 2 B  | zero/sign-extend          |

Pointers are represented as `l` (on 64-bit targets).

---

## 3. Constants

* Integers: `0`, `-5`, `123`
* Floating-point: `s_1.0`, `d_2.71828`, `d_1e-5`
* Global symbol addresses: `$foo`

---

## 4. Instructions Reference

### 4.1 Arithmetic Operations

#### Integer Arithmetic

| Instruction         | Description                | Example               |
| ------------------- | -------------------------- | --------------------- |
| `%r = add %a, %b : T`  | Addition                   | `%sum = add %x, %y : w`  |
| `%r = sub %a, %b : T`  | Subtraction                | `%diff = sub %a, %b : l` |
| `%r = mul %a, %b : T`  | Multiplication             | `%prod = mul %x, %y : w` |
| `%r = div %a, %b : T`  | Signed division            | `%q = div %a, %b : w`    |
| `%r = udiv %a, %b : T` | Unsigned integer division  | `%uq = udiv %a, %b : w`  |
| `%r = rem %a, %b : T`  | Signed remainder           | `%r = rem %a, %b : w`    |
| `%r = urem %a, %b : T` | Unsigned remainder         | `%r = urem %a, %b : w`   |
| `%r = neg %a : T`      | Negation                   | `%inv = neg %x : w`      |

#### Floating-Point Arithmetic

| Instruction          | Description                | Example               |
| -------------------- | -------------------------- | --------------------- |
| `%r = fadd %a, %b : T`  | Floating-point addition    | `%sum = fadd %x, %y : s` |
| `%r = fsub %a, %b : T`  | Floating-point subtraction | `%diff = fsub %a, %b : d`|
| `%r = fmul %a, %b : T`  | Floating-point multiplication | `%prod = fmul %x, %y : s` |
| `%r = fdiv %a, %b : T`  | Floating-point division    | `%q = fdiv %a, %b : d`   |

#### Bitwise Operations

| Instruction         | Description                | Example               |
| ------------------- | -------------------------- | --------------------- |
| `%r = and %a, %b : T`  | Bitwise AND                | `%mask = and %a, %b : w` |
| `%r = or %a, %b : T`   | Bitwise OR                 | `%flag = or %x, %y : l`  |
| `%r = xor %a, %b : T`  | Bitwise XOR                | `%x2 = xor %a, %b : w`   |
| `%r = shl %a, %b : T`  | Shift left                 | `%dbl = shl %a, 1 : w`   |
| `%r = shr %a, %b : T`  | Logical shift right        | `%shr = shr %a, 2 : l`   |
| `%r = sar %a, %b : T`  | Arithmetic shift right     | `%shr = sar %a, 1 : w`   |

---

### 4.2 Memory Operations

#### Stack Allocation

| Instruction              | Description                                      | Example              |
| ------------------------ | ------------------------------------------------ | -------------------- |
| `%r = alloc %size : l`      | Allocate stack memory (8-byte aligned)          | `%buf = alloc 64 : l`   |
| `%r = alloc4 %size : l`     | Allocate stack memory (4-byte aligned)          | `%buf = alloc4 16 : l`  |
| `%r = alloc16 %size : l`    | Allocate stack memory (16-byte aligned)         | `%buf = alloc16 64 : l` |

#### Load Operations

| Instruction               | Description                              | Example               |
| ------------------------- | ---------------------------------------- | --------------------- |
| `%r = load %ptr : T`        | Load value from memory                   | `%x = load %p : w`       |
| `%r = loadw %ptr : w`       | Load 32-bit word                         | `%x = loadw %p : w`      |
| `%r = loadl %ptr : l`       | Load 64-bit long                         | `%x = loadl %p : l`      |
| `%r = loads %ptr : s`       | Load 32-bit float                        | `%x = loads %p : s`      |
| `%r = loadd %ptr : d`       | Load 64-bit double                       | `%x = loadd %p : d`      |
| `%r = loadub %ptr : w`      | Load unsigned byte (zero-extend to w)    | `%b = loadub %p : w`     |
| `%r = loadsb %ptr : w`      | Load signed byte (sign-extend to w)      | `%b = loadsb %p : w`     |
| `%r = loaduh %ptr : w`      | Load unsigned halfword (zero-extend)     | `%h = loaduh %p : w`     |
| `%r = loadsh %ptr : w`      | Load signed halfword (sign-extend)       | `%h = loadsh %p : w`     |
| `%r = loaduw %ptr : l`      | Load unsigned word (zero-extend to l)    | `%w = loaduw %p : l`     |

#### Store Operations

| Instruction               | Description                              | Example               |
| ------------------------- | ---------------------------------------- | --------------------- |
| `store %val, %ptr : T`        | Store value to memory                    | `store %x, %p : w`        |
| `storew %val, %ptr : w`       | Store 32-bit word                        | `storew %x, %p : w`       |
| `storel %val, %ptr : l`       | Store 64-bit long                        | `storel %x, %p : l`       |
| `stores %val, %ptr : s`       | Store 32-bit float                       | `stores %x, %p : s`       |
| `stored %val, %ptr : d`       | Store 64-bit double                      | `stored %x, %p : d`       |
| `storeh %val, %ptr : h`       | Store 16-bit halfword                    | `storeh %x, %p : h`       |
| `storeb %val, %ptr : b`       | Store 8-bit byte                         | `storeb %x, %p : b`       |

#### Memory Copy

| Instruction               | Description                              | Example               |
| ------------------------- | ---------------------------------------- | --------------------- |
| `blit %dst, %src, %count` | Copy memory (like `memcpy`)              | `blit %dst, %src, 16` |

---

### 4.3 Comparison Operations

#### Integer Comparisons

**Signed Integer Comparisons:**

| Instruction           | Description                 | Example                 |
| --------------------- | --------------------------- | ----------------------- |
| `%r = eq %a, %b : w`    | Equal                       | `%cmp = eq %a, %b : w`    |
| `%r = ne %a, %b : w`    | Not equal                   | `%cmp = ne %a, %b : w`    |
| `%r = slt %a, %b : w`   | Signed less than            | `%cmp = slt %a, %b : w`   |
| `%r = sle %a, %b : w`   | Signed less than or equal   | `%cmp = sle %a, %b : w`   |
| `%r = sgt %a, %b : w`   | Signed greater than         | `%cmp = sgt %a, %b : w`   |
| `%r = sge %a, %b : w`   | Signed greater than or equal| `%cmp = sge %a, %b : w`   |

**Unsigned Integer Comparisons:**

| Instruction           | Description                   | Example                 |
| --------------------- | ----------------------------- | ----------------------- |
| `%r = ult %a, %b : w`   | Unsigned less than            | `%cmp = ult %a, %b : w`   |
| `%r = ule %a, %b : w`   | Unsigned less than or equal   | `%cmp = ule %a, %b : w`   |
| `%r = ugt %a, %b : w`   | Unsigned greater than         | `%cmp = ugt %a, %b : w`   |
| `%r = uge %a, %b : w`   | Unsigned greater than or equal| `%cmp = uge %a, %b : w`   |

#### Floating-Point Comparisons

| Instruction           | Description                 | Example                 |
| --------------------- | --------------------------- | ----------------------- |
| `%r = eq %a, %b : w`   | Float equal                 | `%cmp = eq %a, %b : w`   |
| `%r = ne %a, %b : w`   | Float not equal             | `%cmp = ne %a, %b : w`   |
| `%r = lt %a, %b : w`    | Float less than             | `%cmp = lt %a, %b : w`    |
| `%r = le %a, %b : w`    | Float less than or equal    | `%cmp = le %a, %b : w`    |
| `%r = gt %a, %b : w`    | Float greater than          | `%cmp = gt %a, %b : w`    |
| `%r = ge %a, %b : w`    | Float greater than or equal | `%cmp = ge %a, %b : w`    |
| `%r = co %a, %b : w`     | Ordered (neither operand is NaN) | `%cmp = co %a, %b : w` |
| `%r = cuo %a, %b : w`    | Unordered (at least one operand is NaN) | `%cmp = cuo %a, %b : w` |

#### Type-Specific Comparison Shortcuts

Fyra also supports type-specific comparison shortcuts:

```fyra
# Word (32-bit) comparisons
%cmp1 = ceqw %a, %b : w       # int equality
%cmp2 = csltw %a, %b : w      # signed less than
%cmp3 = cultw %a, %b : w      # unsigned greater

# Long (64-bit) comparisons  
%cmp4 = ceql %a, %b : w       # long equality
%cmp5 = csltl %a, %b : w      # signed less than (64-bit)

# Float comparisons
%cmp6 = ceqs %a, %b : w       # single precision equality
%cmp7 = ceqd %a, %b : w       # double precision equality
```

---

### 4.4 Type Conversion Operations

#### Integer Extensions

| Instruction            | Description                    | Example                  |
| ---------------------- | ------------------------------ | ------------------------ |
| `%r = extub %a : w`       | Zero-extend byte to word       | `%r = extub %x : w`         |
| `%r = extuh %a : w`       | Zero-extend halfword to word   | `%r = extuh %x : w`         |
| `%r = extuw %a : l`       | Zero-extend word to long       | `%r = extuw %x : l`         |
| `%r = extsb %a : w`       | Sign-extend byte to word       | `%r = extsb %x : w`         |
| `%r = extsh %a : w`       | Sign-extend halfword to word   | `%r = extsh %x : w`         |
| `%r = extsw %a : l`       | Sign-extend word to long       | `%r = extsw %x : l`         |

#### Floating-Point Conversions

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = exts %a : d`        | Float extend single→double         | `%d = exts %sval : d`       |
| `%r = truncd %a : s`      | Float truncate double→single       | `%s = truncd %dval : s`     |

#### Integer/Float Conversions

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = swtof %a : s`       | Signed word to float                | `%f = swtof %ival : s`      |
| `%r = uwtof %a : s`       | Unsigned word to float              | `%f = uwtof %ival : s`      |
| `%r = sltof %a : s`       | Signed long to float                | `%f = sltof %lval : s`      |
| `%r = ultof %a : s`       | Unsigned long to float              | `%f = ultof %lval : s`      |
| `%r = dtosi %a : w`       | Double to signed int                | `%i = dtosi %dval : w`      |
| `%r = dtoui %a : w`       | Double to unsigned int              | `%i = dtoui %dval : w`      |
| `%r = stosi %a : w`       | Single to signed int                | `%i = stosi %sval : w`      |
| `%r = stoui %a : w`       | Single to unsigned int              | `%i = stoui %sval : w`      |

#### Bitwise Casting

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = cast %a : T`        | Bitcast to another type (same width)| `%x = cast %y : w`         |

---

### 4.5 Control Flow Operations

#### Terminator Instructions

| Instruction                  | Description                    | Example                    |
| ---------------------------- | ------------------------------ | -------------------------- |
| `jmp @label`                 | Unconditional jump             | `jmp @L1`                  |
| `jnz %cond, @T, @F`          | Conditional branch             | `jnz %flag, @yes, @no`     |
| `ret [%val]`                 | Return (optional value)        | `ret %x`                   |
| `hlt`                        | Halt execution                 | `hlt`                      |

#### SSA Phi Nodes

| Instruction                          | Description               | Example                          |
| ------------------------------------ | ------------------------- | -------------------------------- |
| `phi %r =T @L1 %v1, @L2 %v2, ...`    | Merge values in SSA       | `%x =w phi @L1 %a, @L2 %b`      |

Phi nodes are used to merge values from different control flow paths in SSA form:

```fyra
@entry
    jnz %cond, @then, @else
@then
    %val_then =w add %a, %b
    jmp @merge
@else
    %val_else =w sub %a, %b
    jmp @merge
@merge
    %result =w phi @then %val_then, @else %val_else
    ret %result
```

---

### 4.6 Function Call and Variadic Operations

#### Function Calls

| Instruction              | Description                          | Example                         |
| ------------------------ | ------------------------------------ | ------------------------------- |
| `call $f(...) : T`           | Call function (no return value)     | `call $print(%x : w) : w`             |
| `%r = call $f(...) : T`   | Call function with return value     | `%res = call $add(%a : w, %b : w) : w` |

#### Variadic Function Support

| Instruction          | Description                         | Example                         |
| -------------------- | ----------------------------------- | ------------------------------- |
| `vastart %ap`        | Start variadic argument list       | `vastart %ap`                   |
| `%r = vaarg %ap : T`    | Get next variadic argument         | `%x = vaarg %ap : w`               |

#### Other Operations

| Instruction          | Description                         | Example                         |
| -------------------- | ----------------------------------- | ------------------------------- |
| `%r = copy %a : T`      | Copy value                          | `%y = copy %x : w`                 |

### 4.7 Enhanced Features

#### Global Data Declaration

```fyra
data $global_var = { w 42 }                    # Single word
data $string_literal = { b "Hello", b 0 }      # Null-terminated string
data $array = { w 1, w 2, w 3, w 4 }           # Array of words
```

#### Function Export

```fyra
export function $main() : w {
    # Function is visible to linker
    ret 0 : w
}
```

---

## 5. Complete Examples

### 5.1 Simple Arithmetic Function

C:
```c
int add(int a, int b) {
    return a + b;
}
```

Fyra IL:
```fyra
function $add(%a : w, %b : w) : w {
@entry
    %sum = add %a, %b : w
    ret %sum : w
}
```

### 5.2 Conditional Logic with SSA

C:
```c
int max(int a, int b) {
    return a > b ? a : b;
}
```

Fyra IL:
```fyra
function $max(%a : w, %b : w) : w {
@entry
    %cmp = sgt %a, %b : w
    jnz %cmp, @return_a, @return_b
@return_a
    jmp @merge
@return_b
    jmp @merge
@merge
    %result = phi @return_a %a, @return_b %b : w
    ret %result : w
}
```

### 5.3 Memory Operations

C:
```c
void set_array_element(int* arr, int index, int value) {
    arr[index] = value;
}
```

Fyra IL:
```fyra
function $set_array_element(%arr : l, %index : w, %value : w) : void {
@entry
    %offset = shl %index, 2 : w      # multiply by 4 (sizeof(int))
    %addr = add %arr, %offset : l
    store %value, %addr : w
    ret
}
```

### 5.4 Floating-Point Operations

C:
```c
double distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}
```

Fyra IL:
```fyra
function $distance(%x1 : d, %y1 : d, %x2 : d, %y2 : d) : d {
@entry
    %dx = fsub %x2, %x1 : d
    %dy = fsub %y2, %y1 : d
    %dx_sq = fmul %dx, %dx : d
    %dy_sq = fmul %dy, %dy : d
    %sum = fadd %dx_sq, %dy_sq : d
    %result = call $sqrt(%sum : d) : d
    ret %result : d
}
```

### 5.5 Loop with SSA Phi Nodes

C:
```c
int factorial(int n) {
    int result = 1;
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}
```

Fyra IL:
```fyra
function $factorial(%n : w) : w {
@entry
    jmp @loop
@loop
    %i = phi @entry 1, @loop_body %i_next : w
    %result = phi @entry 1, @loop_body %result_next : w
    %cond = sle %i, %n : w
    jnz %cond, @loop_body, @exit
@loop_body
    %result_next = mul %result, %i : w
    %i_next = add %i, 1 : w
    jmp @loop
@exit
    ret %result : w
}
```

### 5.6 Variadic Function

C:
```c
int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    return sum;
}
```

Fyra IL:
```fyra
function $sum_ints(%count : w, ...) : w {
@entry
    vastart %args
    jmp @loop
@loop
    %i = phi @entry 0, @loop_body %i_next : w
    %sum = phi @entry 0, @loop_body %sum_next : w
    %cond = slt %i, %count : w
    jnz %cond, @loop_body, @exit
@loop_body
    %val = vaarg %args : w
    %sum_next = add %sum, %val : w
    %i_next = add %i, 1 : w
    jmp @loop
@exit
    ret %sum : w
}
```

### 5.7 Type Conversions and Extensions

C:
```c
long sign_extend_and_add(char a, short b) {
    return (long)a + (long)b;
}
```

Fyra IL:
```fyra
function $sign_extend_and_add(%a : w, %b : w) : l {
@entry
    %a_byte = extsb %a : w        # sign-extend byte to word
    %b_half = extsh %b : w        # sign-extend halfword to word
    %a_long = extsw %a_byte : l   # sign-extend word to long
    %b_long = extsw %b_half : l   # sign-extend word to long
    %result = add %a_long, %b_long : l
    ret %result : l
}
```

---

## 6. Best Practices and Guidelines

### 6.1 SSA Form Guidelines

*   **Use phi nodes for merging values** from different control flow paths
*   **Each variable should be assigned exactly once** in the source representation
*   **Prefer explicit control flow** over implicit conversions
*   **Use meaningful temporary names** to improve readability

### 6.2 Type Safety

*   **Match operand types** for arithmetic operations
*   **Use explicit conversions** when changing types
*   **Prefer sized types** (`w`, `l`) over generic types
*   **Use appropriate comparison operations** (signed vs unsigned)

### 6.3 Memory Management

*   **Use typed load/store operations** when possible
*   **Align stack allocations** appropriately (`alloc4`, `alloc8`, `alloc16`)
*   **Be explicit about memory sizes** in blit operations
*   **Use consistent pointer arithmetic** with proper scaling

### 6.4 Performance Considerations

*   **Minimize phi node usage** in performance-critical paths
*   **Use appropriate instruction variants** (e.g., `cslt` vs `cult`)
*   **Consider target-specific optimizations** in instruction selection
*   **Use efficient control flow patterns** to help optimization passes

### 6.5 Debugging and Maintenance

*   **Use descriptive temporary names** (`%sum` vs `%t1`)
*   **Add comments** for complex control flow
*   **Keep basic blocks focused** on single logical operations
*   **Use consistent formatting** for readability

---

## 7. Target-Specific Considerations

### 7.1 Linux System V x64
*   Supports all instructions
*   Uses System V calling convention
*   Efficient register allocation

### 7.2 Windows x64
*   Full instruction support
*   Uses Microsoft x64 calling convention
*   Stack alignment requirements

### 7.3 AArch64 (ARM64)
*   Most instructions supported
*   ARM-specific optimizations
*   Different register conventions

### 7.4 WebAssembly (Wasm32)
*   Subset of instructions
*   Stack-based execution model
*   Special handling for control flow

### 7.5 RISC-V 64-bit
*   RISC instruction mapping
*   Efficient for simple operations
*   Good optimization potential

---

## 8. Integration with Fyra Compiler

### 8.1 File Formats

*   **`.fyra` files**: Native Fyra format with enhanced features
*   **`.fy` files**: Alternative Fyra format extension

### 8.2 Command Line Usage

```bash
# Basic compilation
fyra_compiler program.fyra -o program.s

# Target-specific compilation
fyra_compiler program.fyra -o program.s --target windows

# Enhanced compilation with validation
fyra_compiler program.fyra -o program.s --enhanced --validate
```

### 8.3 Programmatic IR Building

See [`ir_builder.md`](ir_builder.md) for comprehensive guidance on building Fyra IR programmatically using the C++ API.

---

*For more information, see the complete [Fyra Compiler Documentation](../README.md).*


