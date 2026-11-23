# Reviewing OAC Changes

This document lists what should be taken into account when evaluating a
change to liboac. It does not automate a decision or dictate what is
allowed. It is meant for the author of a change, for the reviewer, and for
any agent helping on either side.

[CODEBASE.md](CODEBASE.md) describes how the code is organized, the
assumptions it relies on, and the build and test commands referred to below.

## Principles

- The codebase must stay in a state where any codec engineer can understand
  it. Readability and maintainability matter as much as the result.
- Every cost (complexity, memory, code size) needs a benefit that justifies
  it.
- A change should say what it does and what it affects, so the reviewer can
  check that the right evaluation was done.

## What to check

### Correctness and portability

- The change works in both floating-point and fixed-point builds. At a
  minimum, `make check` passes with `--enable-assertions` for:
  - the default float build,
  - `--enable-fixed-point`,
  - `--enable-fixed-point --enable-fixed-point-debug`.
- Depending on what the change touches, also test:
  - `--enable-check-asm` when SIMD code or a function with a SIMD version
    changes,
  - `--disable-float-api` when the API or the analysis code changes,
  - the DNN options (`--enable-deep-plc`, `--enable-dred`, `--enable-osce`)
    when `dnn/` or its callers change,
  - sanitizers and `tests/oac_build_test.sh` for larger or riskier changes.
- The neural network features are not supported in fixed-point yet.
- A fixed-point build should not need float operations, with or without
  `--disable-float-api`. See [Temporary limitations](#temporary-limitations)
  for new features.
- The code stays C90-clean (it compiles with `-std=c90`) and also compiles
  as C++. See the assumptions in CODEBASE.md, including the goal of not
  assuming a 32-bit `int`.
- No undefined behaviour.
- No new compiler warnings in any configuration that is tested, including
  the DNN options.
- All tests pass. A failure is checked against the base commit before it is
  attributed to the change. Problems that were already there are not held
  against the change, but they are worth reporting separately.

### Temporary limitations

A new feature can be merged with known shortcomings, for example float
operations in a fixed-point build, a missing resampler, or names that still
need a prefix. This is acceptable as long as the commit message or a comment
in the code lists them. The review checks that the list is complete rather
than objecting to each item.

### Bitstream

- While OAC is in development, a change may break bitstream compatibility,
  but it must do so on purpose and the commit message must say so.
- A bitstream change that is not announced is a red flag.
- A change that only affects the encoder should say that it leaves the
  bitstream alone.
- A change that claims not to affect the output at all, such as a
  refactoring, should produce the same packets as the base commit. The
  review says whether that was checked.

### Existing configurations

- A change that alters the output or the cost of existing configurations at
  their default settings must say so, even when the bitstream format does
  not change. Examples are a new decision in the encoder that is on by
  default, or a different choice of bandwidth or mode at a given bitrate.
- The review should look for such changes, since they are easy to miss when
  they come with a new feature.

### Decoder robustness and encoder/decoder sync

- The decoder must be safe on arbitrary and malicious packets: no
  out-of-bounds access, no undefined behaviour, no unbounded loop or
  allocation. New parsing code deserves fuzzing or adversarial tests.
- The encoder's view of the decoder state must stay exact. The final range
  check and `RESYNTH` exist to verify this.
- What gets decoded must not depend on whether the build is float or
  fixed-point in a way that breaks that sync.

### Cost

There are no fixed thresholds. Any increase should be identified (where it
comes from, roughly how large, and whether it affects the encoder, the
decoder or both) and justified by what it brings.

- Complexity. Decoder cost and worst-case cost matter more than encoder
  cost and average cost. Encoder increases should scale with the complexity
  setting where practical.
- Memory: stack, state and tables. Large temporaries follow the stack
  allocation rule in CODEBASE.md.
- Code size.
- Large files are not added to git. Models and other large binary data go
  through the existing tarball mechanism (`tar_list.txt`, with the SHA-256
  in `autogen.sh`).

### Readability and maintainability

- The change keeps the structure of the existing code.
- Duplicated functionality is avoided. The review should look for it and
  suggest how to remove it, including deleting existing code that the change
  makes useless.
- Comments are succinct and clear.
- Formatting matches the reformatted codebase (`oac.cfg`). Small deviations
  in details such as operator spacing are fine when they help readability.
- Naming: public symbols use `oac_`/`OAC_` and internal symbols with
  external linkage use `oaci_`. Static functions do not need the prefix, and
  some modules have their own. Macros and types in a header that is
  included from another directory have a module prefix.
- SIMD: every optimized function has a C reference, is covered by
  `--enable-check-asm`, and matches the C output (bit-exact in fixed-point).
- Build systems: added or removed source files are updated in the `*.mk`
  lists, and new options are added to Autotools, CMake and Meson together.
- Public API: changes to `include/*.h` are called out, documented in the
  Doxygen comments, and covered by a test that `make check` runs. Argument
  validation and error paths belong in `tests/test_oac_api.c`. The
  behaviour of a feature can have its own test.
- Tests: new functionality comes with a test that `make check` runs. Bug
  fixes add a regression test when practical.
- Copyright headers are correct for the situation: new files have one, and
  code derived from another file keeps the notices it came with.
- CODEBASE.md is updated in the same patch when a change adds or removes a
  component, or otherwise purposely contradicts what that document says.

### Commits

- A commit does one thing. A commit that fixes two independent things should
  be split, unless it fixes the same trivial issue in several places.
- A commit does not introduce a regression, even one fixed by a later
  commit, unless there is really no way around it.
- A commit that adds functionality can be large, and includes whatever the
  feature needs: refactoring of existing code, changes to the bitstream,
  build system and tests. The review should not ask for such a commit to be
  split. Splitting it into "add unused code" and "use the new code" is
  pointless.
- Reformatting, if really necessary, goes in separate commits that do not
  change behaviour.
- Fixes made during review are folded into the commit they fix before
  merging.
- Commit messages are succinct and clear:
  - a short subject line (about 70 characters at most) in the imperative,
    then a blank line and a body that explains why, not just what. A
    trivial commit needs no body.
  - they state any bitstream break, any change to existing configurations,
    any effect on quality and how it was evaluated, any known cost
    increase, and any temporary limitation that is not listed in the code.
  - a bug fix names the commit that introduced the bug when it is known.

### Quality

Any change that can affect audio quality should be pointed out, along with
the kind of evaluation it calls for:

- Some changes can be evaluated with objective metrics, for example better
  entropy coding or a better search.
- Others need human listening, for example noise shaping tuning, spreading
  changes or transient handling.

If the commit describes the testing that was done, check that it suits the
kind of change. If it does not, say which kind of evaluation the change
needs so the reviewer can ask for it. Nothing in `make check` measures
quality.

## Reporting

A review, especially one written by an agent, should:

- Classify the change: bitstream change, encoder-only, refactoring, build,
  or fix; and, if quality can be affected, whether objective metrics are
  enough or listening is needed.
- List findings with file and line.
- Separate what was verified by building or running from what was only
  read.
- List problems that already exist on the base commit separately from the
  findings about the change.
- Say what was not checked.
- Give no approve or reject verdict. The decision belongs to the reviewer.
