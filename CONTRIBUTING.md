Contributing
============

Thank you for contributing to OAC! This document explains how to run the test
suite locally and what to expect from CI when opening a pull request.

Running tests locally
---------------------

After building the project (see `README`), run the integrated tests:

    % make check

On recent systems the test suite should complete quickly and report the
number of passing tests. For example:

    Testsuite summary for oac
    # TOTAL: 17
    # PASS:  17

Continuous Integration
----------------------

The repository includes GitHub Actions workflows that build the project and
run `make -f Makefile.unix check` on push and pull requests. Please make
sure tests pass locally before opening a PR; the CI will run the test
suite and must succeed before merging.

Tips
----

- If a test fails locally, run `make clean` and rebuild before investigating.
- Some tests require a POSIX-like environment (e.g., Linux or macOS).

If you have questions, open an issue or discuss on the PR.
