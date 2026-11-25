# Jobman development guide

- Before changing preset parsing or expansion, read the preset reference in
  [README.md](README.md#preset-format). Keep validation, examples, and MCP-facing
  behavior consistent. Run the `jobman.preset_` cases, including documented
  examples and file/command execution; do not change queue scheduling to accept
  malformed presets.
- Before using or changing Jobman's MCP tools, read [docs/MCP.md](docs/MCP.md).
  Follow its job-submission workflow and asynchronous completion rules. Update
  the guide when tool names, arguments, or behavior change.
- Preserve the existing queue and signal/slot behavior. Submit files through the
  application's preset/file-drop path and control jobs through the public queue
  API. Verify relevant queue and monitor regressions when changing these paths.
- Drive MCP requests from an external controller: requests run on Jobman's Qt
  application thread. A synchronous request from that same thread can deadlock.
- Read [docs/UI.md](docs/UI.md) before changing view styling. Keep interfaces in
  Designer files and assign semantic icons through `::ui()`.
- Format changed C++ headers and sources with the repository's `.clang-format`.
  Run the relevant tests through `test_app.sh` or `test_app.bat`; GUI tests must
  retain their isolated test settings rather than using the user's preferences.
