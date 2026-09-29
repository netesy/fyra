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
export function $main() : i32 {
    # exported functions are visible to linker
}
```

---

## 2. Types

| Type    | Meaning                 |
| ------- | ----------------------- |
| `i8`    | 8-bit signed integer    |
| `i16`   | 16-bit signed integer   |
| `i32`   | 32-bit signed integer   |
| `i64`   | 64-bit signed integer   |
| `u8`    | 8-bit unsigned integer  |
| `u16`   | 16-bit unsigned integer |
| `u32`   | 32-bit unsigned integer |
| `u64`   | 64-bit unsigned integer |
| `f32`   | 32-bit floating point   |
| `f64`   | 64-bit floating point   |
| `bool`  | Boolean                 |
| `v16i8` | 16 × i8                 |
| `v8i16` | 8 × i16                 |
| `v4i32` | 4 × i32                 |
| `v2i64` | 2 × i64                 |
| `v4f32` | 4 × f32                 |
| `v2f64` | 2 × f64                 |

Vector types mechanically compose with scalar types using the syntax `v<lanes><scalar_type>`.

---

## 3. Constants

* Integers: `0`, `-5`, `123`
* Floating-point: `f32_1.0`, `f64_2.71828`, `f64_1e-5`
* Global symbol addresses: `$foo`

---

## 4. Instructions Reference

### 4.1 Arithmetic Operations

#### Integer Arithmetic

| Instruction         | Description                | Example               |
| ------------------- | -------------------------- | --------------------- |
| `%r = add %a, %b : T`  | Addition                   | `%sum = add %x, %y : i32`  |
| `%r = sub %a, %b : T`  | Subtraction                | `%diff = sub %a, %b : i64` |
| `%r = mul %a, %b : T`  | Multiplication             | `%prod = mul %x, %y : i32` |
| `%r = div %a, %b : T`  | Signed division            | `%q = div %a, %b : i32`    |
| `%r = udiv %a, %b : T` | Unsigned integer division  | `%uq = udiv %a, %b : i32`  |
| `%r = rem %a, %b : T`  | Signed remainder           | `%r = rem %a, %b : i32`    |
| `%r = urem %a, %b : T` | Unsigned remainder         | `%r = urem %a, %b : i32`   |
| `%r = neg %a : T`      | Negation                   | `%inv = neg %x : i32`      |

#### Floating-Point Arithmetic

| Instruction          | Description                | Example               |
| -------------------- | -------------------------- | --------------------- |
| `%r = fadd %a, %b : T`  | Floating-point addition    | `%sum = fadd %x, %y : f32` |
| `%r = fsub %a, %b : T`  | Floating-point subtraction | `%diff = fsub %a, %b : f64`|
| `%r = fmul %a, %b : T`  | Floating-point multiplication | `%prod = fmul %x, %y : f32` |
| `%r = fdiv %a, %b : T`  | Floating-point division    | `%q = fdiv %a, %b : f64`   |

#### Bitwise Operations

| Instruction         | Description                | Example               |
| ------------------- | -------------------------- | --------------------- |
| `%r = and %a, %b : T`  | Bitwise AND                | `%mask = and %a, %b : i32` |
| `%r = or %a, %b : T`   | Bitwise OR                 | `%flag = or %x, %y : i64`  |
| `%r = xor %a, %b : T`  | Bitwise XOR                | `%x2 = xor %a, %b : i32`   |
| `%r = shl %a, %b : T`  | Shift left                 | `%dbl = shl %a, 1 : i32`   |
| `%r = shr %a, %b : T`  | Logical shift right        | `%shr = shr %a, 2 : i64`   |
| `%r = sar %a, %b : T`  | Arithmetic shift right     | `%shr = sar %a, 1 : i32`   |

---

### 4.2 Memory Operations

#### Stack Allocation

| Instruction              | Description                                      | Example              |
| ------------------------ | ------------------------------------------------ | -------------------- |
| `%r = alloc %size : i64`      | Allocate stack memory (8-byte aligned)          | `%buf = alloc 64 : i64`   |
| `%r = alloc4 %size : i64`     | Allocate stack memory (4-byte aligned)          | `%buf = alloc4 16 : i64`  |
| `%r = alloc16 %size : i64`    | Allocate stack memory (16-byte aligned)         | `%buf = alloc16 64 : i64` |

#### Load Operations

| Instruction               | Description                              | Example               |
| ------------------------- | ---------------------------------------- | --------------------- |
| `%r = load %ptr : T`        | Load value from memory                   | `%x = load %p : i32`       |
| `%r = loadw %ptr : i32`       | Load 32-bit word                         | `%x = loadw %p : i32`      |
| `%r = loadl %ptr : i64`       | Load 64-bit long                         | `%x = loadl %p : i64`      |
| `%r = loads %ptr : f32`       | Load 32-bit float                        | `%x = loads %p : f32`      |
| `%r = loadd %ptr : f64`       | Load 64-bit double                       | `%x = loadd %p : f64`      |
| `%r = loadub %ptr : i32`      | Load unsigned byte (zero-extend to w)    | `%b = loadub %p : i32`     |
| `%r = loadsb %ptr : i32`      | Load signed byte (sign-extend to w)      | `%b = loadsb %p : i32`     |
| `%r = loaduh %ptr : i32`      | Load unsigned halfword (zero-extend)     | `%h = loaduh %p : i32`     |
| `%r = loadsh %ptr : i32`      | Load signed halfword (sign-extend)       | `%h = loadsh %p : i32`     |
| `%r = loaduw %ptr : i64`      | Load unsigned word (zero-extend to l)    | `%w = loaduw %p : i64`     |

#### Store Operations

| Instruction               | Description                              | Example               |
| ------------------------- | ---------------------------------------- | --------------------- |
| `store %val, %ptr : T`        | Store value to memory                    | `store %x, %p : i32`        |
| `storew %val, %ptr : i32`       | Store 32-bit word                        | `storew %x, %p : i32`       |
| `storel %val, %ptr : i64`       | Store 64-bit long                        | `storel %x, %p : i64`       |
| `stores %val, %ptr : f32`       | Store 32-bit float                       | `stores %x, %p : f32`       |
| `stored %val, %ptr : f64`       | Store 64-bit double                      | `stored %x, %p : f64`       |
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
| `%r = eq %a, %b : i32`    | Equal                       | `%cmp = eq %a, %b : i32`    |
| `%r = ne %a, %b : i32`    | Not equal                   | `%cmp = ne %a, %b : i32`    |
| `%r = slt %a, %b : i32`   | Signed less than            | `%cmp = slt %a, %b : i32`   |
| `%r = sle %a, %b : i32`   | Signed less than or equal   | `%cmp = sle %a, %b : i32`   |
| `%r = sgt %a, %b : i32`   | Signed greater than         | `%cmp = sgt %a, %b : i32`   |
| `%r = sge %a, %b : i32`   | Signed greater than or equal| `%cmp = sge %a, %b : i32`   |

**Unsigned Integer Comparisons:**

| Instruction           | Description                   | Example                 |
| --------------------- | ----------------------------- | ----------------------- |
| `%r = ult %a, %b : i32`   | Unsigned less than            | `%cmp = ult %a, %b : i32`   |
| `%r = ule %a, %b : i32`   | Unsigned less than or equal   | `%cmp = ule %a, %b : i32`   |
| `%r = ugt %a, %b : i32`   | Unsigned greater than         | `%cmp = ugt %a, %b : i32`   |
| `%r = uge %a, %b : i32`   | Unsigned greater than or equal| `%cmp = uge %a, %b : i32`   |

#### Floating-Point Comparisons

| Instruction           | Description                 | Example                 |
| --------------------- | --------------------------- | ----------------------- |
| `%r = eq %a, %b : i32`   | Float equal                 | `%cmp = eq %a, %b : i32`   |
| `%r = ne %a, %b : i32`   | Float not equal             | `%cmp = ne %a, %b : i32`   |
| `%r = lt %a, %b : i32`    | Float less than             | `%cmp = lt %a, %b : i32`    |
| `%r = le %a, %b : i32`    | Float less than or equal    | `%cmp = le %a, %b : i32`    |
| `%r = gt %a, %b : i32`    | Float greater than          | `%cmp = gt %a, %b : i32`    |
| `%r = ge %a, %b : i32`    | Float greater than or equal | `%cmp = ge %a, %b : i32`    |
| `%r = co %a, %b : i32`     | Ordered (neither operand is NaN) | `%cmp = co %a, %b : i32` |
| `%r = cuo %a, %b : i32`    | Unordered (at least one operand is NaN) | `%cmp = cuo %a, %b : i32` |

#### Type-Specific Comparison Shortcuts

Fyra also supports type-specific comparison shortcuts:

```fyra
# Word (32-bit) comparisons
%cmp1 = ceqw %a, %b : i32       # int equality
%cmp2 = csltw %a, %b : i32      # signed less than
%cmp3 = cultw %a, %b : i32      # unsigned greater

# Long (64-bit) comparisons  
%cmp4 = ceql %a, %b : i32       # long equality
%cmp5 = csltl %a, %b : i32      # signed less than (64-bit)

# Float comparisons
%cmp6 = ceqs %a, %b : i32       # single precision equality
%cmp7 = ceqd %a, %b : i32       # double precision equality
```

---

### 4.4 Type Conversion Operations

#### Integer Extensions

| Instruction            | Description                    | Example                  |
| ---------------------- | ------------------------------ | ------------------------ |
| `%r = extub %a : i32`       | Zero-extend byte to word       | `%r = extub %x : i32`         |
| `%r = extuh %a : i32`       | Zero-extend halfword to word   | `%r = extuh %x : i32`         |
| `%r = extuw %a : i64`       | Zero-extend word to long       | `%r = extuw %x : i64`         |
| `%r = extsb %a : i32`       | Sign-extend byte to word       | `%r = extsb %x : i32`         |
| `%r = extsh %a : i32`       | Sign-extend halfword to word   | `%r = extsh %x : i32`         |
| `%r = extsw %a : i64`       | Sign-extend word to long       | `%r = extsw %x : i64`         |

#### Floating-Point Conversions

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = exts %a : f64`        | Float extend single→double         | `%d = exts %sval : f64`       |
| `%r = truncd %a : f32`      | Float truncate double→single       | `%s = truncd %dval : f32`     |

#### Integer/Float Conversions

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = swtof %a : f32`       | Signed word to float                | `%f = swtof %ival : f32`      |
| `%r = uwtof %a : f32`       | Unsigned word to float              | `%f = uwtof %ival : f32`      |
| `%r = sltof %a : f32`       | Signed long to float                | `%f = sltof %lval : f32`      |
| `%r = ultof %a : f32`       | Unsigned long to float              | `%f = ultof %lval : f32`      |
| `%r = dtosi %a : i32`       | Double to signed int                | `%i = dtosi %dval : i32`      |
| `%r = dtoui %a : i32`       | Double to unsigned int              | `%i = dtoui %dval : i32`      |
| `%r = stosi %a : i32`       | Single to signed int                | `%i = stosi %sval : i32`      |
| `%r = stoui %a : i32`       | Single to unsigned int              | `%i = stoui %sval : i32`      |

#### Bitwise Casting

| Instruction            | Description                         | Example                  |
| ---------------------- | ----------------------------------- | ------------------------ |
| `%r = cast %a : T`        | Bitcast to another type (same width)| `%x = cast %y : i32`         |

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
| `call $f(...) : T`           | Call function (no return value)     | `call $print(%x : i32) : i32`             |
| `%r = call $f(...) : T`   | Call function with return value     | `%res = call $add(%a : i32, %b : i32) : i32` |

#### Variadic Function Support

| Instruction          | Description                         | Example                         |
| -------------------- | ----------------------------------- | ------------------------------- |
| `vastart %ap`        | Start variadic argument list       | `vastart %ap`                   |
| `%r = vaarg %ap : T`    | Get next variadic argument         | `%x = vaarg %ap : i32`               |

#### Other Operations

| Instruction          | Description                         | Example                         |
| -------------------- | ----------------------------------- | ------------------------------- |
| `%r = copy %a : T`      | Copy value                          | `%y = copy %x : i32`                 |

### 4.7 Enhanced Features

#### Global Data Declaration

```fyra
data $global_var = { i32 42 }                    # Single word
data $string_literal = { i8 "Hello", i8 0 }      # Null-terminated string
data $array = { i32 1, i32 2, i32 3, i32 4 }           # Array of words
```

#### Function Export

```fyra
export function $main() : i32 {
    # Function is visible to linker
    ret 0 : i32
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
function $add(%a : i32, %b : i32) : i32 {
@entry
    %sum = add %a, %b : i32
    ret %sum : i32
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
function $max(%a : i32, %b : i32) : i32 {
@entry
    %cmp = sgt %a, %b : i32
    jnz %cmp, @return_a, @return_b
@return_a
    jmp @merge
@return_b
    jmp @merge
@merge
    %result = phi @return_a %a, @return_b %b : i32
    ret %result : i32
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
function $set_array_element(%arr : i64, %index : i32, %value : i32) : void {
@entry
    %offset = shl %index, 2 : i32      # multiply by 4 (sizeof(int))
    %addr = add %arr, %offset : i64
    store %value, %addr : i32
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
function $distance(%x1 : f64, %y1 : f64, %x2 : f64, %y2 : f64) : f64 {
@entry
    %dx = fsub %x2, %x1 : f64
    %dy = fsub %y2, %y1 : f64
    %dx_sq = fmul %dx, %dx : f64
    %dy_sq = fmul %dy, %dy : f64
    %sum = fadd %dx_sq, %dy_sq : f64
    %result = call $sqrt(%sum : f64) : f64
    ret %result : f64
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
function $factorial(%n : i32) : i32 {
@entry
    jmp @loop
@loop
    %i = phi @entry 1, @loop_body %i_next : i32
    %result = phi @entry 1, @loop_body %result_next : i32
    %cond = sle %i, %n : i32
    jnz %cond, @loop_body, @exit
@loop_body
    %result_next = mul %result, %i : i32
    %i_next = add %i, 1 : i32
    jmp @loop
@exit
    ret %result : i32
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
function $sum_ints(%count : i32, ...) : i32 {
@entry
    vastart %args
    jmp @loop
@loop
    %i = phi @entry 0, @loop_body %i_next : i32
    %sum = phi @entry 0, @loop_body %sum_next : i32
    %cond = slt %i, %count : i32
    jnz %cond, @loop_body, @exit
@loop_body
    %val = vaarg %args : i32
    %sum_next = add %sum, %val : i32
    %i_next = add %i, 1 : i32
    jmp @loop
@exit
    ret %sum : i32
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
function $sign_extend_and_add(%a : i32, %b : i32) : i64 {
@entry
    %a_byte = extsb %a : i32        # sign-extend byte to word
    %b_half = extsh %b : i32        # sign-extend halfword to word
    %a_long = extsw %a_byte : i64   # sign-extend word to long
    %b_long = extsw %b_half : i64   # sign-extend word to long
    %result = add %a_long, %b_long : i64
    ret %result : i64
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


