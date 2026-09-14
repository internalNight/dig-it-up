"""Separate functional acceptance from numerical convergence; export reviewable evidence."""
import argparse
import bisect
import csv
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Saved/TransportV3'
OUTPUT = ROOT / 'Docs/TransportV3'
parser = argparse.ArgumentParser()
parser.add_argument('--prefix', default='Physical')
parser.add_argument('--run-name', default='')
parser.add_argument('--output-dir', default='Docs/TransportV3')
parser.add_argument('--min-delivery-kg', type=float, default=5)
parser.add_argument('--repeat-tolerance', type=float, default=.25)
parser.add_argument('--delivery-gain-kg', type=float, default=2)
parser.add_argument('--fine-delivery-kg', type=float, default=2)
parser.add_argument('--delivery-window-start-s', type=float, default=12)
parser.add_argument('--delivery-window-end-s', type=float, default=25)
parser.add_argument('--common-time-s', type=float, default=25)
args = parser.parse_args()
PREFIX = args.prefix
OUTPUT = ROOT / args.output_dir
RUN_NAME = args.run_name or PREFIX+'Run'
NAMES = [RUN_NAME] + [PREFIX+s for s in ['Repeat', 'ChainOff', 'Stop18', 'Clock30', 'Fine']]

def read(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))

def at(rows, key, t):
    """Linear interpolation of one-second reports to a common physical time."""
    times = [float(r['t']) for r in rows]
    i = bisect.bisect_left(times, t)
    if i == 0:
        return float(rows[0][key])
    if i == len(rows):
        return float(rows[-1][key])
    alpha = (t-times[i-1])/(times[i]-times[i-1])
    return float(rows[i-1][key])*(1-alpha)+float(rows[i][key])*alpha

data, metrics, checks, hashes, source_sets = {}, {}, {}, [], []
OUTPUT.mkdir(parents=True, exist_ok=True)
for name in NAMES:
    folder = SOURCE / name
    material = read(folder / 'MATERIAL_PATH.csv')
    machine = read(folder / 'ROADHEADER.csv')
    pose = read(folder / 'HEAD_DIAGNOSTIC.csv')
    feed = read(folder / 'FEED_CONTROL.csv')
    provenance = json.loads((folder / 'run.json').read_text(encoding='utf-8-sig'))
    hashes.append(provenance['binaryHash'])
    source_sets.append(tuple(sorted((str(Path(x['Path']).relative_to(ROOT)),x['Hash']) for x in provenance['sourceHashes'])))
    log = (folder / f'TransportV3_{name}.log').read_text(encoding='utf-8-sig', errors='replace')
    metrics[name] = dict(
        time_s=float(material[-1]['t']),
        entered_trough_kg=float(material[-1]['troughSeenKg']),
        tail_cross_kg=float(material[-1]['tailCrossKg']),
        rear_settled_kg=float(material[-1]['rearSettledKg']),
        rear_settled_at_common_time_kg=at(material, 'rearSettledKg', args.common_time_s),
        signed_advance_m=float(pose[-1]['signedAdvanceM']),
        sampled_max_retreat_m=max(0, -min(float(r['signedAdvanceM']) for r in pose)),
        sampled_max_penetration_mm=max(float(r['maxPenMm']) for r in machine),
        sampled_max_penetrating_mass_kg=max(float(r['penetratingKg']) for r in machine),
        max_mass_error_kg=max(abs(float(r['massError'])) for r in machine),
        binary_sha256=provenance['binaryHash'])
    data[name] = dict(material=material, machine=machine, pose=pose)
    checks[name+'_mass_finite_no_carry'] = all(abs(float(r['massError'])) <= .001 and int(r['nonfinite']) == 0 and int(r['carried']) == 0 for r in machine)
    checks[name+'_geometry'] = 'GEOMETRY_AUDIT independentPairsOver1mm=0' in log
    checks[name+'_sampled_penetration'] = metrics[name]['sampled_max_penetration_mm'] <= 2.5
    offsets = [float(r['worldT'])-float(r['t']) for r in feed]
    checks[name+'_synchronized_clock'] = max(offsets)-min(offsets) < .003
    time_grid = re.search(r'TIME_GRID outerSeconds=([\d.]+) internalSeconds=([\d.]+) substeps=(\d+)', log)
    checks[name+'_exact_internal_time'] = bool(time_grid) and abs(float(time_grid[1])-float(time_grid[2])*int(time_grid[3])) < 1.e-7
    dest = OUTPUT / 'evidence' / name
    dest.mkdir(parents=True, exist_ok=True)
    for filename in ['MATERIAL_PATH.csv', 'ROADHEADER.csv', 'HEAD_DIAGNOSTIC.csv', 'FEED_CONTROL.csv', 'CHASSIS_COUPLING.csv', 'TOOL_CONTROL.csv', 'TransportGeometry.csv']:
        shutil.copy2(folder / filename, dest / filename)
    # Keep full engine logs locally; public evidence contains only task diagnostics.
    diagnostic_lines = [line.split('LogTemp: Display: ', 1)[-1] for line in log.splitlines()
                        if any(marker in line for marker in ['TIME_GRID ', 'GEOMETRY_AUDIT ', 'MACHINE_MASS ', 'HEAD_CONFIG ', 'SOIL_CONFIG '])]
    (dest / 'configuration.txt').write_text('\n'.join(diagnostic_lines)+'\n', encoding='utf-8')
    normalized = dict(binary_sha256=provenance['binaryHash'], started_utc=provenance['startedUtc'],
                      arguments=provenance['arguments'].replace(str(ROOT), '${REPO}'),
                      source_sha256={str(Path(x['Path']).relative_to(ROOT)): x['Hash'] for x in provenance['sourceHashes']})
    (dest / 'provenance.json').write_text(json.dumps(normalized, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')

checks['one_binary_for_final_matrix'] = len(set(hashes)) == 1
checks['one_source_snapshot_for_final_matrix'] = len(set(source_sets)) == 1
for name in [RUN_NAME, (PREFIX+'Repeat')]:
    m = metrics[name]
    checks[name+'_functional'] = m['rear_settled_kg'] >= args.min_delivery_kg and m['signed_advance_m'] >= .10 and m['sampled_max_retreat_m'] <= .10
    checks[name+'_delivery_during_feed'] = (at(data[name]['material'], 'rearSettledKg', args.delivery_window_end_s)
                                             - at(data[name]['material'], 'rearSettledKg', args.delivery_window_start_s)
                                             >= args.delivery_gain_kg)
main = metrics[RUN_NAME]['rear_settled_kg']
repeat = metrics[(PREFIX+'Repeat')]['rear_settled_kg']
checks['repeat_within_tolerance'] = abs(main-repeat)/max(main,repeat,.001) <= args.repeat_tolerance
checks['stopping_belt_reduces_delivery_by_80_percent'] = metrics[(PREFIX+'ChainOff')]['rear_settled_kg'] <= .2*main
stop = data[(PREFIX+'Stop18')]
checks['drive_stopped_after_command'] = all(abs(float(r['rpm'])) < .1 and abs(float(r['chain'])) < .005 for r in stop['machine'] if float(r['t']) >= 20)
checks['no_sustained_delivery_after_stop'] = at(stop['material'], 'rearSettledKg', 60)-at(stop['material'], 'rearSettledKg', 22) <= .6
reference = metrics[RUN_NAME]['rear_settled_at_common_time_kg']
grid_change = abs(metrics[(PREFIX+'Fine')]['rear_settled_at_common_time_kg']-reference)/max(reference,.001)
clock_change = abs(metrics[(PREFIX+'Clock30')]['rear_settled_at_common_time_kg']-reference)/max(reference,.001)
report = dict(
    functional_checks=checks, functionality_passed=all(checks.values()),
    numerical_convergence=dict(common_physical_time_s=args.common_time_s, method='linear interpolation of reported cumulative mass',
                               fine_grid_also_delivers=metrics[(PREFIX+'Fine')]['rear_settled_kg'] >= args.fine_delivery_kg,
                               grid_relative_change=grid_change, grid_within_5_percent=grid_change <= .05,
                               coupling_step_relative_change=clock_change, coupling_step_within_5_percent=clock_change <= .05),
    physical_calibration_performed=False, metrics=metrics)
(OUTPUT / 'acceptance.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
for name in [RUN_NAME, (PREFIX+'Fine')]:
    shutil.copy2(SOURCE / name / 'final.png', OUTPUT / f'{name}.png')
print(json.dumps(report, indent=2))
if not report['functionality_passed']:
    raise SystemExit('Functional acceptance failed; inspect named checks. Numerical convergence is reported separately.')
