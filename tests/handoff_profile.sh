#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PYTEST'
from pathlib import Path
import subprocess,tempfile
source=Path('src/app.c').read_text();start=source.index('int app_handoff_restore_profile(');end=source.index('\nstatic int spotPode(',start)
with tempfile.TemporaryDirectory(prefix='handoff-profile-') as tmp:
 Path(tmp,'handoff_profile.inc').write_text(source[start:end])
 binary=str(Path(tmp,'test'))
 subprocess.run(['cc','-Wall','-Wextra','-Werror','tests/handoff_profile.c','-I'+tmp,'-o',binary],check=True)
 subprocess.run([binary],check=True)
PYTEST
