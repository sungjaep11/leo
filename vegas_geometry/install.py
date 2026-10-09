"""Install complete source overlay on the pinned clean course image."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
HERE=Path(__file__).resolve().parent
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
def install():
    root=Path(os.environ['NS3_DIR']).resolve()
    env=json.loads((HERE/'environment.json').read_text())
    commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
    if commit!=env['ns3_commit']: raise RuntimeError('Unexpected ns-3 base commit: '+commit)
    final=json.loads((HERE/'installed_manifest.json').read_text())
    base=json.loads((HERE/'base_manifest.json').read_text())
    for rel,digest in final.items():
        if sha(HERE/'source'/rel)!=digest: raise RuntimeError('Package source hash mismatch: '+rel)
        if sha(root/rel) not in (base[rel],digest): raise RuntimeError('Unknown local source: '+rel)
    for rel,digest in final.items():
        dest=root/rel
        if sha(dest)!=digest:
            if dest.exists():
                backup=root/'.geometry-original'/rel
                backup.parent.mkdir(parents=True,exist_ok=True)
                if not backup.exists(): shutil.copy2(dest,backup)
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(HERE/'source'/rel,dest)
        if sha(dest)!=digest: raise RuntimeError('Installation verification failed: '+rel)
    print('PASS complete source overlay installed and verified.',flush=True)
if __name__=='__main__': install()
