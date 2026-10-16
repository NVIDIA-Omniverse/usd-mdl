<!-- SPDX-License-Identifier: Apache-2.0 -->

# usdMdl documentation

Start with the [project overview](../README.md). Each topic has one reference
page; related guides link to it rather than repeating its rules.

| Task | Reference and scope |
| --- | --- |
| Review a release | [Changelog](../CHANGELOG.md): alpha scope, dependencies, integration requirements and limitations |
| Build or test the plugin | [Build](build.md): dependencies, platform commands, troubleshooting |
| Configure an application | [Environment](environment-variables.md): runtime paths, precedence, settings |
| Run a complete example | [Examples](examples.md): inspect a bound material in Python or C++ |
| Author USD shaders | [Sub-identifiers](sub-identifiers.md): module references, signatures, resource parameter overrides, legacy input |
| Use Python or inspect node metadata | [Python API](python-api.md): registration, tokens, node metadata, UI hints |
| Bind a standard USD shader interface to MDL | [Shader definitions](shader-definitions.md): Preview Surface resource, registration, port bindings, renderer conversion |
| Author node or property metadata | [Metadata](metadata.md): explicit `SDR__` annotation scope, Sdr fields, conflicts, MDL built-in mappings and UI hints |
| Interpret property values | [Types and defaults](type-mapping.md): storage, shape, expressions |
| Check whether MDL ports can connect | [Connection checks](type-mapping.md#client-side-mdl-connection-checks): native type identity, arrays, modifiers and local helper limits |
| Embed in a native application | [Native application integration](native-integration.md): CMake package, C++ API, SDK ownership, loading and shutdown |
| Diagnose SDK loading or module warnings | [Diagnostics](diagnostics.md): warning forwarding, structured SDK messages, debug output and application loggers |
| Understand asset loading | [Ar resolver](ar-resolver.md): module/resource identity, embedded modules, formats, limitations |
| Change the implementation | [Architecture](architecture.md): component boundaries, control flow, diagnostics; [contributor guide](../CONTRIBUTING.md): source/test map and workflow |
| Navigate or extend tests | [Test guide](../tests/README.md): fixture layout, runners, and process configurations |

