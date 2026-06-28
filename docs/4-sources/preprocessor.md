# Preprocessor

Sew contains a static C++ preprocessor implementation that resolves macros and conditional branches during compilation.

## Directive Evaluation

The preprocessor parses source files line-by-line, tracking directives and rewriting the source to produce a clean compilation unit:

* **Macros**:
  * `#define NAME value`: Stores the macro value and maps all subsequent occurrences.
  * `#undef NAME`: Removes the macro mapping.
* **Conditionals**:
  * `#ifdef`, `#ifndef`, `#if`, `#else`, `#elif`, `#endif`: Evaluates conditional logical expressions.
  * Expressions support comparisons (`==`, `!=`, `<`, `>`, `<=`, `>=`), hex literals (`0xFF`), and logical operators (`defined(X) && !defined(Y)`).
  * Directives and inactive code branches are stripped from the source before compilation.
* **System/Local Includes**:
  * `#include "file.hpp"`: Resolves local include paths relative to the current source file.
  * `#include <cstdio>`: System header includes are kept in the final source file, ensuring they are compiled correctly by Clang.
* **Header Guards**:
  * `#pragma once`: Tracks parsed header file paths to prevent duplicate header inclusions.
