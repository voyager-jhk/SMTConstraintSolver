# 离散数学课程项目：基于区间算术的SMT约束求解器
## Discrete Mathematics Project: SMT Constraint Solver based on Interval Arithmetic

[![Language](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

这是一个为离散数学课程设计并实现的约束求解器。项目使用 C 语言，并借助 Flex 和 Bison 工具进行语法解析。其核心功能是验证一组给定的简单算术约束（等式与不等式）是否存在矛盾。

---

### 项目背景 (Project Background)

在计算机科学中，SMT (Satisfiability Modulo Theories) 求解器是程序验证、人工智能、芯片设计等领域的关键工具。本项目旨在通过实现一个专注于线性整数算术（LIA）理论的基础求解器，来深入理解约束满足问题（Constraint Satisfaction Problems）的核心原理。

求解器接收一系列命题作为输入，例如 `a + b < c + 1` 和 `a = m * n`，并判断是否存在一组整数赋值能够同时满足所有这些命题。

### 核心概念 (Core Concepts)

本项目主要基于以下两个核心概念：

1.  **区间算术 (Interval Arithmetic):**
    与传统算术操作单个数值不同，区间算术操作的是一个可能的值域 `[lower, upper]`。例如，如果变量 `x` 的范围是 `[1, 5]`，`y` 的范围是 `[2, 3]`，那么 `x + y` 的范围就是 `[1+2, 5+3] = [3, 8]`。我们使用这种方法来追踪每个变量和子表达式的可能取值范围。

2.  **约束传播 (Constraint Propagation):**
    这是一个迭代过程，通过不断利用命题中的约束来收紧变量的区间，直到达到一个稳定状态（不动点）。就像一个侦探根据线索不断排除嫌疑人一样，我们的求解器利用约束来排除不可能的数值。主要包含三种传播模式：
    * **正向传播:** 由子表达式的区间更新父表达式的区间 (e.g., 由 `I(m)` 和 `I(n)` 更新 `I(m*n)`)。
    * **关系传播:** 由关系符 (`<`, `=`, `>=` 等) 两侧的区间相互更新另一侧的区间。
    * **反向传播:** 由父表达式的区间反向更新其子表达式的区间 (e.g., 由 `I(a+b)` 和 `I(b)` 更新 `I(a)`)。

如果在这个过程中，任何一个变量或表达式的区间变为空（即 `lower > upper`），则证明整个约束系统存在矛盾。

### 功能实现 (Features)

* **语法解析:** 使用 Flex & Bison 解析包含 `+`, `-`, `*`, `/`, `<<`, `>>` 等操作的算术表达式和关系命题。
* **不动点迭代:** 实现了一个核心的循环，反复进行约束传播直到所有区间收敛。
* **矛盾检测:** 能够精确检测并报告导致区间为空的矛盾。
* **健壮的算术:** 实现了安全的64位整数乘法，以处理潜在的溢出问题。
* **内存安全:** 实现了对抽象语法树 (AST) 的完整递归内存释放，通过了 AddressSanitizer (ASan) 的内存泄漏检测。

### 编译与运行 (Build & Run)

1.  **编译环境:**
    需要 `gcc`, `flex`, `bison`。

2.  **编译指令:**
    ```bash
    gcc -O3 -g -Wno-return-type -Wno-parentheses -fsanitize=address -ftrapv smt_lang.c smt_lang_flex.c smt_lang.tab.c main.c -o test.out
    ```

3.  **运行程序:**
    ```bash
    ./test.out <path/to/your/testfile.txt>
    ```
    例如:
    ```bash
    ./test.out ../test_example/test2.txt
    ```

### 示例演示 (Example Usage)

考虑以下 `test2.txt` 中的约束：
```
(a + b) < (c + 1)
b > 1
a = m * n
n > 1
m > 1
c < 1
```

**运行输出:**
```
STARTING INTERVAL SOLVER...

INTERVAL SOLVER FINISHED.
Result: An empty interval was found (inconsistency detected).

--- Final Variable Intervals ---
Var 'a': [1, 0]
Var 'b': [2, 9223372036854775807]
Var 'c': [-9223372036854775808, 0]
Var 'm': [2, 9223372036854775807]
Var 'n': [2, 9223372036854775807]
```

**结果分析:**
程序正确地发现了矛盾。最终变量 `a` 的区间为 `[1, 0]`，这是一个空区间。推导过程如下：
1.  由 `c < 1` 和 `b > 1` 可推导出 `a + b < 2`。结合 `b > 1` (即 `b` 最小为2)，反向推导出 `a` 的值必须小于 1 (`a < 1`)，甚至可以更精确地推导出 `a <= -1`。
2.  由 `m > 1` 和 `n > 1` 可推导出 `m*n > 1` (实际上是 `m*n >= 4`)。
3.  结合 `a = m*n`，可知 `a` 的值必须大于 1。
4.  条件 1 和条件 3 产生了矛盾（`a < 1` 且 `a > 1`），因此该约束系统无解。

### 挑战与解决方案 (Challenges & Solutions)

1.  **整数溢出:** 在区间乘法中，`LLONG_MAX * LLONG_MAX` 这样的计算会轻易导致整数溢出，并被 `-ftrapv` 编译选项捕捉导致程序中止。
    * **解决方案:** 实现了一个 `safe_multiply` 辅助函数。该函数在执行乘法前，对操作数进行检查，预判所有可能溢出的情况（包括与 `LLONG_MIN`, `LLONG_MAX` 的运算），并返回正确的饱和值 (`LLONG_MAX` 或 `LLONG_MIN`)，从而避免了运行时中止。

2.  **内存泄漏:** AddressSanitizer (ASan) 初期报告了由 `yylex` 中 `strdup` 分配的字符串内存未被释放。
    * **解决方案:** 实现了一套完整的、递归的 AST 释放函数 (`freeSmtProplist`, `freeSmtProp`, `freeSmtTerm`)。在 `freeSmtTerm` 中，特别处理了 `SMT_VarName` 类型，确保 `term.Variable` 指向的动态分配内存被正确 `free`。在 `main` 函数退出前调用 `freeSmtProplist(root)`，彻底解决了内存泄漏问题。

### 未来工作 (Future Work)

* **数据结构升级:** 将当前基于数组的简易 map 替换为更高效的哈希表 (e.g., using `uthash.h`)。
* **支持浮点数:** 将 `long long` 扩展为 `double`，以支持实数理论，并处理浮点数比较的精度问题。
* **算法增强:** 实现更完整的反向传播规则，特别是针对乘法和除法。
* **学术化报告:** 撰写一份正式的学术报告，详细分析算法的复杂度、局限性，并与专业的 SMT 求解器（如 Z3）进行对比。
