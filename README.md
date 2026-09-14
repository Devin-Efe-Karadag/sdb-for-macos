# sdb for macOS

`sdb` is a small command-line debugger for Apple Silicon Macs. It can pause a
program, show where it stopped, inspect variables and registers, and continue
one line or instruction at a time.

I started from the debugger built in *Building a Debugger*, which targets
Linux on x86-64, and replaced the platform-specific parts with Darwin
`ptrace`, Mach APIs, ARM64 debug registers, Mach-O parsing, and dSYM support.

## Before you start

This project only supports macOS on ARM64. You will need the Xcode command-line
tools and [Homebrew](https://brew.sh/). Install the remaining dependencies with:

```sh
brew install cmake pkg-config libedit fmt llvm
```

## Build sdb

From the repository directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The debugger will be at `build/tools/sdb`. CMake signs it with the debugger
entitlement required by macOS.

## Try a debugging session

Create a small program called `hello.c`:

```c
#include <stdio.h>

static void greet(const char *name) {
    printf("Hello, %s!\n", name);
}

int main(void) {
    greet("debugger");
    return 0;
}
```

Compile it with debug information. DWARF 4 is required because newer DWARF
versions are not supported yet.

```sh
clang -g -gdwarf-4 -O0 hello.c -o hello
dsymutil ./hello
./build/tools/sdb ./hello
```

At the `sdb>` prompt, try:

```text
breakpoint set greet
continue
backtrace
variable locals
next
continue
quit
```

`breakpoint set greet` stops when `greet` is entered. `backtrace` shows the
call stack, `variable locals` prints variables in the selected frame, and
`next` executes the current source line without stepping into another
function. Run `help` inside the debugger for the complete command list.

To attach to an already-running process instead, use:

```sh
./build/tools/sdb -p PID
```

macOS may refuse attachment to protected or unsigned processes. Starting a
program through `sdb` is the easiest first test.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The test suite covers registers, source stepping, breakpoints, watchpoints,
threads, syscalls, dynamic libraries, and process attachment.
