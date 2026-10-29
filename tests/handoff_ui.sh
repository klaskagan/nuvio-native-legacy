#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PYTEST'
from pathlib import Path
import subprocess,tempfile,shlex
source=Path('src/handoff.c').read_text();a=source.index('int handoff_event(');b=source.index('\nvoid handoff_draw(',a)
with tempfile.TemporaryDirectory(prefix='handoff-ui-') as tmp:
 Path(tmp,'handoff_ui.inc').write_text(source[a:b]);binary=str(Path(tmp,'test'))
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True))
 subprocess.run(['cc','-Wall','-Wextra','-Werror',*flags,'tests/handoff_ui.c','-I'+tmp,'-pthread','-o',binary],check=True)
 subprocess.run([binary],check=True)
PYTEST
