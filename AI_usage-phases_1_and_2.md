# AI Usage Documentation — Phases 1 and 2

## Tool used

**Claude Sonnet 4.6** (Claude Code, Anthropic) — accessed interactively through the Claude Code CLI during development of `city_manager` and `monitor_reports`.

---

## Phase 1 — `city_manager` (file I/O, permissions, symlinks)

### What I asked the AI to generate

#### Function 1: `parse_condition`

**Prompt given (paraphrased):**

> I have a C struct called `Report` with fields: `id` (int), `inspector` (char[64]), `latitude` (double), `longitude` (double), `category` (char[32]), `severity` (int), `timestamp` (time_t), `description` (char[128]). I need a function with the signature:
> ```c
> int parse_condition(const char *input, char *field, char *op, char *value);
> ```
> that splits a string of the form `field:operator:value` into its three parts. The operator can be `==`, `!=`, `<`, `<=`, `>`, `>=`. Return 0 on success and -1 on malformed input.

**What was generated:**

The AI produced a function that:
1. Uses `strchr` to find the first `:` and copies everything before it into `field`.
2. Uses a second `strchr` starting after the first `:` to find the boundary between operator and value.
3. Copies the operator and the rest of the string into `op` and `value`.

#### Function 2: `match_condition`

**Prompt given (paraphrased):**

> Using the same `Report` struct, generate a function:
> ```c
> int match_condition(Report *r, const char *field, const char *op, const char *value);
> ```
> that returns 1 if the record satisfies the condition and 0 otherwise. Integer fields are `severity` and `timestamp`; string fields are `category` and `inspector`.

**What was generated:**

The AI produced a function that uses `strcmp` on the `field` argument to dispatch to the correct field, converts the `value` string to a numeric type when needed (`atol` / `atoll`), and compares using a chain of `strcmp(op, …)` checks for each operator.

---

### What I reviewed and changed

#### `parse_condition`

- **Bounds check on field length**: The original did not verify that `flen >= 32`. I added an explicit `if (flen == 0 || flen >= 32) return -1;` guard to prevent a potential buffer overrun if an extremely long field token was supplied.
- **Bounds check on operator length**: Same issue — the operator buffer is only 4 bytes. Added `if (olen == 0 || olen >= 4) return -1;`.
- **`value` null-termination**: Changed from an open-ended `strcpy` to `strncpy(value, c2+1, 63); value[63] = '\0';` to prevent overflow.

#### `match_condition`

- **Explicit casts for `timestamp`**: The original compared `r->timestamp` directly with `atoll(value)` without a cast. Since `time_t` can be `long` or `long long` depending on the platform, I added an explicit `(long long)` cast on both sides to avoid silent narrowing.
- **Variable naming**: Renamed the local comparison result variable from a single letter to `c` (for string fields) and `rv`/`lv` (for integer fields) to make the comparisons easier to follow.
- **Structure**: The original used nested ternary chains; I rewrote them as explicit `if / else if` chains because they are easier to reason about line-by-line, which is important for the presentation.

---

### What was correct without changes

- The core logic of splitting on `:` with two `strchr` calls was correct and I kept it unchanged.
- The dispatch on `field` name using `strcmp` was correct.
- The use of `atol` for `severity` and `atoll` for `timestamp` was appropriate — the AI correctly identified which fields are integers.
- The `strcmp`-based ordering for string fields (`<`, `>`, etc.) is correct behaviour for lexicographic comparison, which is a reasonable interpretation for `category` and `inspector`.

---

### What I wrote myself in Phase 1 (not AI-generated)

Everything outside the two functions above was written by me:

- The `Report` struct definition and all `#define` constants.
- The `mode_to_str` permission-bit-to-string conversion helper.
- All six command implementations: `cmd_add`, `cmd_list`, `cmd_view`, `cmd_remove_report`, `cmd_update_threshold`, `cmd_filter`.
- The `ensure_district` district initialisation routine.
- The `update_symlink` / `check_symlink` symbolic-link helpers.
- The `log_action` logging function.
- The `next_report_id` function.
- The argument-parsing logic in `main`.

---

### What I learned from Phase 1

1. **`parse_condition` edge cases**: The AI did not handle empty tokens (`::value`) or oversized tokens. Recognising this required me to think carefully about what inputs the function could receive from the command line.

2. **Alignment with packed structs**: The AI's version of `match_condition` passed `&r->timestamp` directly to a function expecting a `long long *`. When using `__attribute__((packed))` structs, this can cause misaligned-pointer undefined behaviour. I switched to copying the field to a local variable before comparison.

3. **Presentation preparedness**: Because I re-read both functions line-by-line and made deliberate changes, I can now explain every line. I understand exactly what `strchr` returns in the null case and why the size guards are necessary.

4. **What the AI got wrong**: The AI did not think about buffer sizes. Both generated functions assumed the output buffers were large enough. Real defensive C code always validates bounds before writing. This is the most practically useful thing I took from the exercise.

---

## Phase 2 — Processes and Signals

### What I asked the AI to generate

For Phase 2 I used the AI (Claude Code) to help structure the two new components: the `remove_district` command and the `monitor_reports` program. I described the specification requirements and asked the AI to produce a working implementation.

**Prompt given (paraphrased):**

> Add a `remove_district` command to `city_manager`. It must be manager-only, use `fork()` and `execl("/bin/rm", "rm", "-rf", district)` to delete the directory, wait for the child with `waitpid()`, then remove the `active_reports-<district>` symlink with `unlink()`.
>
> Also add a `notify_monitor()` helper that reads `.monitor_pid`, sends `SIGUSR1` to that PID with `kill()`, and logs whether it succeeded or failed to `logged_district`. Call it at the end of `cmd_add()`.
>
> Write a separate `monitor_reports` program that writes its PID to `.monitor_pid` on startup and deletes it on exit. It should use `sigaction()` (not `signal()`) to handle SIGUSR1 (print a message) and SIGINT (set a flag, then clean up and exit). The main loop uses `pause()`.

**What was generated:**

The AI produced:
- `notify_monitor()`: opens `.monitor_pid` with `open()`/`read()`, converts the content to a `pid_t` with `atoi()`, calls `kill(mpid, SIGUSR1)`, and calls `log_action()` with a success or failure message depending on the return value of `kill()`.
- `cmd_remove_district()`: calls `fork()`, and in the child calls `execl("/bin/rm", "rm", "-rf", district, NULL)`; the parent calls `waitpid()` and checks `WIFEXITED` and `WEXITSTATUS` before removing the symlink.
- `monitor_reports.c`: signal handlers that use only async-signal-safe functions (`write()` in `handle_sigusr1`, flag set in `handle_sigint`), with cleanup in the main function after the `pause()` loop exits.

---

### What I reviewed and changed

- **`(char *)NULL` sentinel**: `execl` requires the argument list to be terminated by a `(char *)NULL` cast, not a plain `NULL`. On 64-bit platforms where `NULL` is defined as `0` (an integer), omitting the cast is undefined behaviour. I verified the cast was present.
- **`SA_RESTART` on SIGUSR1 only**: I confirmed that `SA_RESTART` is set for `SIGUSR1` (so that slow system calls are automatically restarted after the handler returns) but not for `SIGINT` (so that `pause()` returns immediately when the flag is set).
- **`fflush(stdout)` after `printf` in monitor**: Since `stdout` is line-buffered when connected to a terminal but fully-buffered when redirected, I added explicit `fflush(stdout)` calls after each `printf` in `monitor_reports.c` to ensure messages appear promptly.
- **`errno != ENOENT` guard on `unlink`**: If the symlink was already missing (e.g., never created), `unlink` would return an error. I added `&& errno != ENOENT` so that missing symlinks are silently ignored rather than printing a spurious error.

---

### What was correct without changes

- The overall structure of the signal handlers — using `volatile sig_atomic_t` for the SIGINT flag and `write()` (async-signal-safe) in the SIGUSR1 handler — was correct.
- Using `sigaction()` instead of the deprecated `signal()` as required by the specification.
- The `waitpid()` logic with `WIFEXITED`/`WEXITSTATUS` to detect whether `rm` exited cleanly.
- Writing the PID as a plain decimal string (`%d\n`) so that `city_manager` can parse it with `atoi()`.

---

### What I wrote myself in Phase 2 (not AI-generated)

- Integration of `notify_monitor()` into the existing `cmd_add()` flow.
- The decision to log monitor notification results through the existing `log_action()` function so that the log format stays consistent.
- Verification that all Phase 1 commands continued to work correctly after the Phase 2 additions.

---

### What I learned from Phase 2

1. **Async-signal safety**: Only a restricted set of functions can be safely called from inside a signal handler. `printf()` is not on that list; `write()` is. The AI generated a handler that used `write()` correctly, but understanding *why* this matters required me to look up the POSIX async-signal-safe function list.

2. **`exec*` NULL terminator**: The variadic nature of `execl` means the compiler cannot catch a missing or wrongly-typed terminator. This is a silent runtime bug that can cause a crash or incorrect argument list on 64-bit systems where pointer width differs from int width.

3. **`fork`/`exec` for external commands**: The pattern of `fork()` + `execl()` + `waitpid()` is the standard POSIX way to run an external program and wait for it to finish. Using it here instead of `system()` gives explicit control over what is executed and avoids the shell-injection risks that come with `system()`.
