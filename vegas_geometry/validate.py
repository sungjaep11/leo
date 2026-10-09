"""Cross-run isolation, traffic independence, cache sharing, and source checks."""
from pathlib import Path
import csv,json,hashlib,os,argparse,math
from bundle import HERE,check_control
here=HERE
p=argparse.ArgumentParser();p.add_argument('--runs',type=Path,default=here/'output/runs');p.add_argument('--out',type=Path,default=here/'output/analysis');a=p.parse_args()
RUNS=a.runs.resolve();a.out.mkdir(parents=True,exist_ok=True)
def table(name):
    with (RUNS/f'vegas_geometry_{name}'/'logs_ns3/geometry_predictions.csv').open() as h:
        return [{k:v for k,v in r.items() if k!='compute_ns'} for r in csv.DictReader(h)]
assert table('p100')==table('mixed_p100'),'Predictions changed with traffic'
assert table('static_p100')==table('static_p100_ack1'),'Predictions changed with ACK behavior'
sharing=json.loads((RUNS/'vegas_geometry_sharing/logs_ns3/geometry_cost.json').read_text())
assert sharing['registered_connections']==2 and sharing['cached_paths']==1 and sharing['prediction_builds']==50
control=check_control(RUNS/'vegas_geometry_control/logs_ns3')
validation={'disabled_control_byte_equal':control,'predictions_identical_with_competing_traffic':True,
 'predictions_identical_with_changed_ACK_behavior':True,'two_connections_share_one_cache':True,'sharing_cost':sharing,
 'note':'No position/routing/queue trace from the unloaded calibration is consumed by the algorithm. Old calibration is an analysis-only reference.'}
if 'NS3_DIR' in os.environ:
    ns3=Path(os.environ['NS3_DIR'])
    for rel,sha in json.loads((here/'installed_manifest.json').read_text()).items():
        assert hashlib.sha256((ns3/rel).read_bytes()).hexdigest()==sha,rel
    validation['installed_source_integrity']=True
# Compare deterministic outcomes, excluding host-dependent wall time.
compared=0
for suite in ['dynamic','congestion']:
    expected=json.loads((here/'reference'/f'{suite}_metrics.json').read_text())['metrics']
    actual=json.loads((a.out/f'{suite}_metrics.json').read_text())['metrics']
    index={(r['case'],r['start_s'],r['end_s']):r for r in actual}
    for row in expected:
        current=index[(row['case'],row['start_s'],row['end_s'])]
        for key,value in row.items():
            other=current[key]
            assert math.isclose(value,other,rel_tol=1e-9,abs_tol=1e-9) if isinstance(value,(int,float)) else value==other,(suite,row['case'],key,value,other)
        compared+=1
validation['reference_metric_windows_matched']=compared
(a.out/'validation.json').write_text(json.dumps(validation,indent=2))
print(json.dumps(validation,indent=2))
