<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tests

The [contributor guide](../CONTRIBUTING.md) maps implementation areas to tests
and defines validation expectations. The [build guide](../docs/build.md#tests)
explains configuration and running the suite. This page describes its layout
and process configurations; CTest registration in
[src/CMakeLists.txt](../src/CMakeLists.txt) is the source of truth for case names.

## Layout

| Location | Purpose |
| --- | --- |
| `tests/*.cpp` | Native API, resolver, discovery, ownership, and lifecycle tests |
| `tests/*.py` | Public-API lookup drivers, case generation, and example-output checks |
| `testMdlSdrPlugins.testenv/` | Shared MDL/resource fixtures and Python tests for types, annotations, expressions, discovery, and bindings |
| `testMdlSdrPlugins.testenv/lib/mdl_test_base.py` | Shared UsdShade lookup and value/metadata assertions |
| Other `*.testenv/` directories | Fixtures for the associated test family, such as embedded modules and sub-identifiers |
| `testMdlPreviewSurfaceAuto.testenv/` | Original canonical-name MDL fixture and fresh-process opt-in discovery tests |
| `testInstalledPackage.testenv/` | Downstream CMake/Python consumer checks for a staged package; see [CI and staged packages](#ci-and-staged-packages) |
| `../examples/` | Scene-inspection programs and fixtures checked by `testInspectMdlMaterial.py` |

The [Resource fixture guide](testMdlSdrPlugins.testenv/mdl/resources/README.md)
describes valid and invalid measurements, light profiles and texture fixtures.
[Resource licensing](../LICENSE_RESOURCES.md) covers the CC0 data files; test
code and MDL modules remain Apache-2.0.

## Running and isolation

List the cases in your configured build, or inspect one family's assertions:

```sh
ctest --test-dir <build> -C Release -N
ctest --test-dir <build> -C Release -V -R '^testMdlSubIdentifiers_' --no-tests=error
```

A CTest entry may run many Python unittest cases. Use the current output for
counts; configuration suffixes identify distinct process environments.

[`mdl_register_test()`](../cmake/MdlPluginTesting.cmake) supplies the build-tree
runtime paths and invokes [mdlTestWrapper.py](../cmake/mdlTestWrapper.py).
The wrapper copies the selected `TESTENV_DIR` into a fresh temporary directory,
sets `PXR_TEST_DIR`, and runs there. Successful runs remove that directory;
failures retain it for inspection. Running fixture scripts directly bypasses
this setup.

Lookup drivers use fresh worker processes so SDK/Sdr caches and cached
environment settings cannot conceal differences between configurations.
They also capture native diagnostics where warnings are part of the contract.
The registration helper defaults `USD_MDL_LOAD_USD_PREVIEW_SURFACE=0` so the
caller's environment cannot change existing discovery tests; per-case `ENV`
settings can override that default.

## Generated Preview Surface definitions

`testGenerateShaderDefinitions.py` compares the generated stage with the
selected SDK's canonical stage. It checks inherited ports, USD roles, defaults,
allowed tokens, connectability, documentation and backend aliases, alongside
provenance hashes, preserved source attribution comments, copied license/notice
bytes and deterministic regeneration. Empty license/notice files are rejected
before any outputs are written. Negative inputs also exercise an
incorrect Python SDK version, unknown shader families, missing or incompatible
mapped ports, required versus optional families, external composition or
relative asset dependencies, conflicting existing MDL opinions, and input/output
collisions. Existing hierarchical relative MDL assets are rejected before their
anchor changes; bare names, absolute paths, resolver URIs and compatible exact
signatures are retained.
Versioned identifiers keep their identity while selecting the family binding.

`testMdlPreviewSurfaceAuto` uses fresh subprocesses for unset, disabled and
enabled `USD_MDL_LOAD_USD_PREVIEW_SURFACE` values. Its original, resource-free
MDL fixture provides the canonical function signatures without a Kit checkout.
Before calling registration APIs, the tests resolve both `mdl` and `glslfx`
entries for the canonical identifiers, verify source-type priority, and ensure
full native discovery remains disabled. They also check explicit registration
with the switch off and nonfatal behavior when the MDL module is missing.

Installed-package checks require the generated asset, provenance and copied
license, and compare their hashes and recorded USD version with the installed
runtime. The package contains no MDL modules or libraries.

## USD-authored interface registration

`testMdlShaderDefinitions` checks registration through the public Python API,
including port aliases, return-struct fields, material-terminal aliases, USD
roles, enum and asset defaults, module overrides and immutable registry identity.
Fresh-process checks cover concurrent first registration and reject interface
identifiers that impersonate native discovery identifiers.

Its array constructor cases verify explicit specializations with renamed and
reordered ports, required inputs and authored defaults, concrete array shape,
struct connection placeholders, enum identity, and flattened bool-vector and
matrix storage. Interface-first, native-first and concurrent registration workers
check cached reuse, changed-interface rejection and preserved module/definition
identities. Invalid definitions cover missing, duplicate or unknown bindings,
incompatible input/output types, malformed and zero counts, incorrect element
module ownership, invalid default lengths and enum values, and unsupported
symbolic enum-array storage. The
[constructor interface example](../docs/shader-definitions.md#array-constructor-interfaces)
is registered from its actual documentation block.

## MDL port connections

`testMdlCanConnectTo` exercises the public Python helper using resolved MDL
fixtures. It covers argument order, null ports, direction, input connectability,
scalar and alias identity, distinct named enums/structs, zero and fixed array
lengths, fixed-to-deferred connections, flattened matrix shape, array element
identity, explicit uniform/varying modifiers and native types retained across
annotated and authored `renderType` overrides. USD shader definitions also
exercise port aliases and attempted overrides of the native `mdl.type` field.
`testMdlCanConnectToCpp` additionally verifies the native exported API with
incomplete declarations, unknown types/modifiers and malformed array sizes.
The helper's pairwise result does not validate a whole function call's
deferred-size bindings or graph-derived uniformity.

## Annotation promotion

`testMdlAnnotations` distinguishes explicit `SDR__` promotion from unprefixed
custom metadata, verifies SDK fallbacks and hints, and checks node/input/return
and struct-field scope. Cases cover reserved `__SDR__` key aliases, exact case,
boolean values, markers, argument names and repeated overloaded annotations.
Annotations targeting one field reject a node when their promoted values
differ; this covers different modules, repeated same-qualified annotations and
overloads. Identical values remain valid. Each occurrence retains its full SDK
name, signature and complete arguments in the ordered raw records, including
promoted annotations and zero-argument markers. Multi-argument occurrences do
not hide an earlier promotable value. Repeated `anno::usage` hints preserve
order and remove identical duplicates; explicit `SDR__hints` takes precedence.
SDK scalar fallbacks and UI hints use the last eligible complete occurrence
returned by the SDK. A legacy-MDL fixture also distinguishes compiler-suppressed
repeated ranges from occurrences actually exposed to the plugin.
Nested module, type and enum-member scopes retain these records without direct
promotion. See the [metadata contract](../docs/metadata.md).

## Sub-identifier lookup

[`testMdlSubIdentifiers.py`](testMdlSubIdentifiers.py) drives public UsdShade
lookup using expectations from [mdl_subidentifier_cases.py](mdl_subidentifier_cases.py)
and modules in `testMdlSubIdentifiers.testenv/`. Assertions check the selected
MDL database definition, inputs, canonical metadata, implementation identity,
and expected rejection or warning.

The process matrices cover:

- Ordinary lookup: cold/on-demand, explicitly registered, and eager discovery;
  first-overload compatibility unset/disabled/enabled; forward/reverse requests.
- `struct_*`: constructor preference and first-overload fallback independently
  enabled/disabled, across the same lookup modes. Checks include constructor
  identity, field defaults, explicit signatures, ordinary overloads, and warnings.
- `legacy_*`: material-signature compatibility unset/disabled/enabled,
  independently of first-overload fallback. Exact matches must not warn;
  malformed requests and wrong function signatures must still fail.
- `relocation`: repeated lookup in fresh processes at different filesystem
  roots, including the standalone sibling-module fixture.

Fixtures distinguish local and external structs/enums with colliding names,
re-exports, hidden/private definitions, nested packages, builtin types, arrays,
constructors, and malformed signatures. Hidden functions, materials, and field
accessors must remain valid and carry `mdl:hidden`; visible nodes carry false.
Hidden overloads participate in ambiguity and compatibility rules. Accepted definitions have incorrect
explicit-signature controls. Source-asset cases check layer-local precedence,
MDL search-path fallback, strict-relative misses, and same-named modules with
different defaults; resolved paths and Ar-opened bytes identify the chosen file.

The native `testMdlSubIdentifier` checks discovery identifiers.
`testMdlOffSearchPath` covers standalone resource/import behavior; passing these
fixtures does not broaden the documented supported content setup.

## MDL execution diagnostics

`testMdlDiagnostics` uses a real host-owned SDK, a thread-safe Tf diagnostic
capture and an application logger. It checks successful compiler warnings,
failed compilation with attached notes, warnings for missing textures, corrupt
PNG data and rejected dot-segment paths, clean loads, concurrent message
association and cached-load silence. A valid PNG control uses a host-loaded
image plugin, distinguishing corrupt data from unavailable decoder support. It
also checks that native delegates receive missing-SDK loader diagnostics. Structured
payloads retain the original SDK fields without creating Tf errors, and the
host logger and SDK lifecycle remain under application control.
`testMdlDiagnosticsDebug` verifies that INFO records are quiet by default and
visible with `TF_DEBUG=USD_MDL_DIAGNOSTICS`.

`testMdlDiagnosticsPython_cold` and `_registered` check that the same modules
preserve Python lookup and registration results. Invalid resources must retain
valid Sdr nodes with empty asset defaults; the valid PNG must retain its resolved
asset default. Their fixtures live in
`testMdlDiagnostics.testenv/` so intentionally invalid or warning-producing
modules do not enter the healthy discovery sweeps.

## Rejected resources

`testMdlRejectedResource_legacy`, `_sdk` and `_plugin` recreate issue #25
with a blue 64x64 RGB `mdl/texture.png`, a red 1x1 RGBA
`mdl/d3/nested/texture.png`, and a different readable PNG outside the root.
The first case matches the original reproduction: a float function with a
single texture parameter default and no other resource references. The
historical resolver behavior is reproduced by a test-only wrapper that
returns null for the authored above-root path. The real SDK then loads the
wrong root texture; the SDK filesystem resolver rejects that same module.
The fixed plugin keeps the module loadable with an invalid resource value.

Assertions inspect the SDK database reference, decoded canvas dimensions,
channel layout, pixel, filename and Ar-opened PNG bytes. A legitimate
`/texture.png` default in the same function must still load the root image,
while the rejected parameter converts to an empty Sdr asset default. Relative
`./` and `../../` controls, malformed `./../nested`, UDIM and frame paths,
imported defaults, independent concurrent loads and cached-load silence are
covered. Warning payloads must retain the authored path, owner and source
position without posting Tf errors. Printed USD warning text hides the private
identity; payloads retain the raw SDK message alongside original resource
fields. Direct SDK loads use ordinary contexts without plugin diagnostic
collection. Separate direct callbacks preserve foreign `user_data` and reject
reserved identities even when a readable file exists under the private path.

`testMdlRejectedResourcePython_cold` and `_registered` run fresh public
UsdShade lookup and registration processes. They require valid Sdr nodes,
empty defaults for rejected paths, successful valid controls and no Python
exception. The driver also checks native output for original authored paths
and absence of private identities in USD warning text. The independent SDK
logger keeps its raw messages. Fixtures are isolated in
`testMdlRejectedResource.testenv/`.

## Other process configurations

`testMdlArrayConstructors_*` checks public Sdr/UsdShade specialization lookup in
cold, registered and eager processes, in forward/reverse order. Named types are
requested first. Assertions cover concrete ports and identities, SDK serialized
names, struct/enum/nested element types, flattened versus MDL counts, fixed-zero
and deferred declarations, empty versus unavailable defaults, malformed requests,
helper/overload stability and saved-USD lookup in a fresh process. Ownership
checks reject imported, qualified and relative type names and builtin types
anchored to user modules. Same-named types in distinct modules retain distinct
identities. An experimental off-search-path bundle is copied to another location
and each copy is resolved in a fresh process through ordinary UsdShade lookup.
The actual documentation examples are also parsed and resolved. Its mixed
helper/constructor graph checks Sdr resolution; it is not a renderer test.

- `testMdlModuleIdentity`: compares SDK-derived package names with URI-encoded
  names, verifies selected source files using different defaults, and checks
  whether file-URI aliases share a database module. Percent-containing names
  cover SDK loading, registration, cold lookup, relative/absolute imports, and
  owner-relative resource paths. Direct module callbacks reject existing raw
  filenames and URIs; a recording resolver also blocks raw filename callbacks
  during registration and cold lookup with imports. Assertions retain
  the reserved first-component collisions as documented limitations.
- `testMdlPathContracts`: compares resource dot-component spelling and deep
  relative imports through direct plugin callbacks and real compilation with
  the plugin and SDK filesystem resolvers. It also checks logical and resolved
  resource paths before and after host resource edits, with different files at
  the package and root locations. Assertions explicitly characterize current
  limitations; they must be updated when the corresponding behavior is fixed.
- `testMdlEmbeddedModules_*`: absent, conventional, empty, and invalid dummy
  files for embedded modules; MDL-root versus Ar-context resolution; cold,
  registered, and eager lookup. Checks cover definition identity, URIs, Ar
  bytes, portable reauthoring, and diagnostics. The `base` field accessor
  checks hidden generated definitions; complete registration must contain only
  valid nodes.
- `testMdlBuiltins_*`: the same dummy-file matrix for scalar, vector, color,
  enum, material-emission, and texture constructors. Enum checks cover symbols,
  signature spellings, options, storage, and default presence. Checks also cover
  exact SDK database names and similar filenames that
  must remain ordinary modules. `testMdlBuiltinsFirst` starts a fresh runtime
  with no library roots, verifies the builtin module is initially absent, then
  requests it before any other module and accesses its SDK definition. It also
  checks rejection of ambiguous bare names and incorrect signatures.
- `testMdlExpressions_registered_*`: default-expression behavior with and
  without explicit registration. `testMdlDefaultExpressions` independently
  checks SDK expression kinds against Sdr values and metadata. An isolated
  uncommitted module proves that selection, call defaults, scalar resources,
  and resource-array elements use the caller's transaction. Copied node data
  must survive abort, and a failed selection must leave the transaction usable.
- `testMdlLazyDiscovery_*`: unset/disabled/enabled startup discovery and its
  effects before and after initialization or lookup.
- Ownership and shutdown tests exercise plugin-owned and externally supplied
  SDK instances. Unstarted host instances must be rejected without changing
  their SDK state, search paths, or image plugins; attachment can succeed after
  the host starts that same instance. Started hosts retain their plugin
  configuration; shut-down instances are also rejected. These tests establish
  lifecycle behavior separately from Python fixture runners that deliberately
  bypass interpreter teardown.
- `testMdlSdkRelocation` copies the SDK runtime into a directory with spaces,
  removes its original location from the child process's library search paths,
  and checks default loading and an explicit path to a renamed SDK library.
  A negative control verifies that default loading fails with neither runtime
  on the library search path. Both image plugins must load from the copied
  directory, even when only the SDK's explicit path is supplied. With image
  plugins moved onto the OS library search path, a host can load them explicitly,
  while plugin-owned initialization warns and leaves them unloaded. Ownership cases also
  check empty paths, rejection of missing explicit paths without fallback,
  and recovery after failed loading.

Dependency automation and SBOM tests live outside this directory; their
commands and ownership are in the [contributor guide](../CONTRIBUTING.md#dependencies).

## Resources and SDK compatibility

[`testMdlResources.cpp`](testMdlResources.cpp) exercises the Ar entity resolver;
[`testMdlUriEncoding.cpp`](testMdlUriEncoding.cpp) checks URI/name encoding.
Both cover zero, one, two, and three slashes after a URI scheme. The custom Ar
resolver maps distinct spellings to distinct content, checks fresh lazy lookup
and explicit registration, host SDK loading with sibling/parent imports, and
resource byte round trips. Invalid parent traversal must stop before Ar can
remove a URI scheme or authority. Module and resource identities share the
same explicit prefix convention.
The [type-conversion tests](testMdlSdrPlugins.testenv/testMdlTypes.py) also
check resource defaults against the loaded SDK version. The isolated
`texture_array_only.mdl` fixture has no scalar texture references: assertions
cover authored paths, gamma, selectors, and Ar-opened bytes. Literal
light-profile and cube-texture arrays have corresponding resource checks.
Older SDK expectations remain explicit alongside the MDL SDK 2026.0.2 fix.

## Example programs

[`testInspectMdlMaterial.py`](testInspectMdlMaterial.py) runs the Python and C++
scene-inspection examples. It checks sample inputs, defaults, labels, help
text, pages, range hints, and authored/resolved texture paths. C++ checks also
cover texture file enumeration, module/definition database names, and the
SDK's qualified module and simple definition names. A second material in a
nested module checks definition selection; a missing definition must fail.

## CI and staged packages


After [staging a package](../docs/build.md#stage-a-package-for-another-project),
CI configures and builds a [downstream CMake project](testInstalledPackage.testenv/cmake/CMakeLists.txt)
and runs [testInstalledPackage.py](testInstalledPackage.testenv/testInstalledPackage.py)
against the clean install. The C++ consumer links only `usdMdl::usdMdl`, loads
a fixture module, checks its Sdr ports and default, and shuts down normally.
CTest runs it in the same installed environment as the Python smoke test.
The test-owned MDL module declares `SDR__label` and `SDR__connectable`
annotations locally and imports the SDK-embedded `::anno` module. The Python
smoke test checks annotation metadata, original port names, defaults, resource
provenance and third-party notices using its own MDL search root. The downstream
CMake project checks the installed plugin directory, confirms that MDL content
paths are not exported, and compiles the shader-registration and enum-value
public APIs.
The package must contain no `mdl/` directory. These checks run separately from
the build-tree CTest suite; the workflow supplies the commands and environment
for each platform.

`testEnvironmentHelpers` checks that helpers configure plugin and Python paths
while leaving application MDL paths unchanged, including empty and unset
values. `testMdlWithoutBundledContent` covers runtime independence through an
application-only fixture with an embedded SDK import in the build-tree suite.
