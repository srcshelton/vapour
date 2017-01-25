Vapour
======

Vapour started off as an attempt to fix the case-sensitivity issues of Valve's
Steam content-distribution system by intercepting `open()` syscalls and
attempting to adjust the capitalisation of paths which aren't accessible.
However, during the short development process a major steam update was made
available which blocked the ability to `dtruss` the Steam process (and so see
the ways in which it is broken) and also to prevent the use of
`DYLD_INSERT_LIBRARIES` mechanism to inject the updated functions - presumably
to protect Steam's DRM mechanism.

Even with this being the case, `libc_hook_open` remains a useful demonstration
of how to cleanly hook functions on Mac OS X/macOS.

Building and running
====================

Build with:

```
clang -Wall -arch x86_64 -arch i386 -dynamiclib -o libc_hook_open.dylib libc_hook_open.c
```

Run a static analysis (using a third-party LLVM/clang build, since Apple don't appear to supply [scan-build](https://clang-analyzer.llvm.org/scan-build.html) with Xcode):

```
scan-build clang -Wall -arch x86_64 -arch i386 -dynamiclib -o libc_hook_open.dylib libc_hook_open.c
```

Execute as:

```
DYLD_INSERT_LIBRARIES="/path/to/vapour/libc_hook_open.dylib" cat /dev/null
```

Code details
============

Please note the values of:

```
#define DEBUG 1
```

... and:

```
#define HOOK_IMPLEMENTATION 1
```

... where the former constant outputs additional debugging information when
performing path-mangling operations, and the latter hooks all of OS X's `open()`
functions, including `_nocancel` variants, at the cost of losing the ability to
recursively call `open()` or to use any function which internally calls `open()`
itself from within the hook functions.
