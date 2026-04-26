# AI Usage Documentation — Phase 1

## Tool used

**Claude Sonnet 4.6** (Claude Code, Anthropic) — accessed interactively through the Claude Code CLI during development of `city_manager`.

---

## What I asked the AI to generate

### Function 1: `parse_condition`

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

### Function 2: `match_condition`

**Prompt given (paraphrased):**

> Using the same `Report` struct, generate a function:
> ```c
> int match_condition(Report *r, const char *field, const char *op, const char *value);
> ```
> that returns 1 if the record satisfies the condition and 0 otherwise. Integer fields are `severity` and `timestamp`; string fields are `category` and `inspector`.

**What was generated:**

The AI produced a function that uses `strcmp` on the `field` argument to dispatch to the correct field, converts the `value` string to a numeric type when needed (`atol` / `atoll`), and compares using a chain of `strcmp(op, …)` checks for each operator.

---

## What I reviewed and changed

### `parse_condition`

- **Bounds check on field length**: The original did not verify that `flen >= 32`. I added an explicit `if (flen == 0 || flen >= 32) return -1;` guard to prevent a potential buffer overrun if an extremely long field token was supplied.
- **Bounds check on operator length**: Same issue — the operator buffer is only 4 bytes. Added `if (olen == 0 || olen >= 4) return -1;`.
- **`value` null-termination**: Changed from an open-ended `strcpy` to `strncpy(value, c2+1, 63); value[63] = '\0';` to prevent overflow.

### `match_condition`

- **Explicit casts for `timestamp`**: The original compared `r->timestamp` directly with `atoll(value)` without a cast. Since `time_t` can be `long` or `long long` depending on the platform, I added an explicit `(long long)` cast on both sides to avoid silent narrowing.
- **Variable naming**: Renamed the local comparison result variable from a single letter to `c` (for string fields) and `rv`/`lv` (for integer fields) to make the comparisons easier to follow.
- **Structure**: The original used nested ternary chains; I rewrote them as explicit `if / else if` chains because they are easier to reason about line-by-line, which is important for the presentation.

---

## What was correct without changes

- The core logic of splitting on `:` with two `strchr` calls was correct and I kept it unchanged.
- The dispatch on `field` name using `strcmp` was correct.
- The use of `atol` for `severity` and `atoll` for `timestamp` was appropriate — the AI correctly identified which fields are integers.
- The `strcmp`-based ordering for string fields (`<`, `>`, etc.) is correct behaviour for lexicographic comparison, which is a reasonable interpretation for `category` and `inspector`.

---

## What I wrote myself (not AI-generated)

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

## What I learned

1. **`parse_condition` edge cases**: The AI did not handle empty tokens (`::value`) or oversized tokens. Recognising this required me to think carefully about what inputs the function could receive from the command line.

2. **Alignment with packed structs**: The AI's version of `match_condition` passed `&r->timestamp` directly to a function expecting a `long long *`. When using `__attribute__((packed))` structs, this can cause misaligned-pointer undefined behaviour. I switched to copying the field to a local variable before comparison.

3. **Presentation preparedness**: Because I re-read both functions line-by-line and made deliberate changes, I can now explain every line. I understand exactly what `strchr` returns in the null case and why the size guards are necessary.

4. **What the AI got wrong**: The AI did not think about buffer sizes. Both generated functions assumed the output buffers were large enough. Real defensive C code always validates bounds before writing. This is the most practically useful thing I took from the exercise.