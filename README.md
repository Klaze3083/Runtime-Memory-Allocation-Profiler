# memtrace-pin — Runtime Memory Allocation Profiler

A dynamic memory analysis tool built with **Intel PIN** that intercepts `malloc`, `calloc`, `realloc`, and `free` calls at runtime — without modifying or recompiling the target binary.

Built as part of the **Computer Architecture (CS204)** course at **IIT Ropar**, under **Dr. Neeraj Goel**.

---

## What It Does

- Intercepts every `malloc`, `calloc`, `realloc`, and `free` call at runtime
- Records the **address**, **size**, and **calling function** for each allocation
- Tracks all **active allocations** and flags ones never passed to `free` as leaks
- Generates a **per-function report** showing total bytes allocated and allocation count
- Writes everything to `mem_report.out`

---

## Contents

| File | Description |
|------|-------------|
| `MyPinTool.cpp` | Full PIN tool source code |

---

## Prerequisites

- **Linux** (64-bit) — PIN tools are Linux-native
- **Intel PIN** — Download from [Intel's official site](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html)
- **g++** with C++11 support
- **make**

---

## Setup

### 1. Download and extract Intel PIN

```bash
tar -xzf pin-<version>-gcc-linux.tar.gz
```

Set an environment variable pointing to it (add to your `.bashrc` for convenience):

```bash
export PIN_ROOT=/path/to/pin-<version>-gcc-linux
```

### 2. Clone this repo into the PIN tools directory

The easiest way is to place the tool inside PIN's `source/tools/` directory:

```bash
cd $PIN_ROOT/source/tools/
git clone https://github.com/Klaze3083/memtrace-pin
cd memtrace-pin
```

---

## Building the Tool

### Option A — Using PIN's built-in makefile system (recommended)

Create a file named `makefile.rules` in the same directory with this content:

```makefile
TEST_TOOL_ROOTS := MyPinTool
```

Then build:

```bash
make PIN_ROOT=$PIN_ROOT
```

This produces `obj-intel64/MyPinTool.so` (or `obj-ia32/` on 32-bit).

### Option B — Manual build command

```bash
g++ -Wall -Werror -Wno-unknown-pragmas \
    -DBIGARRAY_MULTIPLIER=1 -DUSING_XED \
    -DTARGET_IA32E -DHOST_IA32E -DTARGET_LINUX \
    -I$PIN_ROOT/source/include/pin \
    -I$PIN_ROOT/source/include/pin/gen \
    -I$PIN_ROOT/extras/components/include \
    -I$PIN_ROOT/extras/xed-intel64/include/xed \
    -I$PIN_ROOT/source/tools/Utils \
    -I$PIN_ROOT/source/tools/InstLib \
    -O3 -fomit-frame-pointer -fno-stack-protector \
    -fno-exceptions -funwind-tables \
    -fasynchronous-unwind-tables \
    -fno-rtti -fPIC \
    -shared -Wl,--hash-style=sysv \
    -Wl,-Bsymbolic \
    -L$PIN_ROOT/intel64/lib \
    -L$PIN_ROOT/intel64/lib-ext \
    -L$PIN_ROOT/extras/xed-intel64/lib \
    -o MyPinTool.so MyPinTool.cpp \
    -lpin -lxed -lpindwarf -ldl -lrt
```

---

## Running the Tool

Run it against any compiled binary (here `./target` is the program you want to analyse):

```bash
$PIN_ROOT/pin -t obj-intel64/MyPinTool.so -- ./target
```

The tool will print live logs to **stderr** as it runs, and write the full report to **`mem_report.out`** once the program exits.

---

## Reading the Output

### Live logs (stderr)

While running you will see lines like:

```
Loaded Image: /lib/x86_64-linux-gnu/libc.so.6
Replacing malloc in: /lib/x86_64-linux-gnu/libc.so.6
Replacing calloc in: /lib/x86_64-linux-gnu/libc.so.6
...
```

This confirms which functions were successfully intercepted.

### mem_report.out

The output file has three sections:

**1. Per-event log** — one line per allocation/free:
```
[ALLOC]   Addr: 0x55a3b1c2d010  Size: 64    Func: main
[CALLOC]  Addr: 0x55a3b1c2d060  Size: 128   Func: init_buffer
[FREE ]   Addr: 0x55a3b1c2d010  Size: 64    Func: main
[REALLOC] Addr: 0x55a3b1c2d080  Size: 256   Func: grow_array
```

**2. Allocation summary per function:**
```
-- Allocation Summary Per Function --
Function: main         | Total Bytes: 192  | Alloc Count: 3
Function: init_buffer  | Total Bytes: 128  | Alloc Count: 1
```

**3. Memory leaks (active allocations never freed):**
```
-- Active Allocations (Leaks) --
Leaked Addr: 0x55a3b1c2d060  Size: 128  Func: init_buffer
```

If this section is empty, there are no detected leaks.

---

## Notes

- The tool resolves **caller function names** using PIN's `RTN_FindByAddress` on the return IP — so the function shown is the one that called malloc, not malloc itself.
- Thread safety is handled with `PIN_LOCK` so the tool works correctly on multi-threaded programs.
- Symbols need to be present in the binary for function names to resolve properly. Compile your target with `-g` for best results:
  ```bash
  g++ -g -o target target.cpp
  ```

---

## References

- [Intel PIN Documentation](https://software.intel.com/sites/landingpage/pintool/docs/98484/Pin/html/)
- [PIN Downloads](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html)
