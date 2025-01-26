# builtin_memcpy_inline
A (currently, toy) example of how one might use Clang's `__builtin_memcpy_inline` intrinsic to implement ~~fast~~ `memmove` and `memcpy` operations.

It's not competitive at the moment, but at least the tests pass. You can test with e.g. `./memtest64` after building, and benchmark with `./membench64` (or the exe with Wine or on Windows, if that's what you built).

# Why?
I thought that building versions of these functions with the restriction that the `size` argument for `__builtin_memcpy_inline` must be a compile-time constant would make for a fun challenge. Doing this without crazy amounts of copy-pasting for each possible case proved to be harder than expected, but an efficient use of macros made it turn out less horrible than it could have.

Also, I couldn't find a single example or explanation anywhere on how this builtin was meant to be used.

# Some benchmark results (64bit)
### vs. glibc (`make nodlsym`)
```
relative performance (ours vs stdlib):
                                |  avg GB/s   min GB/s   max GB/s   vs stdlib
--------------------------------|------------------------------------
            memcpy (aligned)    |    55.37      24.74      72.31     94.0%
            memcpy (unaligned)  |    39.22      18.92      58.90     72.3%
            memmove (forward)   |    54.15      24.70      71.62     91.9%
            memmove (backward)  |    62.48      25.43      89.92     97.2%
```

### vs. glibc (`make`):
```
                                |  avg GB/s   min GB/s   max GB/s   vs stdlib
--------------------------------|------------------------------------
            memcpy (aligned)    |    59.30      24.13      78.20    108.2%
            memcpy (unaligned)  |    54.57       5.74      78.20    150.3%
            memmove (forward)   |    59.43      24.28      78.23    107.6%
            memmove (backward)  |    64.21      25.40      90.79    102.7%
```

These results (with `dlsym`) are broken, because dynamically loading and using libc's string operations is not normally how it's used, as the compiler would usually generate inline instructions depending on the situation. I made this option for a more "fair" comparison, but that just doesn't work right.

### vs. Windows' ucrtbase.dll (with Wine, but native dll copied from Windows (`make CROSS=1`)):
```
                                |  avg GB/s   min GB/s   max GB/s   vs stdlib
--------------------------------|------------------------------------
        memcpy (aligned)        |    54.70      24.73      68.11     87.9%
        memcpy (unaligned)      |    53.71      24.51      68.99     97.6%
        memmove (forward)       |    54.90      25.15      68.08     88.6%
        memmove (backward)      |    55.65      22.34      67.95     89.4%
```

`nodlsym` (`GetProcAddress`) performance is the same on Windows/Wine, somehow.

# Building
`make` for Linux, or `make CROSS=1` to build for Windows. Either option can also link without `dlsym`/`GetProcAddress` if you use the `make nodlsym` target. It also builds on Windows proper (with `make`) if you have `make`, `rm`, and (new-ish) `clang`.

# Using
Link with it statically or dynamically and use `memcpy_local` or `memmove_local` instead of the non-suffixed versions. Or just steal the code.

# TODO
Make it actually fast.
