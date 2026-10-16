#
# SPDX-License-Identifier: Apache-2.0
#

import os as _os
from pathlib import Path as _Path

# The installed Windows layout keeps usdMdl.dll in <prefix>/plugin/usd while
# this extension lives in <prefix>/lib/python/UsdMdl. Python 3.8+ no longer
# searches PATH for extension-module dependencies by default, so register the
# relocatable sibling directory before importing _usdMdl. Keep the returned
# handle alive for the lifetime of this module.
_usdMdlDllDirectory = None
if _os.name == "nt" and hasattr(_os, "add_dll_directory"):
    _pluginDirectory = (
        _Path(__file__).resolve().parent.parent.parent.parent / "plugin" / "usd"
    )
    if _pluginDirectory.is_dir():
        _usdMdlDllDirectory = _os.add_dll_directory(str(_pluginDirectory))

del _os, _Path
try:
    del _pluginDirectory
except NameError:
    pass

# Register dependency converters before wrapper defaults and returned Sdr nodes.
from pxr import Sdf as _Sdf, Sdr as _Sdr, Tf
Tf.PreparePythonModule()
del _Sdf, _Sdr, Tf
