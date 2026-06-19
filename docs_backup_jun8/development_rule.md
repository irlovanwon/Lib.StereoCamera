# Development Rules

All development in this project must follow the rules below.

## 1. Build System — CMake

- All C++ projects must use **CMake** with a `CMakeLists.txt` file.
- Keep `CMakeLists.txt` structured and organized.

## 2. Reduce Code Redundancy

- Do NOT duplicate logic across modules or projects.
- For frequently used functions or shared source code, create a dedicated **`library/`** folder and place reusable code inside it.
- Before writing new code, check if a similar utility already exists in the shared library.

## 3. Coding Standards

- Follow all rules defined in `../../Coding/*`.
