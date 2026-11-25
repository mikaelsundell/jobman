# Jobman regressions

Preset validation is covered by `jobman.preset_json`, `preset_structure`,
`preset_options`, `preset_dropdown`, `preset_values`, `preset_tasks`,
`preset_variables`, `preset_reuse`, and `preset_examples` (each has the `jobman.`
prefix). They check file/JSON errors, required fields and types, unknown fields,
choice membership, numeric/boolean validation, IDs and dependencies, placeholders,
transactional rereads, built-in presets, README JSON examples, and documentation
of every named MCP tool.

`jobman.preset_errors_ui` verifies aggregated errors for invalid files and
duplicate preset IDs while a valid preset remains selectable. The isolated
`jobman.preset_execution` GUI case executes file and command presets through the
production processor and queue, including three-task chains, task input/output
expansion, exclusivity, optional flags, repeated spaces, and paths with spaces.
See the [preset reference](../README.md#preset-format) for the validated format.

The layout follows Stageviz: native cases registered with CTest, a `jobman_tests`
build target, and `test_app.sh` / `test_app.bat` wrappers around `run_all.py`.
Tests compile the production `Process` implementation and launch a synthetic child;
the process tests do not construct the queue. Queue and monitor regressions use
the real Queue/Job implementations and the actual monitor dialog. No tests
depend on Cadio, third-party Python packages, or QtTest.

Configure the application first with `build_app`, using `THIRDPARTY_DIR` for the
matching Debug or Release dependency prefix. Then run:

```sh
./test_app.sh debug
./test_app.sh release
./test_app.sh all
./test_app.sh release --build-dir build.custom
./test_app.sh release --no-build --filter process_working_directory
```

On Windows, use `test_app.bat` with the same arguments. Python 3 is required for
the runner. It finds CMake/CTest through the build cache, PATH, or `--cmake PATH`.
The runner enables `BUILD_TESTING`, builds the application and native tests, then
runs CTest. A failed configure/build prevents stale binaries from being tested.
Results are written to a new directory under `build.test-results/`, including
configure/build/CTest logs, JUnit XML, and `summary.json`. Override this with
`--output NEW_DIRECTORY`.

For direct CMake/CTest use:

```sh
cmake -S . -B build.release -DBUILD_TESTING=ON
cmake --build build.release --config Release --target Jobman jobman_tests
ctest --test-dir build.release -C Release --output-on-failure --no-tests=error
```

Cases cover large stdout, large stderr, simultaneous streams, split UTF-8 and
embedded NUL bytes, buffer reset, child-only working directories, concurrent
launches in different directories, failed launches, exit codes, and termination.
Each case has a 30-second timeout. macOS Debug tests use `_debug` Qt frameworks;
Release tests clear an inherited debug suffix. Windows CTest entries prepend the
configured Qt binary directory to PATH.

Queue cases verify submission from both the application thread and a worker,
receiver thread affinity, signal payloads and counts, per-job status sequences,
final logs and artifacts, dependency ordering/failure propagation, priorities,
exclusive execution, batching, stop/start, restart, and removal. The offscreen
monitor test exercises its real submission, status, priority, log, button-state,
and removal slots, live execution durations, and selected/failure metrics.
`jobman.job_timestamps` checks timestamp resets and direct status slots that read
Job properties; queued signal counts and sequences remain covered by queue cases.

For comparisons with an earlier revision, compile `queue_regression.cpp` against
that revision's `queue.cpp`, `job.cpp`, `process.cpp`, and matching headers, linking
Qt Core, Concurrent, and Widgets. Use the same fixture executable for both builds:

```sh
python3 CTest/compare_queue.py \
    --baseline path/to/baseline-regressions \
    --current build.release/CTest/jobman_queue_regressions \
    --child build.release/CTest/jobman_process_child \
    --baseline-revision REVISION --repeat 3 \
    --output build.test-results/queue-comparison.json
```

Use `--configuration debug` when comparing Debug builds. Compare builds using the
same Qt configuration and dependency prefix; on Windows put that prefix's `bin`
directory on PATH. Reports compare deterministic results and signal traces;
random UUIDs, temporary paths, timestamps, PIDs, elapsed times, and the ordering
between independent jobs are excluded. Dependency/exclusive execution order is
asserted separately. Existing behaviors (including dependency failure states and
removal notifications) are preserved as the comparison baseline.

File-drop GUI cases reproduce rejected-extension and empty-directory stalls,
mixed drops, declined directory confirmation, and successful subsequent drops.
Application-level GUI tests force-include `testsettings.h` so every production
settings caller uses a per-process test organization. This also isolates native
macOS preferences, whose explicit organization constructor ignores the test path
override. Test values are cleared at shutdown. The MCP case tests the real local
HTTP endpoint, job execution, widget inspection/actions/screenshots, dialog
recovery, and the footer checkbox; see [MCP.md](../docs/MCP.md).

`jobman.ui_theme` checks the shared Stageviz-style UI service across standalone
views, including semantic resources, authored/generated disabled states, DPI-aware
pixmap caching, theme refresh, and embedded stylesheet loading. The monitor test
also checks the actual context menu offers Input file then Output file, without
launching the system file browser. See [UI.md](../docs/UI.md).

`jobman.mcp_large` scans 2,001 files in nested folders, checks decline/retry and
acceptance, enumerates every resulting job across pages, removes a cursor job,
and submits 1,001 explicit paths. It uses a missing fixture executable to verify
launch failures quickly; the main MCP and queue cases separately verify real
executables, output files, dependencies, restart, and captured logs. MCP
persistence tests include occupied-port startup and manual retry failures in
isolated processes. Monitor tests deliver delayed status payloads to ensure
current model state wins.
