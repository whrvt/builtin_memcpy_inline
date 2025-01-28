# builtin_memcpy_inline
A (currently, toy) example of how one might use Clang's `__builtin_memcpy_inline` intrinsic to implement ~~fast~~ `memmove` and `memcpy` operations in a freestanding environment.

It's not very competitive at the moment, but at least the tests pass. You can test with e.g. `./memtest64` after building, and benchmark with `./membench64` (or the exe with Wine or on Windows, if that's what you built).

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

### vs. Windows without dlsym (`make CROSS=1 nodlsym`)
```
                                |  avg GB/s   min GB/s   max GB/s   vs stdlib
--------------------------------|------------------------------------
        memcpy (aligned)        |    62.26      25.71      82.69    113.4%
        memcpy (unaligned)      |    55.02      25.39      77.49    102.5%
        memmove (forward)       |    62.22      25.54      82.82    113.9%
        memmove (backward)      |    62.27      25.53      89.72    111.7%
```

### vs. Windows' ucrtbase.dll (with Wine, but native dll copied from Windows (`make CROSS=1`)):
```
                                |  avg GB/s   min GB/s   max GB/s   vs stdlib
--------------------------------|------------------------------------
        memcpy (aligned)        |    54.70      24.73      68.11     87.9%
        memcpy (unaligned)      |    53.71      24.51      68.99     97.6%
        memmove (forward)       |    54.90      25.15      68.08     88.6%
        memmove (backward)      |    55.65      22.34      67.95     89.4%
```

I have no explanation as of yet why the `GetProcAddress` version (second) is much slower on average.

# Building
`make` for Linux, or `make CROSS=1` to build for Windows. Either option can also link without `dlsym`/`GetProcAddress` if you use the `make nodlsym` target. It also builds on Windows proper (with `make`) if you have `make`, `rm`, and (new-ish) `clang`.

If you have `musl-clang`, then you can also try `make MUSL=1` to build against musl's implementation. This is the only version of memcpy/memmove that this implementation easily beats in every case.

There's also the `make asan` target, but that's not very useful unless you're experimenting with dubious modifications.

There are more experimental targets/build options to consider benchmarking against (like w/ `-static`), but the current selection is already pretty useful.

# Using
Link with it statically or dynamically and use `memcpy_local` or `memmove_local` instead of the non-suffixed versions. Or just steal the code.

# TODO
Make it actually fast.
 - Currently not differentiating between aligned and unaligned copies/moves.
   - I still want to rely on builtins where possible to let the compiler optimize as it sees fit. It might not be optimal, but it's part of the unwritten restriction I gave myself.
