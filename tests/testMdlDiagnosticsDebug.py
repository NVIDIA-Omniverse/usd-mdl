# SPDX-License-Identifier: Apache-2.0
"""Real SDK module-loading INFO checks the opt-in debug channel."""
import os
import subprocess
import sys

outputs = {}
for enabled in (False, True):
    environment = dict(os.environ)
    environment["TF_DEBUG"] = "USD_MDL_DIAGNOSTICS" if enabled else ""
    result = subprocess.run([sys.argv[1]], env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=90)
    output = result.stdout + result.stderr
    assert result.returncode == 0, (enabled, result.returncode, output)
    outputs[enabled] = output
message = '[load_module ::clean] severity=info'
assert message not in outputs[False], outputs[False]
assert outputs[True].count(message) == 1, outputs[True]
assert 'Loading module "::clean" from' in outputs[True], outputs[True]
assert "[load_module ::invalid] severity=info" not in outputs[True], outputs[True]
assert "[testMdlDiagnostics] OK" in outputs[True], outputs[True]
print("MDL diagnostic INFO debug channel passed", flush=True)
