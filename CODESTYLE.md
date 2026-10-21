# HideSo coding style

Applies to every C and C++ source file. No exceptions.

## Line wrapping

Never wrap a construct to satisfy a column limit. Function signatures, function calls, parameter lists, argument lists, initialiser lists, and conditions stay on one line.

## Casts

C-style casts only. Do not use `reinterpret_cast`, `static_cast`, `const_cast`, or `dynamic_cast`.

## Function pointers

Declare function pointers inline as `ReturnType (*Name)(Parameters)`. Do not use aliases or typedefs for function pointer types.

## Braces

Use Allman braces for functions, control flow, types, and `extern "C"`. Namespace braces stay inline and their bodies indent once. Use four spaces and no tabs.

## Naming

Use PascalCase for project-owned variables, functions, classes, structs, enums, members, and locals. Do not rename platform or library identifiers. Names must state their purpose.

## Strings

Do not use global or static string variables. Write string literals at their point of use.

## Code quality

Do not add source comments, unused code, dead logic, commented-out code, redundant wrappers, or unnecessary abstractions. Implement real error handling with no stubs or TODOs.

## Target

AArch64 Android 10/API 29 through Android 17/API 37. Builds must be clean under `-Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti` with C++20. Wrong-architecture builds fail during configuration and compilation.
