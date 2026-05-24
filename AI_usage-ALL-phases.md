# AI Usage Documentation — All Phases

## Tool used

**Claude (Anthropic)** — accessed interactively through the Claude Code CLI during
development of `city_manager`, `monitor_reports`, `city_hub`, and `scorer`.

---

## Phase 1 — `city_manager` (file I/O, permissions, symlinks)

### What I asked the AI to generate

#### Function 1: `parse_condition`

**Prompt given (paraphrased):**

> I have a C struct called `Report` with fields: `id` (int), `inspector` (char[64]),
> `latitude` (double), `longitude` (double), `category` (char[32]), `severity` (int),
> `timestamp` (time_t), `description` (char[128]). I need a function with the signature:
> ```c
> int parse_condition(const char *input, char *field, char *op, char *value);
> ```
> that splits a string of the form `field:operator:value` into its three parts. The
> operator can be `==`, `!=`, `<`, `<=`, `>`, `>=`. Return 0 on success and -1 on
> malformed input.

**What was generated:**

The AI produced a function that:
1. Uses `strchr` to find the first `:` and copies everything before it into `field`.
2. Uses a second `strchr` starting after the first `:` to find the boundary between
   operator and value.
3. Copies the operator and the rest of the string into `op` and `value`.

#### Function 2: `match_condition`

**Prompt given (paraphrased):**

> Using the same `Report` struct, generate a function:
> ```c
> int match_condition(Report *r, const char *field, const char *op, const char *value);
> ```
> that returns 1 if the record satisfies the condition and 0 otherwise. Integer fields
> are `severity` and `timestamp`; string fields are `category` and `inspector`.

**What was generated:**

The AI produced a function that uses `strcmp` on the `field` argument to dispatch to
the correct field, converts the `value` string to a numeric type when needed
(`atol` / `atoll`), and compares using a chain of `strcmp(op, …)` checks for each
operator.

### What I reviewed and changed in Phase 1

#### `parse_condition`

- **Bounds check on field length**: The original did not verify that `flen >= 32`. I
  added an explicit `if (flen == 0 || flen >= 32) return -1;` guard to prevent a
  potential buffer overrun if an extremely long field token was supplied.
- **Bounds check on operator length**: Same issue — the operator buffer is only 4
  bytes. Added `if (olen == 0 || olen >= 4) return -1;`.
- **`value` null-termination**: Changed from an open-ended `strcpy` to
  `strncpy(value, c2+1, 63); value[63] = '\0';` to prevent overflow.

#### `match_condition`

- **Explicit casts for `timestamp`**: The original compared `r->timestamp` directly
  with `atoll(value)` without a cast. Since `time_t` can be `long` or `long long`
  depending on the platform, I added an explicit `(long long)` cast on both sides
  to avoid silent narrowing.
- **Variable naming**: Renamed the local comparison result variable from a single
  letter to `c` (for string fields) and `rv`/`lv` (for integer fields) to make the
  comparisons easier to follow.
- **Structure**: The original used nested ternary chains; I rewrote them as explicit
  `if / else if` chains because they are easier to reason about line-by-line, which
  is important for the presentation.

### What was correct without changes (Phase 1)

- The core logic of splitting on `:` with two `strchr` calls was correct and I kept
  it unchanged.
- The dispatch on `field` name using `strcmp` was correct.
- The use of `atol` for `severity` and `atoll` for `timestamp` was appropriate — the
  AI correctly identified which fields are integers.
- The `strcmp`-based ordering for string fields (`<`, `>`, etc.) is correct behaviour
  for lexicographic comparison.

### What I wrote myself in Phase 1 (not AI-generated)

Everything outside the two functions above was written by me: the `Report` struct,
`mode_to_str`, all command implementations (`cmd_add`, `cmd_list`, `cmd_view`,
`cmd_remove_report`, `cmd_update_threshold`, `cmd_filter`), `ensure_district`,
the symbolic-link helpers, `log_action`, `next_report_id`, and the argument-parsing
logic in `main`.

### What I learned from Phase 1

1. **`parse_condition` edge cases**: The AI did not handle empty tokens (`::value`)
   or oversized tokens. Real defensive C code always validates bounds before writing.
2. **Alignment hazards**: When using `__attribute__((packed))` structs, taking
   `&r->timestamp` and passing it to a function expecting a `long long *` can be
   undefined behaviour due to misalignment.
3. **Presentation preparedness**: Reading both functions line-by-line and making
   deliberate changes meant I can now explain every line and every guard.

---

## Phase 2 — Processes and Signals

### What I asked the AI to generate

**Prompt given (paraphrased):**

> Add a `remove_district` command to `city_manager`. It must be manager-only, use
> `fork()` and `execl("/bin/rm", "rm", "-rf", district)` to delete the directory,
> wait for the child with `waitpid()`, then remove the `active_reports-<district>`
> symlink with `unlink()`.
>
> Also add a `notify_monitor()` helper that reads `.monitor_pid`, sends `SIGUSR1` to
> that PID with `kill()`, and logs whether it succeeded or failed to
> `logged_district`. Call it at the end of `cmd_add()`.
>
> Write a separate `monitor_reports` program that writes its PID to `.monitor_pid`
> on startup and deletes it on exit. It should use `sigaction()` (not `signal()`)
> to handle SIGUSR1 (print a message) and SIGINT (set a flag, then clean up and
> exit). The main loop uses `pause()`.

**What was generated:**

- `notify_monitor()`: opens `.monitor_pid` with `open()`/`read()`, converts the
  content to a `pid_t` with `atoi()`, calls `kill(mpid, SIGUSR1)`, and calls
  `log_action()` with a success or failure message depending on the return value
  of `kill()`.
- `cmd_remove_district()`: `fork()` + `execl("/bin/rm", "rm", "-rf", district, NULL)`
  + `waitpid()` + check `WIFEXITED`/`WEXITSTATUS` before removing the symlink.
- `monitor_reports.c`: handlers using only async-signal-safe functions (`write()` in
  `handle_sigusr1`, flag set in `handle_sigint`), cleanup in `main` after the
  `pause()` loop exits.

### What I reviewed and changed in Phase 2

- **`(char *)NULL` sentinel**: `execl` requires a `(char *)NULL` cast, not a plain
  `NULL`, since on 64-bit platforms `NULL` may be `0` (int) and a missing cast is
  undefined behaviour for the variadic argument list.
- **`SA_RESTART` only on `SIGUSR1`**: SIGUSR1 should restart slow syscalls; SIGINT
  must *not* restart `pause()` (otherwise the flag check is delayed indefinitely).
- **`fflush(stdout)` after `printf`**: Stdout is line-buffered to a terminal but
  fully-buffered when redirected, which matters once Phase 3 redirects it through a
  pipe. (In Phase 3 I went further and switched the monitor entirely to `write()`.)
- **`errno != ENOENT` guard on `unlink`**: silently ignore a missing symlink.

### What was correct without changes (Phase 2)

- `volatile sig_atomic_t` for the SIGINT flag and `write()` (async-signal-safe) in
  the SIGUSR1 handler.
- Using `sigaction()` instead of the deprecated `signal()`.
- `waitpid()` + `WIFEXITED`/`WEXITSTATUS` to detect a clean `rm` exit.
- Writing the PID as a decimal string (`%d\n`) so `city_manager` can read it back
  with `atoi()`.

### What I wrote myself in Phase 2 (not AI-generated)

- Integration of `notify_monitor()` into the existing `cmd_add()` flow.
- The decision to log monitor notification results through the existing
  `log_action()` function so the log format stays consistent.
- Verification that all Phase 1 commands continued to work after the additions.

### What I learned from Phase 2

1. **Async-signal safety**: Only a restricted set of functions are safe inside a
   signal handler. `printf()` is not; `write()` is.
2. **`exec*` NULL terminator**: The variadic nature of `execl` means the compiler
   cannot catch a missing or wrongly-typed terminator.
3. **`fork`/`exec`/`waitpid` is the standard POSIX way to run external programs
   safely** — avoiding the shell-injection risks of `system()`.

---

## Phase 3 — Pipes and Redirects

### What I asked the AI to generate

For Phase 3 I used the AI to help with three pieces: the structural redesign of the
monitor's output protocol, the `hub_mon` pipe-reading loop, and the per-inspector
workload aggregation in the scorer.

**Prompt given (paraphrased):**

> Modify `monitor_reports` so that on startup it checks whether another monitor is
> already running by reading `.monitor_pid` and probing the PID with `kill(pid, 0)`.
> If yes, write an error message to stdout (which will be a pipe in Phase 3) that
> includes the existing monitor's PID, and exit. Change all messages to be prefixed
> with a tag like `[INFO]`, `[NOTIFY]`, `[ERROR]`, `[SHUTDOWN]` so the parent can
> distinguish message types. Use `write()` to stdout (not `printf`) to avoid
> stdio buffering on the pipe.
>
> Then write `city_hub.c` with an interactive command loop. The `start_monitor`
> command should fork a `hub_mon` process that creates a pipe, forks the monitor
> with its stdout dup2'd onto the pipe's write end, and reads variable-length
> newline-terminated lines from the read end, printing them with a `[hub_mon]`
> prefix. When `read()` returns 0 (the monitor closed stdout), print a "monitor has
> ended" message and exit hub_mon.
>
> The `calculate_scores` command should, for each district passed as an argument,
> create a pipe, fork a child that `dup2()`s the write end onto stdout and `execl`s
> the scorer with the district name, then read each scorer's output and print a
> combined report.
>
> Finally, write `scorer.c` that takes a district name, opens `<district>/reports.dat`,
> reads fixed-size `Report` records, and sums severity per inspector into a small
> linear table. Print one line per inspector.

**What was generated:**

- A modified `monitor_reports.c` with an `existing_monitor_pid()` helper using
  `kill(pid, 0)` + `errno == ESRCH` to detect stale PID files, and tagged output
  written via `write(STDOUT_FILENO, ...)`.
- `hub_mon_run()` in `city_hub.c`: pipe + fork + dup2 to set up the monitor with
  redirected stdout, then a read loop with a partial-line buffer that splits the
  bytestream into lines using `memchr` for the newline, carrying any trailing
  partial line across reads. On EOF, prints the monitor-ended message and exits.
- `spawn_scorer()` and `cmd_calculate_scores()`: per-district pipes, dup2-redirection
  inside the child, fan-in over the read ends in the parent.
- `scorer.c`: linear-probe inspector table (capped at 256), accumulating severity
  and count.

### What I reviewed and changed in Phase 3

- **Stale `.monitor_pid` detection**: The AI's first cut treated `.monitor_pid`
  existing as a sufficient signal that a monitor was running. That misbehaves after
  a hard kill that leaves the file behind. I added the `kill(pid, 0)` probe and the
  `errno == ESRCH` branch so a stale file is silently overwritten.
- **Switched monitor entirely to `write()`**: The Phase 2 monitor used `printf` and
  `fflush`. Once its stdout is a pipe, `printf` output is fully-buffered by stdio,
  so a SIGUSR1 message may be invisible until the next `fflush`. I rewrote every
  output path to call `write()` directly, which is both unbuffered and
  async-signal-safe (so the SIGUSR1 handler does not need a separate code path).
- **Line-reassembly buffer in hub_mon**: The AI's initial loop did one `read()` per
  line and assumed each read returned exactly one newline-terminated message. That
  is wrong — pipes do not preserve message boundaries; a single `read()` can return
  partial lines, multiple lines, or a line plus part of the next. I rewrote the
  loop to accumulate bytes in a `LINE_BUF`-sized buffer, scan for `'\n'` with
  `memchr`, print every complete line, and `memmove` the tail to the start of the
  buffer so the next read continues from the right offset.
- **Buffer overflow guard**: If a single line ever exceeds `LINE_BUF - 1`, the loop
  would deadlock waiting for a newline that never fits. I added a "flush whatever
  we have" branch so the program degrades gracefully.
- **Zombie reaping in city_hub**: The `hub_mon` child outlives any single command.
  I added a `reap_zombies()` call that runs `waitpid(-1, ..., WNOHANG)` before each
  prompt so a finished hub_mon does not become a permanent zombie, and so the user
  sees a notice when hub_mon exits.
- **`SIGPIPE` ignored in city_hub**: Avoids the hub being killed if a scorer dies
  before reading all of its output and a subsequent write hits the closed pipe.
- **Skip non-existent districts**: The AI's initial `calculate_scores` forked a
  scorer for every argument unconditionally. I added a `stat()` check first and
  skip with a warning if the directory is missing — otherwise the user sees a
  misleading `[SCORER ...] error: ...` for every typo.

### What was correct without changes (Phase 3)

- The overall pipe-then-fork-then-dup2 pattern in both `hub_mon_run` and
  `spawn_scorer`. Closing the unused pipe end in each process is essential, and the
  AI got the close-order right (parent closes write end before reading, child
  closes read end before dup2-ing the write end).
- The fan-in pattern in `calculate_scores`: read one scorer to EOF, close its fd,
  `waitpid` it, move to the next. This is simple and correct because scorers are
  short-lived and total output is bounded.
- The structure of the scorer's linear-probe table — adequate for the problem size
  and easy to explain at the presentation. A real implementation would use a hash
  table, but the cap of 256 inspectors is far above realistic input.

### What I wrote myself in Phase 3 (not AI-generated)

- The tokenizer and dispatch loop in `city_hub.c`'s `main` (whitespace splitting,
  the `help` / `quit` builtins).
- Choice of output tags (`[INFO]`, `[NOTIFY]`, `[ERROR]`, `[SHUTDOWN]`,
  `[SCORER <d>]`, `[hub_mon]`, `[city_hub]`) and the convention that every line
  starts with a tag in square brackets so output can be `grep`-filtered.
- Verification that Phase 1 + Phase 2 functionality still works:
  `city_manager --role manager --user alice --add downtown` notifies the monitor
  through hub_mon's pipe and the user sees `[hub_mon] [NOTIFY] New report added.`
- The decision to keep the monitor a standalone binary (as required) rather than
  inlining it into `city_hub` even though that would simplify wiring.

### What I learned from Phase 3

1. **Pipes are byte streams, not message streams.** A single `write()` of one
   newline-terminated message does *not* guarantee the corresponding `read()` returns
   the same bytes in one go. Any consumer that needs message boundaries must
   reassemble them itself — typically by scanning for a delimiter and buffering the
   tail across reads. This is the exact mechanism the spec hints at when it says
   "you will probably have to change the structure of the lines written to stdout
   by the monitor".

2. **`dup2()` ordering matters.** The child must close the pipe end it is *not*
   using before `dup2()`-ing the other end onto stdout. Otherwise the child holds
   both ends open, and the parent's `read()` never sees EOF because the kernel
   still has writers on the pipe.

3. **`kill(pid, 0)` is the standard liveness probe.** It sends no signal but
   performs the same permission and existence checks. `errno == ESRCH` after a
   failure means the PID is unused. This is much more robust than just checking
   whether `.monitor_pid` exists.

4. **Stdio buffering and pipes don't mix without `setvbuf` or `fflush`.** Switching
   the monitor to raw `write()` removed an entire class of bugs in one stroke and
   also made the SIGUSR1 handler trivially safe.

5. **Reaping background children**: without a SIGCHLD handler or periodic
   `waitpid(..., WNOHANG)`, a long-running parent accumulates zombies. For a
   command shell like `city_hub`, the natural place to reap is right before the
   next prompt.
