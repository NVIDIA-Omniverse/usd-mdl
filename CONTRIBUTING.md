<!-- SPDX-License-Identifier: Apache-2.0 -->

# Contributing to usdMdl

At this time this project is not open to external contributions. We currently cannot
accept external pull requests.

We very much appreciate bug reports, feature requests and feedback through this repository's
GitHub Issues. Please check existing issues first, then join a related discussion
or open a new issue:

- For bugs, include steps to reproduce, expected and actual behavior, and your
  platform, usdMdl, OpenUSD and MDL SDK versions. A minimal example is helpful.
- For feature requests, describe your use case and the problem you would like
  to solve so we can discuss possible approaches.

Thank you for helping us improve usdMdl.

## Maintainer guide

The following development guidance is for project maintainers.

Start with the [architecture](docs/architecture.md) and the guide for the
behavior you are changing in the [documentation index](docs/README.md).
The plugin is one shared library; its runtime headers are internal. Installed
headers, Python tokens, metadata keys, CMake outputs and environment settings
are consumer interfaces even when no in-tree caller uses them.

### Where to change behavior

| Concern | Implementation | Regression coverage |
| --- | --- | --- |
| Registration, discovery, selection | `mdlSdrIntegration`, `mdlModule`, `utils` | `testMdlSubIdentifiers`, `testMdlEmbeddedModules`, `testMdlDiscovery` |
| Types, defaults, annotations | `function`, `annotation`, `converter/` | `testMdlTypes`, `testMdlExpressions`, `testMdlAnnotations`, `testMdlDefaultExpressions` |
| Module/resource identity and Ar reads | `runtime/EntityResolver`, `UriEncoding`, `PathHelpers`, `TileExpansion` | `testMdlResources`, `testMdlUriEncoding`, `testMdlOffSearchPath` |
| SDK ownership and concurrency | `initialize`, `runtime/Context`, `ModuleLoader`, `PluginLoader` | `testMdlNeurayOwnership`, `testMdlConcurrentLoading`, `testMdlRuntimeShutdown` |
| Packaging and examples | `src/CMakeLists.txt`, `cmake/`, `examples/` | `testInspectMdlMaterial`, installed-package smoke test |

Names in the implementation column are under `src/usdMdl/`; tests are under
`tests/`. See the [test guide](tests/README.md) for fixture layout, process
configurations, and how to list the currently registered cases.

### Working and validating

Inspect `git status` and the existing build cache before changing dependencies
or configuration. Follow [build and test](docs/build.md) for a fresh build and
the [test guide](tests/README.md#ci-and-staged-packages) for installed-package
checks. Use a separate build directory when comparing SDKs
so that DLLs, headers and fixture expectations stay matched.

For a focused change, build and run the affected CTest entries first:

```sh
cmake --build <build> --config Release
ctest --test-dir <build> -C Release -R 'testMdl(Types|Expressions)' --output-on-failure --no-tests=error
```

Run the complete suite for changes to shared conversion, runtime or dependency
behavior. CTest stages fixtures and supplies the build-tree runtime paths;
installation is unnecessary for these tests. Separate lifecycle tests cover
normal shutdown; some Python fixture runners deliberately bypass interpreter
teardown and cannot establish shutdown correctness.

When adding coverage, assert observable behavior: exact selected definitions,
USD types and defaults, diagnostics, or bytes opened through Ar. Resource tests
must distinguish an empty value from a successfully loaded texture. Exercise
cold lookup, registration and eager discovery when their paths differ. Keep
known unsupported cases explicit instead of accepting either outcome.

Before review, run `git diff --check`, inspect the diff, and record the platform,
OpenUSD/SDK versions, commands and results in the pull request. A local pass
is not evidence that the full Linux/Windows CI matrix passed.

### Dependencies

See the [build prerequisites](docs/build.md#prerequisites) for supported
dependency versions and upstream installation instructions.


An SDK fix can change expected results. Keep version-specific expectations in
tests, with a release reference and a strict assertion for each supported SDK;
do not add production workarounds for behavior the SDK now supplies.

### Documentation and comments

The [documentation index](docs/README.md) assigns a home to each topic. Update
that page and link to it from neighboring guides. Keep examples in `examples/`
and their usage in [the example guide](docs/examples.md); CTest checks the
programs. Keep the root README an entry point; detailed API and setup
instructions belong in the guides. Public documentation must stand on its own
and link only to files included in the release. Changes to documented support
or limitations require reproducible runtime validation.

Record user-visible release changes in [CHANGELOG.md](CHANGELOG.md), using short
bullets under the applicable `Added`, `Changed`, `Removed`, `Fixed`, `Security`
or `Limitations` headings. Link to the owning guide for details and mark breaking
changes explicitly. Omit empty categories; keep pending releases undated.

Test structure and coverage details belong in [tests/README.md](tests/README.md),
not consumer guides. Obtain test counts from CTest rather than maintaining
totals in documentation.

Comments should explain constraints that are easy to break: SDK handle lifetime,
lock ownership, distinctions between MDL and Ar identities, storage shape, and
why a default cannot be evaluated. Remove obsolete implementation history and
comments that merely repeat the next statement.
