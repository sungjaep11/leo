"""Build/smoke then reproduce dynamic, true-congestion and sharing experiments."""
from pathlib import Path
import os,json,subprocess,concurrent.futures,time,argparse,hashlib
from bundle import HERE,prepare_state,clean_env,check_control
here=HERE;ns3=Path(os.environ['NS3_DIR']);base=here/'inputs'
p=argparse.ArgumentParser();p.add_argument('suite',choices=['smoke','dynamic','congestion','sharing','diagnostic','all']);p.add_argument('--runs',type=Path,default=here/'output/runs');p.add_argument('--jobs',type=int,default=2);a=p.parse_args()
a.runs=a.runs.resolve();a.runs.mkdir(parents=True,exist_ok=True)
if a.jobs<1: p.error('--jobs must be positive')
state=prepare_state()
exe=ns3/'build/scratch/main_satnet/ns3.48-main_satnet'

def run(case):
    name,enabled,interval,delay,scenario=case
    dest=a.runs/('vegas_geometry_'+name);assert not dest.exists(),dest
    config=(base/'config_ns3.properties').read_text();schedule=(base/'schedule.csv').read_text()
    relative=Path(os.path.relpath(state,dest)).as_posix()
    config='\n'.join(f'satellite_network_dir="{relative}"' if line.startswith('satellite_network_dir=') else f'satellite_network_routes_dir="{relative}/dynamic_state_100ms_for_200s"' if line.startswith('satellite_network_routes_dir=') else line for line in config.splitlines())+'\n'
    duration=5 if scenario in ['smoke','sharing'] else 200
    if scenario=='static':duration=120
    config=config.replace('simulation_end_time_ns=200000000000',f'simulation_end_time_ns={duration*10**9}')
    if scenario=='static':
        config=config.replace('dynamic_state_update_interval_ns=100000000','dynamic_state_update_interval_ns=120000000000')
        config+='satellite_network_force_static=true\n'
    if scenario in ['static','mixed']:
        config+='enable_udp_burst_scheduler=true\nudp_burst_schedule_filename="udp_schedule.csv"\n'
    if scenario=='sharing':
        schedule+='1,1584,1585,100000000000,1000000000,,\n'
        config=config.replace('set(0)','set(0,1)')
    dest.mkdir();(dest/'config_ns3.properties').write_text(config);(dest/'schedule.csv').write_text(schedule)
    if scenario in ['static','mixed']:
        start,length=(60,40) if scenario=='static' else (130,50)
        (dest/'udp_schedule.csv').write_text(f'0,1584,1585,80,{start*10**9},{length*10**9},,competition\n')
    env=clean_env()
    env.update(LAB1_TRACE='0',LAB1_VEGAS_TRACE='1',LAB1_ORACLE_CALIBRATE='0',LAB1_VEGAS_RECENT_WINDOW_S='0',
               LAB1_VEGAS_GEOMETRY=str(enabled),LAB1_GEOMETRY_INTERVAL_MS=str(interval),LAB1_GEOMETRY_DELIVERY_MS=str(delay))
    meta={'scenario':scenario,'enabled':bool(enabled),'interval_ms':interval,'delivery_ms':delay,'duration_s':duration,
          'env':{k:v for k,v in env.items() if k.startswith('LAB1_')},'source_manifest':json.loads((here/'installed_manifest.json').read_text())}
    command=[str(exe),f'--run_dir={dest}']
    if name=='static_p100_ack1':
        command.append('--ns3::TcpSocket::DelAckCount=1')
        meta['diagnostic_only']='DelAckCount=1 from connection start; not primary algorithm performance.'
    meta['command']=command
    (dest/'experiment.json').write_text(json.dumps(meta,indent=2));start=time.monotonic();print('START '+dest.name,flush=True)
    with (dest/'out.txt').open('w') as out:subprocess.run(command,cwd=ns3,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
    assert (dest/'logs_ns3/finished.txt').read_text().strip()=='Yes'
    meta.update(completed=True,wall_seconds=time.monotonic()-start)
    (dest/'experiment.json').write_text(json.dumps(meta,indent=2))
    print(f'DONE {dest.name}: {meta["wall_seconds"]:.1f} wall seconds',flush=True)
    if scenario=='dynamic' and not enabled:
        check_control(dest/'logs_ns3')
        print('PASS full disabled control matches original byte-for-byte.',flush=True)
    if scenario=='sharing':
        cost=json.loads((dest/'logs_ns3/geometry_cost.json').read_text());assert cost['registered_connections']==2 and cost['cached_paths']==1
        assert cost['prediction_builds']==50,cost
        print('PASS two connections share one 50-entry prediction stream.',flush=True)
    return dest
cases={
 'smoke':[('smoke_off',0,100,0,'smoke'),('smoke_on',1,100,0,'smoke')],
 'dynamic':[('control',0,100,0,'dynamic'),('p100',1,100,0,'dynamic'),('p10',1,10,0,'dynamic'),
            ('p500',1,500,0,'dynamic'),('p100_d100',1,100,100,'dynamic'),('p100_d500',1,100,500,'dynamic')],
 'congestion':[('static_control',0,100,0,'static'),('static_p100',1,100,0,'static'),
               ('mixed_control',0,100,0,'mixed'),('mixed_p100',1,100,0,'mixed')],
 'sharing':[('sharing',1,100,0,'sharing')],
 'diagnostic':[('static_p100_ack1',1,100,0,'static')]}
cases=sum(cases.values(),[]) if a.suite=='all' else cases[a.suite]
for case in cases:
    if (a.runs/('vegas_geometry_'+case[0])).exists(): raise FileExistsError('Use a new output directory: '+str(a.runs))
with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:list(pool.map(run,cases))
