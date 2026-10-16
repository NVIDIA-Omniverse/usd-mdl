<!-- SPDX-License-Identifier: Apache-2.0 -->

# Diagnostics

usdMdl reports runtime problems through OpenUSD `Tf` diagnostics. Native
applications can receive them through a `TfDiagnosticMgr::Delegate`; applications
using the Python plugin receive the normal USD warning output. SDK library-load
failures include the attempted library path, platform loader details when
available, and the library/header versions for a version mismatch.

## MDL module messages

After each actual SDK `load_module()` call, usdMdl reads its execution context.
It forwards warning, error and fatal messages as `Tf` warnings, including messages
from successful loads. This makes warnings about unresolved resources visible
without requiring a separate SDK logger.

| SDK severity | Default behavior | With `TF_DEBUG=USD_MDL_DIAGNOSTICS` |
| --- | --- | --- |
| Fatal, error, warning | USD warning with the original SDK severity and details | Same USD warning |
| Info, verbose, debug | Quiet | Debug output with the original SDK details |

Forwarding reports what the SDK returned; it does not change the load result,
registration behavior or resource resolution. An SDK error remains a failed load
when the SDK returns failure. A warning does not turn a successful load into an
error. Context messages are deliberately forwarded as `TF_WARN`, rather than
`TF_ERROR` or `TF_RUNTIME_ERROR`, so this reporting does not introduce Python
exceptions or add errors to the caller's `TfErrorMark`.

The module name, severity, message kind, SDK code, original message text and
recursive notes are retained. Source filenames and line/column information are
preserved when the SDK includes them in its message text; usdMdl does not parse
that text into separate location fields. The USD diagnostic's own source location
identifies the forwarding code, rather than the MDL source file.

Messages are delivered after loading-batch bookkeeping and outside the module
loader's locks. Cached module lookups do not replay earlier diagnostics. There is
no global message deduplication: a new SDK load attempt can report the same
problem again. Concurrent loads can interleave diagnostics, so associate messages
with their module names rather than their position in the output.

## Structured warning details

Each forwarded SDK warning carries a `VtDictionary` in its `TfDiagnosticInfo`.
A native diagnostic delegate can obtain it from the `TfWarning`:

```c++
#include <pxr/base/tf/warning.h>
#include <pxr/base/vt/dictionary.h>

// Inside TfDiagnosticMgr::Delegate::IssueWarning().
const pxr::VtDictionary* details = warning.GetInfo<pxr::VtDictionary>();
```

The pointer can be null for warnings that do not carry this payload. Copy any
needed data before the delegate call returns.

| Key | Held C++ type | Meaning |
| --- | --- | --- |
| `module` | `std::string` | Qualified module name passed to the SDK load call |
| `severity` | `int` | Original `mi::base::Message_severity` value |
| `kind` | `int` | Original `mi::neuraylib::IMessage::Kind` value |
| `code` | `int` | Original SDK message code |
| `message` | `std::string` | Original SDK message text |
| `notes` | `std::vector<VtValue>` | Notes in SDK order, each holding another dictionary with these keys |

Each note retains its own severity, kind, code, message and nested notes, as well
as the owning module name. Notes remain attached to their parent message. The
SDK can also place a copy of a compiler note in the top-level message list;
with debug output enabled, usdMdl prints that lower-severity copy only through
its owning parent, rather than printing it again separately.

The structured details are a native C++ interface; the Python API does not
expose the payload dictionary.

The SDK severity values are `0` for fatal, `1` for error, `2` for warning, `3` for
info, `4` for verbose and `5` for debug. Interpret message kinds using the
`IMessage::Kind` enumeration from the SDK headers used by the application.

## Rejected resource messages

usdMdl also reports logical resource paths its resolver rejects, such as
`../../../texture.png` used by `::d3::nested::probe`, or a path containing a
non-leading `.` or `..` component. A rejection warning includes the original
path, owning module and source file, line and column when provided by the SDK,
and the reason for rejection. Printed warnings and debug output show the
original resource spelling in place of the private identity. The structured
`message` field still retains the raw SDK message when the warning comes from
the SDK; an optional `displayMessage` field contains the text used for display.

The module can still load, but the rejected reference cannot select a different
resource during the SDK's later lookup. Its Sdr asset default remains empty.
Explicit, valid references to the same root or owner-local filename continue to
work. See [rejected resources](ar-resolver.md#rejected-resources) for the resolver
contract.

These are plugin rejection records, separate from the SDK's execution-context
message list. For loads made through usdMdl, records belong to that load's
execution context and are reported after loading-batch completion, outside the
loader's locks. Concurrent loads keep their records separate. Cache hits do not
replay earlier warnings. Repeated callbacks for the same owner, path and source
position produce one collector record for that load. This fills gaps in SDK
reporting; it does not suppress independent SDK warning parents. Imported defaults
can cause separate SDK warnings for the provider and consumer, and each is
forwarded once with its original SDK details.

Rejection warnings use the same `VtDictionary` payload as forwarded SDK messages
and add these fields:

| Key | Held C++ type | Meaning |
| --- | --- | --- |
| `resourcePath` | `std::string` | Original resource spelling supplied to the resolver |
| `ownerModule` | `std::string` | Module that authored the resource reference, which can be an imported dependency |
| `ownerFile` | `std::string` | Owner's source filename supplied to the resolver |
| `line`, `column` | `int` | Source position supplied to the resolver; zero when unavailable |
| `reason` | `std::string` | Why the logical reference was rejected |
| `displayMessage` | `std::string`, optional | SDK message with the private identity replaced for display |
| `origin` | `std::string`, optional | `"resolver"` for a warning supplied by the plugin's rejection collector |
| `rejectedResources` | `std::vector<VtValue>`, optional | Rejections mentioned in this SDK message, each holding a dictionary of the path, owner, position and reason fields above |

The scalar rejection fields describe the first reference in a message. An SDK
message containing several preserved references retains each reference's details
in `rejectedResources`. Attached notes can carry these same fields and collections.

When the SDK already reports the preserved rejection as a warning, the plugin
adds the original details to that warning. Otherwise, the collector supplies a
warning with severity `warning`, kind `MSG_INTEGRATION` and code `0`. The payload's
`module` identifies the requested load; `ownerModule` identifies the source of
the resource reference. The raw SDK payload can contain a private identity;
it is not an authored resource path and should not be used for asset lookup.

## Application SDK loggers

usdMdl does not install or replace the application's MDL SDK receiving logger.
Native applications can continue using
`ILogging_configuration::set_receiving_logger()` on the same Neuray instance to
observe SDK activity outside usdMdl's module-load execution contexts. See
[native integration](native-integration.md#host-configuration-ownership) for the
host instance contract. The Python binding does not expose the Neuray logger APIs.

Consuming both the SDK logger and USD diagnostics can report the same underlying
problem twice. SDK logger messages are independent of usdMdl's display formatting
and can contain a private rejected-resource identity. The SDK logger can be called
concurrently from multiple threads.
A `TfDiagnosticMgr::Delegate` must not issue another `TF_*` diagnostic from its
callback; keep forwarding paths from cycling between the SDK and USD loggers.

Rejected logical paths retain the original reference for reporting, but ordinary
failed resource lookups can still lose their path or parameter association in
the SDK. An invalid resource and an intentionally empty default can produce the
same empty Sdr asset value. See [resource limitations](ar-resolver.md#limitations).
