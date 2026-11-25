# Jobman MCP server

Jobman exposes its running desktop application over a local MCP Streamable HTTP
endpoint, following Stageviz's stateless JSON request/response transport. It binds
only to `127.0.0.1` and is **off by default**. Jobman's default port is **37652**;
Stageviz uses 37651, so both applications can run together.

## Start and stop

Check **MCP** beside **Max jobs** in the main window. Uncheck it to stop the
server. The checkbox follows actual server state and its tooltip shows the
listening endpoint. A startup failure leaves it unchecked and displays the error.
The switch is saved immediately and restored on the next launch. The selected
port is remembered as well; port `0` requests a new available port each launch.
Explicit `--mcp --mcp-port` startup takes precedence over the remembered port.
Closing the application preserves the switch; uncheck it to disable automatic
startup. A failed startup clears the saved switch to match the unchecked UI, avoiding
repeated failures on every launch. Resolve the error and check MCP to retry.

The Help menu has a separator below **Open Github Issues**, followed by
**MCP Server**, which opens this guide on GitHub. Use the main-window checkbox
to start or stop the server.

Command-line startup is also available:

```sh
Jobman --mcp
Jobman --mcp --mcp-port 37652
Jobman --mcp --mcp-port 0
```

Port `0` chooses an available port. Jobman prints the actual endpoint when the
server starts. On macOS, the executable is inside `Jobman.app/Contents/MacOS/`.

## Connect

With Jobman open and MCP enabled, configure a local HTTP MCP client with:

```text
http://127.0.0.1:37652/mcp
```

As in Stageviz, local clients can connect through their MCP configuration:

```sh
codex mcp add jobman --url http://127.0.0.1:37652/mcp
claude mcp add --transport http --scope user jobman http://127.0.0.1:37652/mcp
```

Use a new client session after adding the server. The endpoint runs on the same
computer as Jobman; it is not a cloud connector or a public network listener.
Enabled clients can inspect jobs and operate the application through the tools
below. Disable the checkbox when that access is no longer needed.

## Jobs and application tools

| Tool | Purpose |
| --- | --- |
| `jobman_get_status` | Active preset/output directory, scan/submission readiness, queue activity, job count, and max jobs. |
| `jobman_list_presets` | Loaded preset ids, names, filters, types, and selection. |
| `jobman_select_preset` | Select a loaded preset by `id` using the normal UI signal path. |
| `jobman_set_output_directory` | Set an existing absolute output directory for subsequent submissions. |
| `jobman_submit_files` | Submit absolute local file/directory `paths` through the normal file-drop path, with no file-count cap. |
| `jobman_list_jobs` | Current jobs, UUIDs, states, commands, outputs, priorities, PIDs, and dependencies. `limit` defaults to 100 with no job-count cap. Follow `nextCursor` using the `cursor` argument for further pages. |
| `jobman_get_job` | One `uuid` and its log tail. `logLimit` defaults to 65536 characters, maximum 262144; zero omits the log. |
| `jobman_control_job` | Use the existing queue API to `start`, `stop`, `restart`, or `remove` a `uuid`. Invalid states are rejected. Removal includes dependent jobs, matching the monitor. |
| `jobman_get_dialog` | Active application modal dialog, text, object path, and available buttons. |
| `jobman_respond_dialog` | `close` a message or answer a confirmation with `yes`/`no`. |
| `jobman_save_preferences` | Save current preferences through the existing Save implementation. |

File submission is asynchronous: `acceptedForScanning` does not mean that every
file passed the preset filter or that conversion completed. Poll readiness/jobs,
inspect any dialog, and wait for the desired job's terminal state and queue
completion before reading its final log/output. Rejected files remain rejected;
the drop area returns to ready and later submissions can proceed. Directory
submissions can show the same confirmation dialog as a manual drop.

Jobs submitted manually or through MCP share the same queue and monitor. Read-only
tools observe the existing job objects; MCP does not maintain another scheduler.

Only successfully validated presets appear in `jobman_list_presets`. Invalid
definitions are skipped; the application's preset-loading report includes their
filenames, fields, and errors, and can be read with `jobman_get_dialog`. Duplicate
preset IDs in a folder are reported and skipped too. See the
[preset reference](../README.md#preset-format) for required fields, dropdown
membership, ranges, task dependencies, and supported placeholders. Correct the
JSON file and use the normal preset refresh action before retrying; submitting
files does not bypass preset validation.

### Adding jobs from an agent

Read this guide before operating Jobman. Connect to the running application and
use `tools/list` to confirm the tools available in that build. Use the named job
tools for submissions; Qt tools are for inspecting or operating controls when
the named tools do not cover the task.

1. Call `jobman_get_status` and `jobman_list_presets`. Check the current settings
   and choose a loaded file preset by its returned `id`.
2. Call `jobman_select_preset` with `{"id": "<returned preset id>"}` and, if
   requested, `jobman_set_output_directory` with an existing absolute directory.
   These settings apply to subsequent submissions, as they do in the UI.
3. Record existing job UUIDs with `jobman_list_jobs`, then call
   `jobman_submit_files` with `{"paths": ["/absolute/path/to/input.png"]}`.
   Multiple paths and directories are supported. Preset filtering, task
   expansion, dependencies, and output naming follow normal file-drop behavior.
4. The response `{"acceptedForScanning": true}` confirms only that scanning
   started. Poll `jobman_get_status`, `jobman_list_jobs`, and `jobman_get_dialog`.
   If a confirmation appears, inspect it and answer through
   `jobman_respond_dialog` according to the user's request. Close a skipped-file
   message before submitting more files. Do not resubmit while a scan is pending.
5. Identify the newly added UUIDs and poll `jobman_get_job` until they reach a
   terminal state and `jobman_get_status.processing` is false. Then inspect each
   final log and expected output. A rejected file can produce no job; report the
   rejection rather than claiming that it was added or completed.

For large queues, `jobman_list_jobs` returns `jobs`, `total`, `truncated`, and
`nextCursor`. Start without a cursor, then pass each nonempty `nextCursor` as the
next call's `cursor`. Stop when `nextCursor` is empty. `limit` is a positive
integer (default 100), with no application-imposed job-count maximum. A cursor
continues after its UUID even if that job has since been removed. Listings use
UUID order; they are not immutable snapshots. Wait for submission to finish
before collecting a batch, and restart enumeration after new submissions because
new UUIDs can sort before the current cursor.

```json
{"name": "jobman_list_jobs", "arguments": {"limit": 250}}
{"name": "jobman_list_jobs", "arguments": {"limit": 250, "cursor": "<previous nextCursor>"}}
```

Use returned UUIDs with `jobman_get_job` and `jobman_control_job` for later
inspection or control. `jobman_submit_files` accepts any nonempty path list;
folders may contain thousands of files. Normal HTTP request-size bounds still
apply to explicit path lists, so submit a folder or split the path list across
requests when necessary. There is no total queue-job limit imposed by MCP.
Submitting jobs uses the currently selected preset and its options, including
overwrite and copy-original settings. Changing preferences on disk is a separate
operation (`jobman_save_preferences`); submission does not require it.

## Qt debugging and screenshots

MCP requests and Qt operations run on Jobman's application thread, as in Stageviz.
Drive them from an external client/controller so the application's event loop
remains available to handle the requests.

Jobman uses named Qt tools instead of an embedded Python interpreter. External
Python scripts can combine these tools to inspect and automate the actual UI:

- `jobman_get_qt_state`: bounded QObject/widget tree with object names, class names,
  stable object paths, visibility/enabled state, text, button state, combo entries,
  stack indexes, progress, and tree rows. `path` defaults to `Jobman`; `depth`
  defaults to 4 (maximum 12), and `limit` defaults to 500 (maximum 1000).
- `jobman_qt_action`: operate an exact returned `path`. Supported `action` values
  are `click`, `trigger`, `set_text`, `set_checked`, `select_index`, `select_row`,
  `show`, and `hide`. Supply `value` where needed. Tree selection takes a row-index
  array, such as `[0, 1]` for the second child of the first top-level row.
- `jobman_capture_widget`: capture an actual QWidget by `path` and return PNG MCP
  image content. The default target is the main window. No screenshot file is
  written by Jobman.

Button clicks and QAction triggers are queued so a modal dialog can open without
holding the triggering HTTP response. Poll the UI/dialog state before the next
dependent operation. An active modal dialog restricts Qt actions to that dialog.
Other setters return after applying the Qt property and its normal signals.
Disabling MCP through its own control closes the endpoint and its active clients.

Do not assume object paths: obtain them from `jobman_get_qt_state`. For modal
dialogs, `jobman_get_dialog` gives their root path. Unnamed internal objects are
omitted from the inspection tree, and missing/ambiguous paths are rejected.

### External Python example

The following uses only Python's standard library. Prefer named job tools when
they are sufficient, and use Qt tools for UI debugging and screenshots.

```python
import base64
import json
from pathlib import Path
from urllib.request import Request, urlopen

endpoint = "http://127.0.0.1:37652/mcp"
request_id = 0

def rpc(method, params):
    global request_id
    request_id += 1
    body = json.dumps({"jsonrpc": "2.0", "id": request_id,
                       "method": method, "params": params}).encode()
    request = Request(endpoint, data=body, headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=15) as response:
        value = json.load(response)
    if "error" in value:
        raise RuntimeError(value["error"])
    return value["result"]

def call(name, arguments=None):
    result = rpc("tools/call", {"name": name, "arguments": arguments or {}})
    if result.get("isError"):
        raise RuntimeError(result["content"])
    return result

rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                   "clientInfo": {"name": "Jobman debug script", "version": "1"}})
print(rpc("tools/list", {}))
print(call("jobman_get_status"))
print(call("jobman_get_qt_state", {"path": "Jobman", "depth": 6}))
# Select a loaded preset/output directory first, then submit real input paths:
# call("jobman_submit_files", {"paths": ["/absolute/path/to/input.png"]})

image = call("jobman_capture_widget")["content"][0]
Path("jobman.png").write_bytes(base64.b64decode(image["data"]))
```

## Transport and tests

The server implements `initialize`, `ping`, `tools/list`, `tools/call`, and MCP
notifications over POST `/mcp`, returning JSON or HTTP 202 for notifications.
Host/Origin checks match the loopback endpoint. Headers and request bodies are
bounded, duplicate framing headers are rejected, and idle connections expire.
This is a stateless endpoint: no session id or server event stream is required.

Run the GUI/MCP regression with `test_app.sh` or `test_app.bat` using
`--filter jobman.mcp`. It drives HTTP from a separate client thread, exercises
normal job submission/results, Qt inspection/actions/screenshots, dialog recovery,
checkbox state, lifecycle, and invalid HTTP/JSON-RPC requests. Application GUI
tests use an isolated per-process settings organization and clean up their values.
