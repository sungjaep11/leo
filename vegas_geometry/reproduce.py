"""Install, build, run and analyze from this folder only (inside pinned Docker)."""
from pathlib import Path
import argparse,json,os,subprocess,sys,time,datetime
from bundle import HERE,prepare_state
from install import install

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--suite',choices=['all','smoke','dynamic','congestion','sharing'],default='all')
    p.add_argument('--output',type=Path,default=HERE/'output'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p.add_argument('--jobs',type=int,default=2)
    a=p.parse_args();out=a.output.resolve()
    if out.exists(): p.error('Output already exists; choose a new name: '+str(out))
    if a.jobs<1: p.error('--jobs must be positive')
    out.mkdir(parents=True);started=time.monotonic()
    record={'suite':a.suite,'status':'running','environment':json.loads((HERE/'environment.json').read_text()),'source_directory':str(HERE)}
    def save(): (out/'reproduction.json').write_text(json.dumps(record,indent=2))
    def call(script,*args): subprocess.run([sys.executable,str(HERE/script),*map(str,args)],check=True)
    save()
    try:
        prepare_state();install()
        ns3=Path(os.environ['NS3_DIR'])
        print('Building main_satnet; output in build.log',flush=True)
        with (out/'build.log').open('w') as log:
            subprocess.run([str(ns3/'ns3'),'build','main_satnet','-j',str(a.jobs)],cwd=ns3,stdout=log,stderr=subprocess.STDOUT,check=True)
        call('run.py',a.suite,'--runs',out/'runs','--jobs',a.jobs)
        if a.suite=='congestion': call('run.py','diagnostic','--runs',out/'runs','--jobs',a.jobs)
        for suite in ['dynamic','congestion']:
            if a.suite in ['all',suite]:
                with (out/f'{suite}_analysis.log').open('w') as log:
                    subprocess.run([sys.executable,str(HERE/'analyze.py'),'--suite',suite,'--runs',str(out/'runs'),'--out',str(out/'analysis')],stdout=log,stderr=subprocess.STDOUT,check=True)
        if a.suite=='all': call('validate.py','--runs',out/'runs','--out',out/'analysis')
        record['status']='passed'
        print('PASS reproduction complete: '+str(out),flush=True)
    except Exception as exc:
        record.update(status='failed',error=str(exc));raise
    finally:
        record['wall_seconds']=time.monotonic()-started;save()

if __name__=='__main__': main()
