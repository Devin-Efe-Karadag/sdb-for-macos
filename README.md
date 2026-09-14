# sdb for macOS

This is my Apple Silicon port of the debugger from *Building a Debugger*. The
book builds for Linux on x86-64; this version talks to macOS through
`ptrace`, Mach task APIs and ARM64 debug registers. It reads Mach-O binaries
and their dSYM bundles.

It is deliberately a small debugger, but the usual learning tools are here:
breakpoints, watchpoints, stepping, backtraces, registers, memory inspection
and local variables.

## A first session

The project needs an ARM64 Mac, the Xcode command-line tools and Homebrew. Set
up the libraries and build:

```sh
brew install cmake pkg-config libedit fmt llvm
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

CMake also signs `build/tools/sdb` with the entitlement macOS requires for a
debugger.

Here is a tiny target to experiment with. Save it as `hello.c`:

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

Compile it without optimization and ask Clang for DWARF 4:

```sh
clang -g -gdwarf-4 -O0 hello.c -o hello
dsymutil ./hello
./build/tools/sdb ./hello
```

At the `sdb>` prompt:

```text
breakpoint set greet
continue
backtrace
variable locals
next
continue
quit
```

That sequence stops inside `greet`, prints the call stack and locals, steps
over one source line, and lets the program finish. `help` lists everything
else the command line understands.

## A few useful details

- Source-level commands expect DWARF 4. Newer DWARF versions are not handled
  yet.
- `./build/tools/sdb -p PID` attaches to an existing process. macOS will
  reject protected processes and processes it does not allow the debugger to
  inspect.
- Launching a program through `sdb` is usually less troublesome than
  attaching while learning the project.

Run the tests after building with:

```sh
ctest --test-dir build --output-on-failure
```

They exercise the ARM64 register code, stepping, software and hardware
breakpoints, watchpoints, threads, syscalls, dynamic libraries and attachment.
