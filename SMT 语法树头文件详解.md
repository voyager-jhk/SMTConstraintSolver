# SMT 语法树头文件详解

## 概述
该头文件定义了 SMT（Satisfiability Modulo Theories）语言的语法树结构，包含线性整数算术（LIA）、未解释函数（UF）和命题逻辑的核心数据结构。所有定义通过 `struct` 和 `enum` 实现，支持构建复杂的逻辑公式。


## 枚举类型介绍

### 1. **术语操作符（Term Operators）**
#### 二元操作符 (`SmtTermBop`)
```c
enum SmtTermBop {
    LIA_ADD = 1,  // 加法 (+)
    LIA_MINUS,    // 减法 (-)
    LIA_LSHIFT,   // 算术左移
    LIA_RSHIFT,   // 算术右移
    LIA_MULT,     // 乘法 (*)
    LIA_DIV       // 除法 (/)
};
```

#### 一元操作符 (`SmtTermUop`)
```c
enum SmtTermUop {
    LIA_NEG = LIA_DIV + 1  // 负号 (-)
};
```

### 2. 量词类型 (`SmtQuant`)
```c
enum SmtQuant {
    Forall = LIA_NEG + 1,  // 全称量词 (∀)
    Exists                 // 存在量词 (∃)
};
```

### 3. 术语类型 (`SmtTermType)
```c
enum SmtTermType {
    SMT_LiaBTerm = Exists + 1,  // LIA 二元运算表达式（如 a + b）
    SMT_LiaUTerm,               // LIA 一元运算表达式（如 -a）
    SMT_NiaBTerm,               // 非线性算术术语（如 a * b， 移位和乘除）
    SMT_UFTerm,                 // 未解释函数（如 f(a, b)）
    SMT_ConstNum,               // 常数（如 5）
    SMT_VarNum,                 // 变量编号（如 x#1）
    SMT_VarName                 // 变量名称（如 x）
};
```

### 4. **命题逻辑操作符**
#### 二元命题操作符 (`SmtPropBop)
```c
enum SmtPropBop {
    SMTPROP_AND = SMT_VarName + 1,  // 合取 (AND)
    SMTPROP_OR,                      // 析取 (OR)
    SMTPROP_IMPLY,                   // 蕴含 (→)
    SMTPROP_IFF                      // 双条件 (↔)
};
```

#### 一元命题操作符 (`SmtPropUop)
```c
enum SmtPropUop {
    SMTPROP_NOT = SMTPROP_IFF + 1  // 否定 (¬)
};
```

### 5. 二元谓词 (`SmtBinPred)
```c
enum SmtBinPred {
    SMT_LE = SMTPROP_NOT + 1,  // 小于等于 (≤)
    SMT_LT,                     // 小于 (<)
    SMT_GE,                     // 大于等于 (≥)
    SMT_GT,                     // 大于 (>)
    SMT_EQ                      // 等于 (=)
};
```

### 6. 命题类型 (`SmtPropType)
```c
enum SmtPropType {
    SMTB_PROP = SMT_EQ + 1,      // 基础命题（如 a ∧ b）
    SMTU_PROP,                    // 一元命题（如 ¬a）
    SMT_QUANT_PROP,               // 量词命题（如 ∀x. P(x)）
    SMTAT_PROP_EQ,                // 等式原子命题（如 a = b）
    SMTAT_PROP_LIA,               // LIA 原子命题（如 a + b ≤ 5）
    SMTAT_PROP_UF_EQ,             // 未解释函数等式（如 f(a) = g(b)）
    SMTAT_PROP_LIA_EQ,            // LIA 等式（如 a + b = 5）
    SMTAT_PROP_NIA_EQ,            // NIA 等式（如 a * b = 5）
    SMT_PROPVAR,                  // 命题变元（如 p1）
    SMTTF_PROP                    // 永真/永假命题（如 true/false）
};
/*    SMTAT_PROP_UF_EQ, 
    SMTAT_PROP_LIA_EQ, 
    SMTAT_PROP_NIA_EQ,   
    这三种命题类型在本次作业中不需要，所有等式都为  SMTAT_PROP_EQ， 不等式都为  SMTAT_PROP_LIA  */
```

---

## 核心数据结构

### 1. 未解释函数 (`UFunction`)
```c
struct UFunction {
    char* name;        // 函数名（如 "f"）
    int numArgs;       // 参数数量
    SmtTerm** args;    // 参数列表（指向 SmtTerm 数组）
};
```

### 2. SmtTerm
```c
struct SmtTerm {
    SmtTermType type;  // 术语类型（如 SMT_LiaBTerm）
    union {
        // 二元术语（如 a + b）
        struct {
            SmtTermBop op;    // 操作符（如 LIA_ADD）
            SmtTerm *t1, *t2; // 子术语
        } BTerm;
        
        // 一元术语（如 -a）
        struct {
            SmtTermUop op;    // 操作符（如 LIA_NEG）
            SmtTerm *t;       // 子术语
        } UTerm;
        
        int ConstNum;         // 常数或变量编号（如 5 或 x#1）
        char* Variable;       // 变量名称（如 "x"）
        UFunction* UFTerm;    // 未解释函数（如 f(a, b)）
    } term;
};
```

### 3. 命题结构 (SmtProp)
```c
struct SmtProp {
    SmtPropType type;  // 命题类型（如 SMT_QUANT_PROP）
    union {
        // 二元命题（如 a ∧ b）
        struct {
            SmtPropBop op;        // 操作符（如 SMTPROP_AND）
            SmtProp *prop1, *prop2; // 子命题
        } Binary_prop;
        
        // 一元命题（如 ¬a）
        struct {
            SmtPropUop op;        // 操作符（如 SMTPROP_NOT）
            SmtProp *prop1;       // 子命题
        } Unary_prop;
        
        // 量词命题（如 ∀x. P(x)）
        struct {
            char* quant_var;      // 量词变量名（如 "x"）
            SmtQuant type;        // 量词类型（如 Forall）
            SmtProp* body;        // 子命题 （如P(X)）
        } Quant_prop;
        
        // 原子命题（如 a = b 或 a + b ≤ 5）
        struct {
            SmtBinPred op;        // 谓词（如 SMT_EQ）
            SmtTerm* term1, *term2; // 操作数
        } Atomic_prop;
        
        int Propvar;              // 命题变元编号（如 p1）
        bool TF;                  // 永真/永假（true/false）
    } prop;
};
```

---

## 示例场景
假设需表示公式：
$$
\forall x. (x > 0) \implies (f(x) = 5)
$$

对应的语法树构建步骤：
1. **术语构建**：
   - `x`：`SmtTerm` 类型为 `SMT_VarName`，值为 `"x"`。
   - `f(x)`：`UFunction` 名为 `"f"`，参数为 `x`。
   - `5`：`SmtTerm` 类型为 `SMT_ConstNum`，值为 `5`。
2. **原子命题**：
   - `x > 0`：`SmtBinPred` 类型为 `SMT_GT`，操作数为 `x` 和 `0`。
   - `f(x) = 5`：`SmtBinPred` 类型为 `SMT_EQ`，操作数为 `f(x)` 和 `5`。
3. **复合命题**：
   - 使用 `SMTPROP_IMPLY` 连接两个原子命题。
4. **量词命题**：
   - `Quant_prop` 类型为 `Forall`，绑定变量 `x`。

---
