"""Summarize measured transport runs without starting or altering the simulator."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Saved/TransportV3'
OUTPUT = ROOT / 'Docs/TransportV3'
OUTPUT.mkdir(parents=True, exist_ok=True)

def read(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))

rows = []
for folder in sorted(SOURCE.iterdir()):
    if not (folder / 'MATERIAL_PATH.csv').exists():
        continue  # Failed geometry/startup runs remain in their raw logs.
    material = read(folder / 'MATERIAL_PATH.csv')[-1]
    machine = read(folder / 'ROADHEADER.csv')[-1]
    pose = read(folder / 'HEAD_DIAGNOSTIC.csv')[-1]
    config = json.loads((folder / 'run.json').read_text(encoding='utf-8-sig'))
    rows.append(dict(
        case=folder.name, seconds=material['t'], rig=material['rig'],
        entered_trough_kg=material['troughSeenKg'], in_trough_kg=material['troughKg'],
        legacy_outlet_kg=material['outletKg'], tail_cross_kg=material.get('tailCrossKg',''),
        rear_settled_kg=material.get('rearSettledKg',''), signed_advance_m=pose.get('signedAdvanceM', ''),
        displacement_m=pose['horizontalDisplacementM'],
        final_mass_error_kg=machine['massError'],
        nonfinite=machine['nonfinite'], carried=machine['carried'],
        binary_sha256=config.get('binaryHash', 'not recorded in early pilot'),
        arguments=config['arguments'].replace(str(ROOT), '${REPO}')))

with (OUTPUT / 'all-runs.csv').open('w', encoding='utf-8-sig', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)

lines = ['# Transport development measurements', '',
         'Pilot runs include failed designs and different builds/durations. This is an evidence ledger, not a controlled ranking.',
         'Rig=1 uses a prescribed carriage in the full sandbox. Rig=0 alone does not establish valid free driving.',
         'Legacy outlet uses the original -65 cm crossing gate; see the report for its limitations.', '',
         '| Case | Time s | Rig | Entered trough kg | In trough kg | Legacy outlet kg | Rear settled kg | Signed advance m |',
         '|---|---:|---:|---:|---:|---:|---:|---:|']
for r in rows:
    lines.append(f"| {r['case']} | {r['seconds']} | {r['rig']} | {float(r['entered_trough_kg']):.3f} | {float(r['in_trough_kg']):.3f} | {float(r['legacy_outlet_kg']):.3f} | {r['rear_settled_kg'] or 'not recorded'} | {r['signed_advance_m'] or 'not recorded'} |")
(OUTPUT / 'measurements.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
print(f'Wrote {len(rows)} completed run records to {OUTPUT}')
