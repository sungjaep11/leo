"""Diagnostic runs directly from bundled inputs."""
from pathlib import Path
import subprocess,sys
subprocess.run([sys.executable,str(Path(__file__).with_name('run.py')),'diagnostic',*sys.argv[1:]],check=True)
