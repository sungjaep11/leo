"""Paths and integrity checks shared by standalone reproduction scripts."""
from pathlib import Path
import hashlib,json,tarfile,os
HERE=Path(__file__).resolve().parent

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare_state():
    env=json.loads((HERE/'environment.json').read_text())
    archive=HERE/'inputs/state.tar.gz'
    if sha(archive)!=env['state_archive_sha256']: raise RuntimeError('State archive checksum mismatch')
    dest=HERE/'.runtime';dest.mkdir(exist_ok=True)
    state=dest/'state'
    if not state.exists():
        with tarfile.open(archive) as f: f.extractall(dest,filter='data')
    for rel,digest in json.loads((HERE/'inputs/state_manifest.json').read_text()).items():
        if not (state/rel).is_file() or sha(state/rel)!=digest: raise RuntimeError('Input changed: '+rel)
    return state

def clean_env():
    env={k:v for k,v in os.environ.items() if not k.startswith('LAB1_')}
    env.update(LAB1_TRACE='0',LAB1_VEGAS_TRACE='1',LAB1_ORACLE_CALIBRATE='0',LAB1_VEGAS_RECENT_WINDOW_S='0')
    return env

def check_control(logs):
    expected=json.loads((HERE/'reference/control_hashes.json').read_text())
    result={name:sha(logs/name)==digest for name,digest in expected.items()}
    if not all(result.values()): raise RuntimeError('Original control differs: '+str(result))
    return result
