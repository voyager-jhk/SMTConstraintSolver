# SMT Interval Solver

[![Language](https://img.shields.io/badge/Language-C-blue.svg)](<https://en.wikipedia.org/wiki/C_(programming_language)>)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A Linear Integer Arithmetic (LIA) constraint solver implemented in C. It parses logical propositions and uses interval arithmetic with constraint propagation to determine consistency.

## Overview

This project implements a lightweight SMT solver for the **Discrete Mathematics** course. It validates systems of inequalities and equations (e.g., `a + b < c + 1`) by iteratively narrowing variable domains until a fixed point is reached or a contradiction (empty interval) is found.

**Key Features:**

- **Parsing:** Uses **Flex & Bison** to parse arithmetic expressions (`+`, `-`, `*`, `/`, `<<`, `>>`) and relational propositions.
- **Interval Propagation:** Implements forward, backward, and relational propagation logic.
- **Robustness:** Handles 64-bit integer overflows using saturation arithmetic.
- **Memory Safety:** Full AST memory management verified with AddressSanitizer (ASan).

## Mechanism

The solver operates on two main principles:

1.  **Interval Arithmetic:** Instead of concrete values, variables are represented as ranges `[lower, upper]`. Operations are performed on these bounds (e.g., `[1, 5] + [2, 3] = [3, 8]`).
2.  **Fixed-Point Iteration:**
    - **Forward:** Updates parent nodes based on children (e.g., `a = m * n`).
    - **Relational:** Restricts bounds based on comparison operators (e.g., `x < y`).
    - **Backward:** Refines child nodes based on parent constraints.
    - The cycle repeats until intervals stabilize. If `lower > upper` for any variable, the system is UNSAT (inconsistent).

## Build & Run

**Requirements:** `gcc`, `flex`, `bison`

**Compile:**

```bash
gcc -O3 -g -Wno-return-type -Wno-parentheses -fsanitize=address -ftrapv smt_lang.c smt_lang_flex.c smt_lang.tab.c main.c -o test.out
```

**Run:**

```bash
./test.out <input_file>
```

## Example

**Input (`test2.txt`):**

```
(a + b) < (c + 1)
b > 1
a = m * n
n > 1
m > 1
c < 1
```

**Output:**

```
STARTING INTERVAL SOLVER...

INTERVAL SOLVER FINISHED.
Result: An empty interval was found (inconsistency detected).

--- Final Variable Intervals ---
Var 'a': [1, 0]   <-- Empty Interval (Contradiction)
Var 'b': [2, 9223372036854775807]
...
```

**Analysis:** The solver correctly identifies the contradiction:

1. `c < 1` and `b > 1` implies `a < 1`.
2. `m > 1` and `n > 1` implies `a = m * n >= 4`.
3. `a < 1` AND `a >= 4` is impossible, resulting in the empty interval `[1, 0]`.

## Technical Challenges

- **Integer Overflow:** Standard `long long` multiplication can trigger undefined behavior or program aborts (via `-ftrapv`).
  - _Fix:_ Implemented `safe_multiply` using saturation arithmetic. It detects potential overflows against `LLONG_MAX`/`LLONG_MIN` bounds before execution.
- **Memory Management:** The parser (Flex) allocates strings via `strdup`.
  - _Fix:_ Implemented recursive AST deallocation (`freeSmtTerm`, `freeSmtProp`) to ensure zero leaks, verified by ASan.

## Future Work

- Replace array-based maps with hash tables (e.g., `uthash`).
- Extend support to floating-point arithmetic.
- Implement complete backward propagation for division operators.
