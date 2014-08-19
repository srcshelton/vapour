Vapour
======

Vapour started off as an attempt to fix the case-sensitivity issues of Valve's
Steam content-distribution system by intercepting open() syscalls and
attempting to adjust the capitalisation of paths which aren't accessible.
However, during the short development process a major steam update was made
available which blocks the ability to `dtruss` the Steam process (and so see
the ways in which it is broken) and prevents to use of `DYLD_INSERT_LIBRARIES`
to inject the updated functions.

Even with this being the case, `libc_hook_open` remains a useful demonstration
of how to cleanly hook functions on Mac OS X.

Building and running
====================

Build with:

```
clang -Wall -arch x86_64 -arch i386 -dynamiclib -o libc_hook_open.dylib libc_hook_open.c
```

Run a static analysis with:

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
#define DEBUG 0
```

... and:

```
#define HOOK_IMPLEMENTATION 1
```

... where the former constant outputs additional debugging information when
performing path-mangling operations, and the latter hooks all of OS X's open()
functions, including `_nocancel` variants, at the cost of losing the ability to
recursively call open() or use any function which internally calls open()
itself from within the hook functions.
