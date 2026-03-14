# Essentials of Compilation — An Incremental Approach in Racket

**Author:** Jeremy G. Siek
**Publisher:** The MIT Press, 2023
**ISBN:** 9780262047760

---

## Preface — pp. xi–xiv

- Acknowledgments — p. xiv

---

## Chapter 1: Preliminaries — pp. 1–10

| Section | Title | Pages |
|---------|-------|-------|
| 1.1 | Abstract Syntax Trees | 1–2 |
| 1.2 | Grammars | 3–5 |
| 1.3 | Pattern Matching | 4–5 |
| 1.4 | Recursive Functions | 6 |
| 1.5 | Interpreters | 6–8 |
| 1.6 | Example Compiler: A Partial Evaluator | 9–10 |

---

## Chapter 2: Integers and Variables — pp. 11–32

| Section | Title | Pages |
|---------|-------|-------|
| 2.1 | The L_Var Language | 11–13 |
| 2.2 | The x86_Int Assembly Language | 14–18 |
| 2.3 | Planning the Trip to x86 | 19–22 |
| 2.4 | Uniquify Variables | 23–24 |
| 2.5 | Remove Complex Operands | 25–26 |
| 2.6 | Explicate Control | 27 |
| 2.7 | Select Instructions | 28–29 |
| 2.8 | Assign Homes | 30 |
| 2.9 | Patch Instructions | 30 |
| 2.10 | Generate Prelude and Conclusion | 31 |
| 2.11 | Challenge: Partial Evaluator for L_Var | 32 |

---

## Chapter 3: Register Allocation — pp. 33–53

| Section | Title | Pages |
|---------|-------|-------|
| 3.1 | Registers and Calling Conventions | 34–35 |
| 3.2 | Liveness Analysis | 36–39 |
| 3.3 | Build the Interference Graph | 40 |
| 3.4 | Graph Coloring via Sudoku | 41–46 |
| 3.5 | Patch Instructions | 47 |
| 3.6 | Generate Prelude and Conclusion | 48 |
| 3.7 | Challenge: Move Biasing | 49–52 |
| 3.8 | Further Reading | 53 |

---

## Chapter 4: Booleans and Conditionals — pp. 55–79

| Section | Title | Pages |
|---------|-------|-------|
| 4.1 | The L_If Language | 56 |
| 4.2 | Type Checking L_If Programs | 57–61 |
| 4.3 | The C_If Intermediate Language | 62 |
| 4.4 | The x86_If Language | 62–63 |
| 4.5 | Shrink the L_If Language | 64 |
| 4.6 | Uniquify Variables | 65 |
| 4.7 | Remove Complex Operands | 65 |
| 4.8 | Explicate Control | 66–71 |
| 4.9 | Select Instructions | 72 |
| 4.10 | Register Allocation | 73–74 |
| 4.11 | Patch Instructions | 75 |
| 4.12 | Challenge: Optimize Blocks and Remove Jumps | 75–78 |
| 4.13 | Further Reading | 79 |

---

## Chapter 5: Loops and Dataflow Analysis — pp. 81–93

| Section | Title | Pages |
|---------|-------|-------|
| 5.1 | The L_While Language | 82 |
| 5.2 | Cyclic Control Flow and Dataflow Analysis | 83–87 |
| 5.3 | Mutable Variables and Remove Complex Operands | 88 |
| 5.4 | Uncover get! | 89 |
| 5.5 | Remove Complex Operands | 90 |
| 5.6 | Explicate Control and C_◇ | 91 |
| 5.7 | Select Instructions | 92 |
| 5.8 | Register Allocation | 92–93 |

---

## Chapter 6: Tuples and Garbage Collection — pp. 95–124

| Section | Title | Pages |
|---------|-------|-------|
| 6.1 | The L_Tup Language | 95–97 |
| 6.2 | Garbage Collection | 98–105 |
| 6.3 | Expose Allocation | 106 |
| 6.4 | Remove Complex Operands | 107 |
| 6.5 | Explicate Control and the C_Tup Language | 107 |
| 6.6 | Select Instructions and the x86_Global Language | 108–112 |
| 6.7 | Register Allocation | 113 |
| 6.8 | Generate Prelude and Conclusion | 113–115 |
| 6.9 | Challenge: Simple Structures | 116–117 |
| 6.10 | Challenge: Arrays | 118–122 |
| 6.11 | Challenge: Generational Collection | 123 |
| 6.12 | Further Reading | 124 |

---

## Chapter 7: Functions — pp. 125–141

| Section | Title | Pages |
|---------|-------|-------|
| 7.1 | The L_Fun Language | 125–129 |
| 7.2 | Functions in x86 | 130–132 |
| 7.3 | Shrink L_Fun | 133 |
| 7.4 | Reveal Functions and the L_FunRef Language | 133 |
| 7.5 | Limit Functions | 133 |
| 7.6 | Remove Complex Operands | 134 |
| 7.7 | Explicate Control and the C_Fun Language | 134–135 |
| 7.8 | Select Instructions and the x86_callq* Language | 136–138 |
| 7.9 | Register Allocation | 139 |
| 7.10 | Patch Instructions | 139 |
| 7.11 | Generate Prelude and Conclusion | 140 |
| 7.12 | An Example Translation | 141 |

---

## Chapter 8: Lexically Scoped Functions — pp. 143–158

| Section | Title | Pages |
|---------|-------|-------|
| 8.1 | The L_λ Language | 145–147 |
| 8.2 | Assignment and Lexically Scoped Functions | 148 |
| 8.3 | Assignment Conversion | 148–149 |
| 8.4 | Closure Conversion | 150–152 |
| 8.5 | Expose Allocation | 153 |
| 8.6 | Explicate Control and C_Clos | 153 |
| 8.7 | Select Instructions | 153–155 |
| 8.8 | Challenge: Optimize Closures | 156–157 |
| 8.9 | Further Reading | 158 |

---

## Chapter 9: Dynamic Typing — pp. 159–175

| Section | Title | Pages |
|---------|-------|-------|
| 9.1 | The L_Dyn Language | 159–160 |
| 9.2 | Representation of Tagged Values | 161–163 |
| 9.3 | The L_Any Language | 164–169 |
| 9.4 | Cast Insertion: Compiling L_Dyn to L_Any | 170 |
| 9.5 | Reveal Casts | 171 |
| 9.6 | Remove Complex Operands | 172 |
| 9.7 | Explicate Control and C_Any | 172 |
| 9.8 | Select Instructions | 172–174 |
| 9.9 | Register Allocation for L_Any | 175 |

---

## Chapter 10: Gradual Typing — pp. 177–194

| Section | Title | Pages |
|---------|-------|-------|
| 10.1 | Type Checking L_? | 177–178 |
| 10.2 | Interpreting L_Cast | 179–186 |
| 10.3 | Cast Insertion | 187–188 |
| 10.4 | Lower Casts | 189 |
| 10.5 | Differentiate Proxies | 189–191 |
| 10.6 | Reveal Casts | 192 |
| 10.7 | Closure Conversion | 193 |
| 10.8 | Select Instructions | 193 |
| 10.9 | Further Reading | 194 |

---

## Chapter 11: Generics — pp. 197–207

| Section | Title | Pages |
|---------|-------|-------|
| 11.1 | Compiling Generics | 204 |
| 11.2 | Resolve Instantiation | 205 |
| 11.3 | Erase Generic Types | 205–207 |

---

## Appendix A — pp. 209–211

| Section | Title | Pages |
|---------|-------|-------|
| A.1 | Interpreters | 209 |
| A.2 | Utility Functions | 209 |
| A.3 | x86 Instruction Set Quick Reference | 210–211 |

---

## References — pp. 213–219

## Index — pp. 221–237
