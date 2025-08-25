# sdb for macOS

This is my Apple Silicon version of `sdb`, the debugger developed in *Building
a Debugger*. The original project follows Linux on x86-64; this version uses
Darwin `ptrace`, Mach task APIs, ARM64 debug registers, Mach-O files, and dSYM
bundles instead.

The debugger can launch a program or attach to one that is already running.
It has software and hardware breakpoints, watchpoints, register and memory
commands, single stepping, syscall catchpoints, and an LLVM-backed
disassembler. Source breakpoints, local variables, backtraces, `next`, and
`finish` come from the target's DWARF information. It currently expects DWARF
version 4.

## Building it

The dependencies are available through Homebrew:

```sh
brew install cmake pkg-config libedit fmt llvm
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake signs the resulting binary with the debugger entitlement. The project is
macOS/ARM64-only.

## Debugging a program

Compile the program with debug information and leave optimization off while
stepping through it:

```sh
clang -g -gdwarf-4 -O0 program.c -o program
dsymutil ./program
./build/tools/sdb ./program
```

To attach instead, use `./build/tools/sdb -p PID`. Once inside `sdb`, `help`
prints the command list and `help breakpoint`, `help register`, and the other
subcommands show their own usage.
