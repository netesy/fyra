# QBE ↔ Fyra Reference Manual

This manual provides a comprehensive comparison between QBE IL and Fyra IL, covering syntax differences, instruction mappings, and migration guidelines. Fyra maintains substantial compatibility with QBE while extending capabilities and modernizing the instruction set.

## Overview of Differences

### Key Similarities
- **SSA-based design** with explicit typing
- **Three-address instruction format** for most operations
- **Similar type system** with integers, floats, and pointers
- **Compatible control flow** constructs
- **Function and basic block organization**

### Key Differences
- **Enhanced instruction set** with floating-point operations
- **Extended type conversions** and casting operations
- **Improved memory operations** with more load/store variants
- **Enhanced function syntax** with explicit return types
- **Multiple file format support** (.qbe, .fyra, .fy)
- **Target-aware compilation** with multiple backend support

---

## 1. Function Declaration Syntax

### Function Headers

**QBE:**
```qbe
function [export] [w|l|s|d] $name(type %param1, type %param2, ...) {
    # function body
}
```

**Fyra:**
```fyra
[export] function $name(type %param1, type %param2, ...) -> ReturnType {
    # function body
}
```

**Key Differences:**
- Fyra uses `-> ReturnType` syntax for explicit return type declaration
- Fyra places `export` before `function` keyword
- Both support variadic functions with `...`

### Examples

**QBE:**
```qbe
export function w $main() {
@start
    ret 0
}
```

**Fyra:**
```fyra
export function $main() : w {
@start
    ret 0 : w
}
```

---

---

## 2. Arithmetic Instructions

### Integer Arithmetic

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Addition | `%d =w add %a, %b` | `%d = add %a, %b : w` | Fyra uses colon type syntax |
| Subtraction | `%d =w sub %a, %b` | `%d = sub %a, %b : w` | Fyra uses colon type syntax |
| Multiplication | `%d =w mul %a, %b` | `%d = mul %a, %b : w` | Fyra uses colon type syntax |
| Division (Signed) | `%d =w div %a, %b` | `%d = div %a, %b : w` | Fyra uses colon type syntax |
| Division (Unsigned) | `%d =w udiv %a, %b` | `%d = udiv %a, %b : w` | Fyra uses colon type syntax |
| Remainder (Signed) | `%d =w rem %a, %b` | `%d = rem %a, %b : w` | Fyra uses colon type syntax |
| Remainder (Unsigned) | `%d =w urem %a, %b` | `%d = urem %a, %b : w` | Fyra uses colon type syntax |
| Negation | `%d =w sub 0, %a` | `%d = neg %a : w` | Fyra has explicit neg with colon syntax |

### Floating-Point Arithmetic

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Addition | `%d =s add %a, %b` | `%d = fadd %a, %b : s` | Fyra uses fadd with colon syntax |
| Subtraction | `%d =s sub %a, %b` | `%d = fsub %a, %b : s` | Fyra uses fsub with colon syntax |
| Multiplication | `%d =s mul %a, %b` | `%d = fmul %a, %b : s` | Fyra uses fmul with colon syntax |
| Division | `%d =s div %a, %b` | `%d = fdiv %a, %b : s` | Fyra uses fdiv with colon syntax |

**Migration Note:** In Fyra, floating-point operations use explicit `f` prefixes to distinguish from integer operations.

---

---

## 3. Bitwise and Logical Instructions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| AND | `%d =w and %a, %b` | `%d = and %a, %b : w` | Fyra uses colon type syntax |
| OR | `%d =w or %a, %b` | `%d = or %a, %b : w` | Fyra uses colon type syntax |
| XOR | `%d =w xor %a, %b` | `%d = xor %a, %b : w` | Fyra uses colon type syntax |
| NOT | `%d =w xor %a, -1` | `%d = xor %a, -1 : w` | No explicit NOT in either, colon syntax |
| Shift Left | `%d =w shl %a, %b` | `%d = shl %a, %b : w` | Fyra uses colon type syntax |
| Shift Right (Logical) | `%d =w shr %a, %b` | `%d = shr %a, %b : w` | Fyra uses colon type syntax |
| Shift Right (Arithmetic) | `%d =w sar %a, %b` | `%d = sar %a, %b : w` | Fyra uses colon type syntax |

**Compatibility:** Bitwise operations are fully compatible between QBE and Fyra.

---

---

## 4. Comparison Instructions

### Integer Comparisons

| Comparison | QBE Syntax | Fyra Syntax | Notes |
|------------|------------|-------------|-------|
| Equal | `%d =w ceq %a, %b` | `%d = eq %a, %b : w` | Fyra simplified to eq with colon syntax |
| Not Equal | `%d =w cne %a, %b` | `%d = ne %a, %b : w` | Fyra simplified to ne with colon syntax |
| Signed Less | `%d =w cslt %a, %b` | `%d = slt %a, %b : w` | Fyra simplified to slt with colon syntax |
| Signed Less/Equal | `%d =w csle %a, %b` | `%d = sle %a, %b : w` | Fyra simplified to sle with colon syntax |
| Signed Greater | `%d =w csgt %a, %b` | `%d = sgt %a, %b : w` | Fyra simplified to sgt with colon syntax |
| Signed Greater/Equal | `%d =w csge %a, %b` | `%d = sge %a, %b : w` | Fyra simplified to sge with colon syntax |
| Unsigned Less | `%d =w cult %a, %b` | `%d = ult %a, %b : w` | Fyra simplified to ult with colon syntax |
| Unsigned Less/Equal | `%d =w cule %a, %b` | `%d = ule %a, %b : w` | Fyra simplified to ule with colon syntax |
| Unsigned Greater | `%d =w cugt %a, %b` | `%d = ugt %a, %b : w` | Fyra simplified to ugt with colon syntax |
| Unsigned Greater/Equal | `%d =w cuge %a, %b` | `%d = uge %a, %b : w` | Fyra simplified to uge with colon syntax |

### Floating-Point Comparisons

| Comparison | QBE Syntax | Fyra Syntax | Notes |
|------------|------------|-------------|-------|
| Equal | `%d =w ceq %a, %b` | `%d = eq %a, %b : w` | Fyra uses generic eq with colon syntax |
| Not Equal | `%d =w cne %a, %b` | `%d = ne %a, %b : w` | Fyra uses generic ne with colon syntax |
| Less Than | `%d =w clt %a, %b` | `%d = lt %a, %b : w` | Fyra context-dependent with colon syntax |
| Less/Equal | `%d =w cle %a, %b` | `%d = le %a, %b : w` | Fyra context-dependent with colon syntax |
| Greater Than | `%d =w cgt %a, %b` | `%d = gt %a, %b : w` | Fyra context-dependent with colon syntax |
| Greater/Equal | `%d =w cge %a, %b` | `%d = ge %a, %b : w` | Fyra context-dependent with colon syntax |
| Ordered | `%d =w cod %a, %b` | `%d = co %a, %b : w` | Fyra drops 'd' suffix, uses colon syntax |
| Unordered | `%d =w cuod %a, %b` | `%d = cuo %a, %b : w` | Fyra drops 'd' suffix, uses colon syntax |

### Type-Specific Shortcuts

Both QBE and Fyra support type-specific comparison shortcuts:

**Examples:**
```fyra
# Word comparisons
%cmp = ceqw %a, %b : w    # 32-bit equality
%cmp = csltw %a, %b : w   # 32-bit signed less-than

# Long comparisons  
%cmp = ceql %a, %b : w    # 64-bit equality
%cmp = cultl %a, %b : w   # 64-bit unsigned less-than

# Float comparisons
%cmp = ceqs %a, %b : w    # Single precision equality
%cmp = ceqd %a, %b : w    # Double precision equality
```

---

---

## 5. Memory Instructions

### Stack Allocation

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Alloc 4-byte | `%p =l alloc4 %size` | `%p = alloc4 %size : l` | Fyra uses colon type syntax |
| Alloc 8-byte | `%p =l alloc8 %size` | `%p = alloc %size : l` | Fyra alloc defaults to 8-byte, colon syntax |
| Alloc 16-byte | `%p =l alloc16 %size` | `%p = alloc16 %size : l` | Fyra uses colon type syntax |

### Load Operations

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Load Word | `%d =w loadw %addr` | `%d = load %addr : w` | Fyra uses colon type syntax |
| Load Long | `%d =l loadl %addr` | `%d = load %addr : l` | Fyra uses colon type syntax |
| Load Single | `%d =s loads %addr` | `%d = load %addr : s` | Fyra uses colon type syntax |
| Load Double | `%d =d loadd %addr` | `%d = load %addr : d` | Fyra uses colon type syntax |
| Load Unsigned Byte | `%d =w loadub %addr` | `%d = loadub %addr : w` | Fyra uses colon type syntax |
| Load Signed Byte | `%d =w loadsb %addr` | `%d = loadsb %addr : w` | Fyra uses colon type syntax |
| Load Unsigned Half | `%d =w loaduh %addr` | `%d = loaduh %addr : w` | Fyra uses colon type syntax |
| Load Signed Half | `%d =w loadsh %addr` | `%d = loadsh %addr : w` | Fyra uses colon type syntax |
| Load Unsigned Word | `%d =l loaduw %addr` | `%d = loaduw %addr : l` | Fyra uses colon type syntax |

### Store Operations

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Store Word | `storew %val, %addr` | `store %val, %addr : w` | Fyra uses generic store with colon syntax |
| Store Long | `storel %val, %addr` | `store %val, %addr : l` | Fyra uses generic store with colon syntax |
| Store Single | `stores %val, %addr` | `store %val, %addr : s` | Fyra uses generic store with colon syntax |
| Store Double | `stored %val, %addr` | `store %val, %addr : d` | Fyra uses generic store with colon syntax |
| Store Half | `storeh %val, %addr` | `store %val, %addr : h` | Fyra uses generic store with colon syntax |
| Store Byte | `storeb %val, %addr` | `store %val, %addr : b` | Fyra uses generic store with colon syntax |

**Important:** Fyra reverses the operand order for store instructions compared to QBE.

### Memory Copy

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Block Copy | `blit %dst, %src, N` | `blit %dst, %src, %count` | Fyra allows register count |

---

---

## 6. Control Flow Instructions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Unconditional Jump | `jmp @label` | `jmp @label` | Identical |
| Conditional Branch | `jnz %cond, @true, @false` | `jnz %cond, @true, @false` | Identical |
| Return Value | `ret %val` | `ret %val` | Identical |
| Return Void | `ret` | `ret` | Identical |
| Halt | Not standard | `hlt` | Fyra addition |

### Function Calls

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Call with Return | `%d =w call $func(%args)` | `call [%d =w] $func(%args)` | Fyra uses bracket syntax |
| Call Void | `call $func(%args)` | `call $func(%args)` | Identical |

**Examples:**

**QBE:**
```qbe
%result =w call $add(w %a, w %b)
call $print(l $str)
```

**Fyra:**
```fyra
call [%result =w] $add(w %a, w %b)
call $print(l $str)
```

---

---

## 7. Type Conversion Instructions

### Integer Extensions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Zero-extend Byte | `%d =w extub %src` | `%d = extub %src : w` | Fyra uses colon type syntax |
| Zero-extend Half | `%d =w extuh %src` | `%d = extuh %src : w` | Fyra uses colon type syntax |
| Zero-extend Word | `%d =l extuw %src` | `%d = extuw %src : l` | Fyra uses colon type syntax |
| Sign-extend Byte | `%d =w extsb %src` | `%d = extsb %src : w` | Fyra uses colon type syntax |
| Sign-extend Half | `%d =w extsh %src` | `%d = extsh %src : w` | Fyra uses colon type syntax |
| Sign-extend Word | `%d =l extsw %src` | `%d = extsw %src : l` | Fyra uses colon type syntax |

### Floating-Point Conversions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Single to Double | `%d =d exts %src` | `%d = exts %src : d` | Fyra uses colon type syntax |
| Double to Single | `%d =s truncd %src` | `%d = truncd %src : s` | Fyra uses colon type syntax |

### Integer/Float Conversions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Signed Word to Float | `%d =s swtof %src` | `%d = swtof %src : s` | Fyra uses colon type syntax |
| Unsigned Word to Float | `%d =s uwtof %src` | `%d = uwtof %src : s` | Fyra uses colon type syntax |
| Signed Long to Float | `%d =s sltof %src` | `%d = sltof %src : s` | Fyra uses colon type syntax |
| Unsigned Long to Float | `%d =s ultof %src` | `%d = ultof %src : s` | Fyra uses colon type syntax |
| Double to Signed Int | `%d =w dtosi %src` | `%d = dtosi %src : w` | Fyra uses colon type syntax |
| Double to Unsigned Int | `%d =w dtoui %src` | `%d = dtoui %src : w` | Fyra uses colon type syntax |
| Single to Signed Int | `%d =w stosi %src` | `%d = stosi %src : w` | Fyra uses colon type syntax |
| Single to Unsigned Int | `%d =w stoui %src` | `%d = stoui %src : w` | Fyra uses colon type syntax |

### Bitwise Casting

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Bitcast | `%d =T cast %src` | `%d = cast %src : T` | Fyra uses colon type syntax |

**Notes:** The primary difference is that Fyra uses colon type syntax (`:type`) while QBE uses equals-type syntax (`=type`).

---

## 8. SSA and Other Instructions

### Phi Nodes

**QBE:**
```qbe
%d =w phi @lbl1 %a, @lbl2 %b
```

**Fyra:**
```fyra
%d = phi @lbl1 %a, @lbl2 %b : w
```

**Notes:** Fyra uses colon type syntax for phi nodes.

### Copy Instructions

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| Copy Value | `%d =w copy %src` | `%d = copy %src : w` | Fyra uses colon type syntax |

### Variadic Function Support

| Operation | QBE Syntax | Fyra Syntax | Notes |
|-----------|------------|-------------|-------|
| VA Start | `vastart %ap` | `vastart %ap` | Identical |
| VA Arg | `%d =w vaarg %ap` | `%d = vaarg %ap : w` | Fyra uses colon type syntax |

---

## 9. Migration Guide

### Automatic Migration

Most QBE code can be automatically converted to Fyra with minimal changes:

1. **Function headers** - Convert `function w $name` to `function $name -> w`
2. **Store instructions** - Reverse operand order from `store val, addr` to `store addr, val`
3. **Floating-point ops** - Convert `add`/`sub`/`mul`/`div` on floats to `fadd`/`fsub`/`fmul`/`fdiv`
4. **Export placement** - Move `export` before `function` keyword

### Manual Migration Steps

#### Step 1: Update Function Declarations

**Before (QBE):**
```qbe
export function w $main() {
    # body
}
```

**After (Fyra):**
```fyra
export function $main() : w {
    # body
}
```

#### Step 2: Fix Store Instructions

**Before (QBE):**
```qbe
storew %value, %pointer
```

**After (Fyra):**
```fyra
store %value, %pointer : w
```

#### Step 3: Update Floating-Point Operations

**Before (QBE):**
```qbe
%result =s add %a, %b
```

**After (Fyra):**
```fyra
%result = fadd %a, %b : s
```

#### Step 4: Verify Comparisons

Most comparisons are compatible, but verify floating-point comparisons:

**QBE:**
```qbe
%cmp =w ceq %a, %b    # Works for both int and float
```

**Fyra (recommended):**
```fyra
%cmp = eq %a, %b : w     # For integers
%cmp = eq %a, %b : w     # For floats (generic eq works for both)
```

### Compatibility Testing

1. **Compile with Fyra** using `--target linux` for QBE compatibility mode
2. **Run test suite** to verify semantic equivalence
3. **Compare outputs** between QBE and Fyra compilation
4. **Validate performance** for performance-critical code

### New Features in Fyra

After migration, consider leveraging Fyra's enhanced features:

1. **Enhanced CodeGen** with `--enhanced` flag
2. **Multi-target compilation** with `--target` options
3. **ASM validation** with `--validate` flag
4. **Object file generation** with `--object` flag
5. **Comprehensive optimization pipeline** with `--pipeline` flag

---

## 10. Summary of Key Differences

| Aspect | QBE | Fyra | Impact |
|--------|-----|------|--------|
| Function Syntax | `function w $name` | `function $name() : w` | Migration required |
| Store Order | `store val, addr` | `store val, addr : type` | Syntax update required |
| Float Arithmetic | Context-dependent | Explicit `fadd`, etc. with `:type` | Recommended update |
| Float Comparisons | Context-dependent | Generic `eq`, `lt`, etc. with `:type` | Recommended update |
| Export Position | After `function` | Before `function` | Migration required |
| Type Syntax | `%var =type instruction` | `%var = instruction : type` | **Breaking change** |
| Halt Instruction | None | `hlt` | Fyra addition |
| Call Syntax | `%d =w call $f()` | `%d = call $f() : w` | Syntax update required |
| Target Support | Single target | Multi-target | Fyra enhancement |
| File Formats | `.qbe` only | `.qbe`, `.fyra`, `.fy` | Fyra enhancement |

### Compatibility Level

- **High Compatibility:** Arithmetic, bitwise, comparisons, control flow
- **Medium Compatibility:** Function declarations, floating-point operations
- **Low Compatibility:** Store instructions (breaking change)

**Recommendation:** Use Fyra's QBE compatibility mode during migration, then gradually adopt Fyra-specific enhancements.
