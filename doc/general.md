# Funscript language documentation

This document's purpose is to provide general information about Funscript language.
It also contains links to the documents which describe more specific topics exhaustively.

### Overview

The Funscript language is a high-level multi-paradigm programming language.
It is designed to be a suitable tool for different purposes: writing small scripts,
developing full-featured standalone programs, embedding plugins into existing software.
Its clean syntax allows to use Funscript as a markup or data serialization language, too.

Funscript is mostly an imperative dynamically typed scripting language with employment of
various functional programming techniques (therefore the name, **Fun**script). A brief
enumeration of paradigm-specific techniques usable in Funscript follows.

* Functional
    - Function values
    - Lambda expressions
    - Higher-order functions
* Imperative
    - Flow control operators (expressions)
    - Assignment operator
* Procedural
    - Variable scoping
    - Modularity
* Object-oriented
    - Objects and methods (closures)
    - Standard library-driven custom object classes (types)

### Interpreter pipeline

Reference implementation of the language interpreter performs several steps during execution
of a program.

- **Tokenization.** The interpreter uses its own [lexer (tokenizer)](tokenizer.md) to convert
  input program into the stream of tokens.
- **Parsing.** Tokens are arranged into an AST following the [parser](parser.md) rules.
- **Compilation/assembly.** The AST is [compiled](ast.md) into bytecode instructions recursively.
  The [assembler](assembler.md) combines chunks of the bytecode.
- **Evaluation.** Funscript [VM](vm.md) follows the instructions of the bytecode thus
  executing the input program.