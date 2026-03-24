# Fyra Optimization Roadmap (Unoptimized → O1 → Partial O2)

This document lays out a pragmatic optimization plan for Fyra's LIR backend with a strong bias toward:

- **high impact early wins**,
- **cross-target correctness**,
- **reasonable compile times**, and
- **debuggable, incremental delivery**.

It is intentionally practical rather than academically complete.

---

## 1) Guiding Strategy

1. **Build canonicalization + cleanup first** so later passes are effective.
2. **Prefer sparse / local analyses** (SCCP, copy propagation, local CSE, simple loop passes) before expensive global techniques.
3. **Run light fixed-point “micro-pipelines”** instead of a single giant global pass.
4. **Keep architecture-neutral optimizations in target-independent LIR**, and reserve target-specific peepholes for the very end.
5. **Add correctness checks after every mutating pass** (CFG, SSA/LIR invariants, use-def consistency).

---

## 2) Recommended Implementation Order (Highest ROI First)

The following order maximizes real-world impact per engineering time.

### Phase 0: Compiler Plumbing + Metrics (do this before optimization work)

- Add per-pass timing and IR size counters:
  - instruction count,
  - block count,
  - branch count,
  - load/store count,
  - spill/reload count (post-regalloc).
- Add pass verification hooks:
  - pre-pass verify (optional in debug),
  - post-pass verify (mandatory in debug).
- Add benchmark harness split into:
  - microbenchmarks (scalar/control-flow patterns),
  - medium real workloads (parser, regex, JSON, sqlite-like loops).

**Why first:** You cannot prioritize effectively without visibility. Many optimization efforts fail because compile-time and win attribution are unclear.

### Phase 1: Canonicalization + Folding + DCE Foundation

Implement a **canonical simplification pass** (cheap, deterministic):

- algebraic simplification (`x + 0`, `x * 1`, `x - x`, boolean identities),
- constant folding for arithmetic/compare/casts,
- branch simplification (`br true`, `br false`),
- CFG cleanup:
  - remove unreachable blocks,
  - merge trivial single-predecessor/successor blocks,
  - simplify redundant jumps.

Then implement **dead code elimination**:

- instruction-level DCE using side-effect model,
- dead store elimination (local/basic-block first),
- dead block elimination.

**Expected impact:** Big code-size reduction and immediate runtime wins by removing obvious waste; enables all later passes.

### Phase 2: SCCP + CFG Simplification Loop

Implement **Sparse Conditional Constant Propagation (SCCP)** over SSA-ish LIR value graph + CFG executability lattice.

- Lattice: `Unknown`, `Constant(c)`, `Overdefined`.
- Track executable edges/blocks.
- Fold branch conditions to constants when discoverable.
- Replace instruction results with constants where valid.

Then rerun:

1. simplify,
2. DCE,
3. CFG cleanup.

**Why now:** SCCP often unlocks dead branches and dead computations that plain local folding cannot see.

### Phase 3: Copy Propagation + Lightweight CSE

Implement **copy propagation** first:

- SSA value forwarding,
- trivial phi/copy collapse,
- move-chain collapse (`a=b; c=a` => `c=b`),
- memory-safe restrictions respected.

Implement **local value numbering (LVN) / local CSE** next:

- per basic block hash-consing of pure expressions,
- canonical operand ordering for commutative ops,
- dominance-safe reuse only.

Optionally add **very small global CSE** using dominator tree with strict compile-time budget.

Then rerun simplify + DCE.

**Expected impact:** Noticeable instruction count reduction and lower register pressure, especially after SCCP.

### Phase 4: Loop Optimization Starter Pack (O1.5 level)

Add low-risk loop passes:

1. **Loop-invariant code motion (LICM)**
   - only for side-effect-free ops,
   - alias-safe memory hoisting only when proven,
   - preheader creation.
2. **Strength reduction (basic)**
   - induction-based address arithmetic simplification.
3. **Induction variable simplification**
   - remove redundant IVs and derived recurrences.
4. **Loop unswitching (trivial constant/near-constant guards only)** optional and gated.

Avoid heavy loop transforms (vectorization/unrolling heuristics) early.

**Expected impact:** Large wins on tight numeric and traversal loops with moderate complexity.

### Phase 5: Inlining (Conservative, Budgeted)

Implement **small-function inlining** with strict heuristics:

- always-inline tiny leaf functions,
- budget by caller growth and module-level instruction growth,
- forbid/limit recursive SCCs initially,
- apply only when profitable signals are present (constant args, branch simplification opportunities).

After inlining, run a short cleanup pipeline:

- SCCP → simplify → copyprop/CSE → DCE.

**Why not earlier:** Inlining magnifies IR and can hide bugs in weak cleanup infrastructure. It pays off more once cleanup passes are mature.

### Phase 6: Register-Pressure-Aware Late Cleanup (pre/post regalloc)

Given linear-scan RA, add targeted passes:

- pre-RA copy coalescing hints,
- rematerialization of cheap constants,
- post-RA peepholes:
  - remove redundant moves,
  - fold move+op patterns,
  - branch/jump shortening,
  - target-specific instruction selection cleanups.

**Expected impact:** Significant practical runtime gains via fewer spills and cleaner machine code.

---

## 3) Biggest Early Wins: What to Implement First

If engineering bandwidth is limited, prioritize in this exact order:

1. **Simplify + DCE + CFG cleanup**
2. **SCCP**
3. **Copy propagation**
4. **Local CSE (LVN)**
5. **LICM + IV simplification (basic)**
6. **Conservative inlining**

### SCCP vs DCE vs Inlining

- **DCE**: easiest and immediately useful; should exist before SCCP.
- **SCCP**: typically bigger optimization unlock per complexity than early inlining.
- **Inlining**: high upside but can hurt compile time/code size early; do after cleanup foundation is reliable.

So: **DCE + SCCP before inlining** is the pragmatic route.

---

## 4) LIR Invariants Needed for Reliable Optimization

Maintain these invariants (and verify them constantly):

### Structural invariants

- CFG is well-formed (entry reachable, terminator in every block, consistent predecessor/successor lists).
- No critical edge assumptions violated (either split on demand or handle explicitly).
- Dominator tree invalidated/recomputed when CFG changes.

### SSA/value invariants (or SSA-like def-use discipline)

- Each virtual register/value has one defining instruction (or explicit phi/arg semantics).
- All uses dominate where required (phi exceptions handled correctly).
- Def-use and use-def chains are complete and consistent.
- Type and width consistency per instruction and operand.

### Side-effect and memory model invariants

- Every instruction classified as:
  - pure,
  - may-read-memory,
  - may-write-memory,
  - has control/exception side effects.
- Volatile/atomic/call barriers respected.
- Alias class metadata (even coarse) is available and conservative.

### Canonical form invariants

- Commutative ops normalized (`const` on RHS, deterministic operand ordering).
- Equivalent compare patterns normalized.
- Redundant copies minimized at pass boundaries.

These invariants are what make SCCP/DCE/CSE/LICM trustworthy and cross-target consistent.

---

## 5) Pipeline Design: Pass Structure, Interaction, Invalidation

Use a **two-tier pipeline**:

## A) Core Optimization Pipeline (target-independent)

Suggested O1 pipeline:

1. Canonical simplify/fold
2. CFG cleanup
3. DCE
4. SCCP
5. CFG cleanup
6. Copy propagation
7. Local CSE (LVN)
8. DCE (again)
9. Loop starter pack (LICM/IV simp)
10. Final simplify + DCE

Run as bounded fixed-point groups, e.g. max 2–3 iterations for:

- `{simplify, DCE, CFG cleanup}`
- `{SCCP, simplify, DCE}`

Avoid unbounded convergence loops.

## B) Backend/Late Pipeline (target-aware)

1. Instruction selection/legalization
2. Pre-RA cleanup/coalescing
3. Linear-scan RA
4. Post-RA peephole + branch cleanup

Keep target-specific transforms here, never mixed into core IR pipeline.

### Analysis invalidation rules (practical)

Each pass declares what it preserves:

- **If CFG changed**: invalidate dominators, loop info, reachability.
- **If instructions changed only locally**: preserve CFG analyses, invalidate value numbering/available expressions.
- **If memory-affecting ops inserted/removed/reordered**: invalidate alias-based analyses.

Implement a tiny pass manager with explicit preserved-analysis sets; do not overengineer full LLVM-style analysis manager initially.

---

## 6) Correctness and Cross-Target Consistency Plan

### Differential testing strategy

- Run optimized and unoptimized outputs and compare behavior.
- Cross-target differential tests:
  - same program on x86_64/AArch64/RISC-V,
  - compare observable outputs and checksums.
- Include UB-sensitive tests only when semantics are clearly defined for Fyra IL.

### Pass-level test strategy

For each pass:

- unit tests on handcrafted IR snippets,
- golden IR tests (before/after canonical text),
- fuzzed random CFG snippets with verifier checks.

### Regression gates

- No pass lands without:
  - correctness tests,
  - compile-time budget check,
  - benchmark delta report on representative suite.

---

## 7) Compile-Time Budget Controls

- Use pass-specific work limits (instructions visited, iterations, hash table growth).
- Gate expensive passes behind optimization level (`-O2` only).
- Add “bail out” for pathological functions (huge CFGs) to preserve responsiveness.
- Profile pass time continuously and report top offenders.

---

## 8) Proposed Optimization Levels for Fyra

### `-O0`

- Minimal canonicalization required for correctness/legalization only.
- Fast compile, best debug fidelity.

### `-O1` (first major milestone)

- Simplify/fold, CFG cleanup, DCE, SCCP, copyprop, local CSE, basic LICM/IV simp.
- Conservative inlining (tiny leaf functions only, optional initially).

### `-O2` (partial parity milestone)

- Stronger inlining heuristics,
- more loop work (still conservative),
- better pre/post-RA cleanup,
- limited global CSE/GVN if compile-time budget allows.

---

## 9) Concrete 10-Step Execution Plan

1. Add pass manager skeleton + analysis invalidation bookkeeping.
2. Add IR verifier and enable after every mutating pass in debug builds.
3. Implement simplify/fold + CFG cleanup.
4. Implement DCE and dead block elimination.
5. Implement SCCP and integrate cleanup loop.
6. Implement copy propagation + phi/copy collapse.
7. Implement local CSE (LVN) and rerun cleanup.
8. Implement basic LICM + IV simplification.
9. Implement conservative inlining + post-inline cleanup.
10. Add RA-focused pre/post passes and tune heuristics with benchmarks.

This sequence gets you to practical **O1 quality quickly**, then to **partial O2 parity** with manageable risk.

---

## 10) What to Avoid Early

- Full-blown global GVN/PRE before simpler wins are stable.
- Aggressive loop unrolling/vectorization before scalar pipeline is mature.
- Complex interprocedural analyses before inlining + cleanup are robust.
- Target-specific hacks in middle-end IR passes.

---

## 11) Success Criteria by Milestone

### O1 milestone

- Significant instruction-count reduction vs current baseline.
- Measurable speedups on branch-heavy and scalar benchmarks.
- Stable compile-time overhead (e.g., within acceptable project budget).
- No target-specific correctness regressions.

### Partial O2 milestone

- Additional loop/inlining gains on medium workloads.
- Reduced spill/reload counts post-RA.
- Improved code quality consistency across x86_64/AArch64/RISC-V.

---

## 12) Windows Backend Verification Track (Textual ASM + In-Memory PE + Wine)

To keep backend optimization work grounded in executable correctness, run this Windows-focused verification track for every roadmap milestone (and for any backend or CFG/DCE change):

### A. Textual assembly correctness gate

1. Build and run the Windows textual-assembly test:
   - `cmake --build build --target test_windows`
   - `./build/tests/test_windows`
2. Treat the run as passing only if it confirms expected structural hallmarks in emitted assembly (function labels, calls, compares/jumps, and `ret`/epilogue flow).
3. Keep this gate enabled in CI for default coverage.

### B. In-memory PE generation gate

1. Build and run the PE structural generator test:
   - `cmake --build build --target test_pe_generator`
   - `./build/tests/test_pe_generator`
2. Require PE structure checks to pass (`MZ`, `PE\\0\\0`, non-zero entry RVA, sane output size).
3. If this fails, block roadmap progression for optimizer changes that can affect codegen layout.

### C. Runtime execution gate under Wine (required when environment allows)

1. Generate a Windows executable from a representative Fyra input:
   - `./build/fyra_compiler tests/windows.fyra -o /tmp/windows_test.exe --target windows --enhanced --gen-exec --no-validate`
2. Execute it via Wine:
   - `wine /tmp/windows_test.exe`
3. Record and track:
   - process exit code,
   - stdout/stderr,
   - any loader/runtime errors.

### D. CI/environment policy

- If Wine is unavailable due environment policy/network restrictions, mark the runtime gate as **blocked** (not passed), and keep A/B structural gates as mandatory.
- Merge backend/optimizer work only when:
  - A + B pass, and
  - C passes in at least one Wine-enabled CI lane or maintainer validation run.
