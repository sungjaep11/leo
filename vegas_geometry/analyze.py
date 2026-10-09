"""Validate and compare cached network-geometry Vegas and congestion controls."""
from pathlib import Path
import csv,json,os,sys,argparse
from bundle import HERE,check_control
p=argparse.ArgumentParser();p.add_argument('--suite',choices=['dynamic','congestion'],required=True)
p.add_argument('--runs',type=Path,default=HERE/'output/runs');p.add_argument('--out',type=Path,default=HERE/'output/analysis');a=p.parse_args()
RUNS=a.runs.resolve();OUT=a.out.resolve();OUT.mkdir(parents=True,exist_ok=True)
os.environ.setdefault('MPLCONFIGDIR',str(OUT/'.mplconfig'))
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
def rows(path):
    with path.open() as h:yield from csv.DictReader(h)
def sample(trace,x):
    t,v=trace;i=np.searchsorted(t,x,side='right')-1
    return np.where(i>=0,v[np.maximum(i,0)],0.)
def traces(logs):
    result={}
    for kind in ['cwnd','rtt','progress']:
        arr=np.loadtxt(logs/f'tcp_flow_0_{kind}.csv',delimiter=',',ndmin=2)
        assert np.all(np.diff(arr[:,1])>=0)
        result[kind]=(arr[:,1]/1e9,arr[:,2])
    return result
DATA={};metrics=[];validation={};costs={};calibration={};inputs={}
cases=['control','p10','p100','p500','p100_d100','p100_d500'] if a.suite=='dynamic' else ['static_control','static_p100','mixed_control','mixed_p100','static_p100_ack1']
for name in cases:
    run=RUNS/f'vegas_geometry_{name}';logs=run/'logs_ns3'
    assert (logs/'finished.txt').read_text().strip()=='Yes'
    meta=json.loads((run/'experiment.json').read_text());duration=meta['duration_s'];scenario=meta['scenario'];enabled=meta['enabled']
    config=(run/'config_ns3.properties').read_bytes()
    if scenario in inputs:assert config==inputs[scenario]
    else:inputs[scenario]=config
    assert (run/'schedule.csv').read_bytes()==(HERE/'inputs/schedule.csv').read_bytes()
    tr=traces(logs);d=[r for r in rows(logs/'tcp_flow_0_vegas.csv') if int(r['diff'])>=0]
    dt=np.array([float(r['time_s']) for r in d]);base=np.array([float(r['base_ms']) for r in d]);diff=np.array([int(r['diff']) for r in d])
    events=list(rows(logs/'tcp_flow_0_socket_events.csv'))
    retx=np.array([float(r['time_s']) for r in events if r['kind']=='retransmission'])
    drops=np.array([float(r['time_s']) for r in rows(logs/'vegas_queue_drops.csv')])
    tcDrops=np.array([float(r['time_s']) for r in rows(logs/'traffic_control_drops.csv')])
    n=duration*100+1;qmean=np.zeros(n);qpeak=np.zeros(n);qwait=np.zeros(n);qdeq=np.zeros(n);qsumwait=np.zeros(n)
    for r in rows(logs/'vegas_queue_10ms.csv'):
        i=round(float(r['time_s'])*100)
        assert abs(float(r['window_s'])-.01)<1e-8
        qmean[i]+=float(r['mean_packets']);qpeak[i]=max(qpeak[i],int(r['peak_packets']))
        qwait[i]=max(qwait[i],float(r['max_wait_ms']));qdeq[i]+=int(r['dequeued']);qsumwait[i]+=float(r['wait_sum_ms'])
    windows={'dynamic':[(0,200),(115,120),(122,127),(150,160),(170,185),(195,200),(121.6,200)],
             'static':[(0,120),(50,60),(60,65),(70,80),(90,100),(100,110),(110,120)],
             'mixed':[(0,200),(120,130),(130,140),(150,160),(170,180),(180,187),(195,200)]}[scenario]
    for start,end in windows:
        s=slice(round(start*100)+1,round(end*100)+1);mask=(dt>=start)&(dt<end)
        metrics.append({'case':name,'start_s':start,'end_s':end,
          'acked_mbps':float((sample(tr['progress'],end)-sample(tr['progress'],start))*8/(end-start)/1e6),
          'base_min_ms':float(base[mask].min()),'base_max_ms':float(base[mask].max()),
          'cwnd_end_segments':float(sample(tr['cwnd'],end)/1380),
          'vegas_decreases':sum(r['kind']=='decrease_diff_gt_beta' for r,on in zip(d,mask) if on),
          'queue_peak_packets':int(qpeak[s].max()),'mean_total_queued_packets':float(qmean[s].mean()),
          'max_per_hop_wait_ms':float(qwait[s].max()),
          'mean_per_hop_wait_ms':float(qsumwait[s].sum()/qdeq[s].sum()) if qdeq[s].sum() else 0.,
          'device_queue_drop_events':int(np.sum((drops>=start)&(drops<end))),
          'traffic_control_drop_events':int(np.sum((tcDrops>=start)&(tcDrops<end))),
          'retransmissions':int(np.sum((retx>=start)&(retx<end)))})
    cost=json.loads((logs/'geometry_cost.json').read_text());cost['simulation_wall_seconds']=meta['wall_seconds']
    cost['build_mean_us']=cost['build_wall_ns']/max(cost['prediction_builds'],1)/1e3
    cost['lookup_mean_us']=cost['lookup_wall_ns']/max(cost['cache_lookups'],1)/1e3
    cost['build_and_lookup_wall_seconds']=(cost['build_wall_ns']+cost['lookup_wall_ns'])/1e9
    cost['build_and_lookup_fraction_of_run_wall']=cost['build_and_lookup_wall_seconds']/meta['wall_seconds']
    costs[name]=cost
    valid={'config_equal_within_scenario':True,'oracle_off':not (logs/'tcp_flow_0_vegas.csv.oracle.csv').exists(),
           'recent_min_off':not (logs/'tcp_flow_0_vegas.csv.recent.csv').exists()}
    if 'diagnostic_only' in meta:valid['diagnostic_only']=meta['diagnostic_only']
    assert valid['oracle_off'] and valid['recent_min_off']
    if enabled:
        audit=list(rows(logs/'tcp_flow_0_vegas.csv.geometry.csv'));decisionMap={float(r['time_s']):r for r in d}
        for r in audit:
            t=float(r['time_s']);sent=float(r['estimated_send_s']);computed=float(r['snapshot_s']);available=float(r['available_s'])
            assert computed<=available+1e-8 and available<=sent+1e-8 and sent<=t
            assert abs(available-computed-meta['delivery_ms']/1000)<1e-8
            effective=float(r['effective_base_ms']);pred=float(r['prediction_smoothed_ms']);current=float(r['cycle_min_ms'])
            assert abs(effective-min(pred,current))<1e-8
            assert abs(effective-float(decisionMap[t]['base_ms']))<1e-8
        valid.update(decision_audits=len(audit),decisions_without_prediction=len(d)-len(audit),
                     cap_fraction=sum(int(r['clamped']) for r in audit)/len(audit),no_future_or_unavailable_predictions=True)
        predrows=list(rows(logs/'geometry_predictions.csv'))
        assert len(predrows)==cost['prediction_builds']==round(duration*1000/meta['interval_ms'])
        assert cost['cached_paths']==1 and cost['registered_connections']==1
        times=np.array([float(r['computed_s']) for r in predrows]);values=np.array([float(r['predicted_ms']) for r in predrows]);available=np.array([float(r['available_s']) for r in predrows])
        assert np.all(np.diff(times)>0)
        if scenario=='static':assert float(np.ptp(values))<1e-8,'Static model changed with queue load'
        valid['prediction_value_range_ms']=float(np.ptp(values))
        if scenario=='dynamic':
            # Independent old probe run is read ONLY here for evaluation, never by the controller.
            probes=list(rows(HERE/'reference/oracle_probe.csv'))
            sends=np.array([float(r['send_s']) for r in probes]);actual=np.array([int(r['rtt_ns'])/1e6 for r in probes])
            idx=np.searchsorted(available,sends,side='right')-1;keep=idx>=0;error=values[idx[keep]]-actual[keep]
            calibration[name]={'probes':int(keep.sum()),'mean_abs_error_ms':float(np.abs(error).mean()),
                 'p95_abs_error_ms':float(np.quantile(np.abs(error),.95)),'max_abs_error_ms':float(np.abs(error).max()),
                 'note':'Snapshot/cache error against independent queue-free probes; path changes and in-flight reroutes are included.'}
    validation[name]=valid
    DATA[name]=(tr,dt,base,diff,qmean,qpeak,qwait,duration)
    print('Read '+name,flush=True)
if a.suite=='dynamic':
    check_control(RUNS/'vegas_geometry_control/logs_ns3')
    validation['disabled_control_identical']=True
result={'metrics':metrics,'validation':validation,'cost':costs,'calibration_error':calibration,
 'method':{'provider':'Online current geometry + actual forwarding state + configured link attributes. Network-assisted ideal access, not end-host-only.',
 'cache':'Per endpoint pair; snapshot held between updates, ten-second history plus delivery-delay allowance. No future fstate is read.',
 'rtt_model':'Data/ACK propagation plus serialization: 1434 and 54 link bytes. Snapshot approximation, not a time-advanced per-hop predictor.',
 'alignment':'Lookup by max(0, ACK arrival minus raw RTT), approximating transmission time with TCP timestamp precision; source must have been available by that time. Matched RttEstimator clone smooths predictions on raw RTT updates. Choose estimate paired with the Vegas cycle-min observation.',
 'base':'Direct modeled no-load RTT (including serialization), capped at observed cycle min. No rolling upward adaptation from observed RTT; absolute model, not anchor+delta implementation.',
 'cost':'steady_clock measurements for prediction build and cache lookup only; excludes log writes, initialization, per-flow smoothing and real feedback transmission. Timing includes instrument and host scheduling effects.',
 'loss':'Device Queue::Drop and TrafficControlLayer::TcDrop separately observed; not an exhaustive all-layer loss inventory.',
 'limits':'MSS 1380, timestamp ACK, fixed normal packet-size model; excludes delayed ACK/SACK size variations. Same seed/path; no deployment or general fairness claim.'}}
(OUT/f'{a.suite}_metrics.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
with (OUT/f'{a.suite}_comparison.csv').open('w',newline='') as h:
    w=csv.DictWriter(h,fieldnames=list(metrics[0]));w.writeheader();w.writerows(metrics)
def plot(names,filename,title,span=None,xmin=0):
    colors=['#b94a44','#087f8c','#346ac3','#b77700','#8c559e','#5b6575']
    fig,axes=plt.subplots(5,1,figsize=(12,12),sharex=True,layout='constrained')
    for name,color in zip(names,colors):
        tr,dt,base,diff,qmean,qpeak,qwait,duration=DATA[name];x=np.arange(duration)+.5
        label={'control':'Original Vegas','p100':'Geometry cache: 100 ms','p10':'Geometry cache: 10 ms','p500':'Geometry cache: 500 ms',
               'p100_d100':'100 ms cache + 100 ms feedback delay','p100_d500':'100 ms cache + 500 ms feedback delay',
               'static_control':'Original Vegas','static_p100':'Geometry cache: default delayed ACK','mixed_control':'Original Vegas','mixed_p100':'Geometry cache: 100 ms',
               'static_p100_ack1':'Geometry cache: ACK every segment (diagnostic)'}.get(name,name)
        axes[0].plot(x,np.diff(sample(tr['progress'],np.arange(duration+1)))*8/1e6,label=label,color=color)
        axes[1].step(tr['cwnd'][0],tr['cwnd'][1]/1380,where='post',color=color)
        axes[2].plot(dt,base,color=color)
        axes[3].plot(x,qpeak[1:].reshape(duration,100).max(axis=1),color=color)
        axes[4].plot(x,qwait[1:].reshape(duration,100).max(axis=1),color=color)
    axes[0].set_ylabel('ACKed rate (Mbit/s)');axes[0].legend(fontsize=8,ncol=2,loc='best')
    axes[1].set_ylabel('cwnd (segments)');axes[2].set_ylabel('Vegas baseRTT (ms)')
    axes[3].set_ylabel('Actual queue peak\n(packets, 1 s max)');axes[4].set_ylabel('Max per-hop wait\n(ms, 1 s max)');axes[4].set_xlabel('Simulation time (s)')
    for ax in axes:
        ax.set_xlim(xmin,duration);ax.grid(alpha=.15)
        if duration==200:
            for t in [121.6,187.4]:ax.axvline(t,color='#777777',ls=':')
        if span:ax.axvspan(*span,color='#777777',alpha=.1)
    if xmin>0:
        first=round(xmin*100)+1
        axes[3].set_ylim(0,max(1,max(float(DATA[name][5][first:].max()) for name in names)*1.12))
        axes[4].set_ylim(0,max(.1,max(float(DATA[name][6][first:].max()) for name in names)*1.12))
    fig.suptitle(title,fontsize=13);fig.savefig(OUT/filename,dpi=150);plt.close(fig)
if a.suite=='dynamic':
    plot(['control','p100'],'geometry_comparison.png','Periodic geometry prediction reused by Vegas\nNo unloaded RTT table; no queue input',xmin=110)
    plot(['control','p100'],'geometry_full.png','Periodic geometry prediction reused by Vegas\nEnabled from connection start')
    plot(['p10','p100','p500'],'interval_comparison.png','Prediction update interval sensitivity',xmin=110)
    plot(['p100','p100_d100','p100_d500'],'delivery_comparison.png','Cached prediction delivery-delay sensitivity',xmin=110)
else:
    plot(['static_control','static_p100'],'static_comparison.png','Fixed positions and routes; competing UDP 80 Mbit/s',span=(60,100))
    plot(['mixed_control','mixed_p100'],'mixed_comparison.png','Moving routes; competing UDP 80 Mbit/s',span=(130,180))
    plot(['static_p100','static_p100_ack1'],'ack_diagnostic.png','Diagnostic only: delayed ACK behavior changed\nSame geometry algorithm and fixed-route competing load',span=(60,100))
print(json.dumps({'selected_metrics':[r for r in metrics if r['start_s'] in [121.6,90,170,195]],'cost':costs,'validation':validation,'calibration_error':calibration},indent=2))
