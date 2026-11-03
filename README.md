# FreeBSD ncurses Input Timeouts Can Overrun After Backward Clock Adjustments

A reproducer and patch for FreeBSD ncurses input timeouts that can overrun after a backward system-clock adjustment.

## Problem and Impact

In the inspected FreeBSD 16.0-CURRENT configuration, ncurses uses `CLOCK_REALTIME` to measure elapsed time while waiting for input in functions such as `getch()`. If the system clock moves backward during a timed wait, ncurses underestimates the elapsed time and incorrectly increases the remaining timeout. In certain configurations, it waits again, so the input timeout takes longer than requested.

This issue concerns a **backward clock adjustment during the wait**. A forward adjustment makes ncurses overestimate the elapsed time and may cause the timeout to expire early; it does not explain an extended wait.

## Source Analysis

The following implementation was inspected in the FreeBSD 16.0-CURRENT source tree at `/usr/src` (revision `61c70dc16` at the time of inspection):

1. In the `HAVE_CLOCK_GETTIME` branch, `contrib/ncurses/ncurses/curses.priv.h` defines `GetClockTime(t)` as `clock_gettime(CLOCK_REALTIME, t)`.
2. In `contrib/ncurses/ncurses/tty/lib_twait.c`, `_nc_timed_wait()` uses `_nc_gettime()` before and after waiting to measure elapsed time, then subtracts that duration from the remaining timeout.
3. If the wait times out while more than 100 ms remain, the code calls `napms(100)` and retries with the updated timeout. The inspected FreeBSD configuration enables `poll()`, `HAVE_CLOCK_GETTIME`, and `HAVE_NANOSLEEP`, so this retry path applies.
4. The FreeBSD kernel's `kern_poll_kfds()` converts the relative `poll()` timeout into a timer based on monotonic uptime. The initial `poll()` wait is therefore unaffected by wall-clock changes, while ncurses subsequently uses `CLOCK_REALTIME` to calculate the remaining timeout. These two parts use different clock semantics.

For example, consider a 2-second wait with the system clock moved backward by 3 seconds about 100 ms after the wait starts. The initial `poll()` still returns after about 2 seconds. However, ncurses sees only about -1 second of elapsed wall-clock time, miscalculates about 3 seconds as remaining, and waits again. The total wait can be about 5 seconds instead of 2 seconds. Actual timings depend on scheduling and clock resolution.

## Reproduction

The complete interactive reproducer is in `ncurses-monotonic-clock.cpp`. Build and run it on a FreeBSD test machine:

```sh
cd /path/to/ncurses-monotonic-clock
make
sudo ./ncurses-monotonic-clock
```

The program requires an effective user ID of root. Run it as root directly, or use `sudo` if configured on the system. Then:

1. Press `B` in the curses interface.
2. The parent process enters a 2-second `getch()` wait on the next loop iteration. A child process sleeps for about 100 ms and then moves `CLOCK_REALTIME` backward by 3 seconds.
3. After the call returns, check the elapsed time measured with `steady_clock`. With the affected ncurses library, the wait is typically about 5 seconds. With ncurses rebuilt using the patch, it is expected to be close to 2 seconds. Exact timings depend on scheduling and clock resolution.

The reproducer uses `fork()` to adjust the clock concurrently with the curses wait. The parent reaps the child with `waitpid()` and reports clock-adjustment failures. Rebuilding only the reproducer does not fix the system library; for a before-and-after comparison, run it in an isolated test environment with both the unpatched and patched ncurses libraries.

**Warning: this test changes the FreeBSD host's system clock.** Run it only on a controlled test machine or virtual machine, and consider its effects on other services, logs, and tests. Do not run it on a production system.

The earlier test, which moved the clock forward after `getch()` returned, did not exercise this issue: it changed the clock outside the wait and in the opposite direction from the backward adjustment that extends the wait.

## Proposed Fix

Elapsed-time measurements should use a monotonic clock rather than wall-clock time, which can be adjusted by an administrator or a time-synchronization service. Where `CLOCK_MONOTONIC` is available, use `clock_gettime(CLOCK_MONOTONIC, ...)` for ncurses' internal elapsed-time measurements. Platforms without that macro retain the existing compatibility implementation.

The patch is in [ncurses-monotonic-clock.patch](./ncurses-monotonic-clock.patch). It changes only an ncurses private timing macro. It applies to the inspected FreeBSD source layout; after applying it, rebuild and test the FreeBSD base-system ncurses library. Rebuilding the reproducer alone is not sufficient. Also run ncurses' own tests to check other paths that depend on `_nc_gettime()`, including escape-sequence and keyboard timeouts.

The patch is reproduced below (paths are relative to the FreeBSD source tree root):

```diff
diff --git a/contrib/ncurses/ncurses/curses.priv.h b/contrib/ncurses/ncurses/curses.priv.h
--- a/contrib/ncurses/ncurses/curses.priv.h
+++ b/contrib/ncurses/ncurses/curses.priv.h
@@ -794,4 +794,8 @@
 #if HAVE_CLOCK_GETTIME
 # define PRECISE_GETTIME 1
+# if defined(CLOCK_MONOTONIC)
+#  define GetClockTime(t) clock_gettime(CLOCK_MONOTONIC, t)
+# else
 # define GetClockTime(t) clock_gettime(CLOCK_REALTIME, t)
+# endif
 # define TimeType struct timespec
```

## Measured Verification

A before-and-after comparison was run on a FreeBSD 16.0-CURRENT test system. Both ncurses variants were built from `/usr/src` with the same FreeBSD Makefile configuration, using separate `MAKEOBJDIRPREFIX` values and isolated runtime directories. The reproducer loaded each variant's `libncursesw.so.9` and `libtinfow.so.9` through `LD_LIBRARY_PATH`; `ldd` confirmed which libraries were loaded in each run. No system libraries were installed or replaced, and the original source under `/usr/src` was not modified.

| Library | 2-second `getch()` wait while the clock was moved backward | Subsequent 2-second wait without a clock adjustment |
|---|---:|---:|
| Unpatched | 5188 ms | 2132 ms |
| Patched | 2082 ms | 2135 ms |

In both runs, the reproducer reported `Clock moved backward by 3 seconds during getch().` During the clock adjustment, the unpatched library exceeded the requested wait by about 3.19 seconds. The patched library returned to about 2 seconds, close to the control wait without a clock change. This comparison shows that the patch eliminates the reproduced delay with this FreeBSD source version and build configuration.

## Verification Scope

The measured test required and did move the test host's system clock backward by 3 seconds. Confirm that this is acceptable before running the reproducer. The comparison verifies the tested FreeBSD version, build configuration, and input-timeout path; other platforms and ncurses timeout paths still require their own tests.
