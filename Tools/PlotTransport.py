"""Plot saved measurements and actual collider geometry; no particle manipulation."""
import argparse
import csv
from itertools import product
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon, Circle
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('cases', nargs='+')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'Docs/TransportV3'
out.mkdir(parents=True, exist_ok=True)

def records(path):
    with path.open(encoding='utf-8-sig') as f:
        return list(csv.DictReader(f))

fig, axs = plt.subplots(2, 2, figsize=(11, 7), layout='constrained')
for name in args.cases:
    folder = root / 'Saved/TransportV3' / name
    rows = records(folder / 'MATERIAL_PATH.csv')
    for ax, key, title in zip(axs.flat,
            ['troughSeenKg', 'troughKg', 'tailCrossKg', 'rearSettledKg'],
            ['Entered upper trough', 'Currently in trough', 'Crossed actual tail edge', 'Deposited behind machine']):
        if key in rows[0]:
            ax.plot([float(r['t']) for r in rows], [float(r[key]) for r in rows], label=name)
        ax.set(title=title, xlabel='Physical time [s]', ylabel='Mass [kg]')
        ax.spines[['top', 'right']].set_visible(False)
        ax.grid(alpha=.15)
axs[0, 0].legend(fontsize=8)
fig.savefig(out / 'material-path.png', dpi=180)
plt.close(fig)

for name in args.cases:
    folder = root / 'Saved/TransportV3' / name
    geom_path = folder / 'TransportGeometry.csv'
    if not geom_path.exists():
        continue
    geometry = records(geom_path)
    floor = geometry[2]
    vector = lambda r, keys: np.array([float(r[k]) for k in keys])
    forward = vector(floor, ['axx', 'axy', 'axz'])
    up = vector(floor, ['azx', 'azy', 'azz'])
    origin = vector(floor, ['x', 'y', 'z']) - forward * (-.62 + float(floor['hx'])) - up*(.03 if floor['motion']=='4' else 0)
    fig, ax = plt.subplots(figsize=(12, 5), layout='constrained')
    for r in geometry:
        if abs(float(r['y'])) > .03:
            continue  # Actual central section, not an opaque projection of side walls.
        center = vector(r, ['x', 'y', 'z'])
        a = vector(r, ['axx', 'axy', 'axz']) * float(r['hx'])
        b = vector(r, ['azx', 'azy', 'azz']) * float(r['hz'])
        corners = [center+a+b, center+a-b, center-a-b, center-a+b]
        poly = [[np.dot(p-origin, forward), np.dot(p-origin, up)] for p in corners]
        if r.get('shape')=='1':
            ax.add_patch(Circle((np.dot(center-origin,forward),np.dot(center-origin,up)),float(r['hx']),facecolor='#f8b452',edgecolor='#34404c',alpha=.45))
            continue
        ax.add_patch(Polygon(poly, facecolor='#f8b452' if int(r['motion']) else '#a2acb6',
                             edgecolor='#34404c', linewidth=.55, alpha=.45))
    points = records(folder / 'TransportParticles.csv')
    points = [p for p in points if abs(float(p['y'])) < .15]
    colors = ['#2563eb' if int(p['history']) & 64 else '#008b80' if int(p['history']) & 1
              else '#df8e24' if int(p['history']) & 4 else '#b7bec5' for p in points]
    ax.scatter([float(p['x']) for p in points], [float(p['z']) for p in points],
               c=colors, s=5, linewidths=0)
    ax.set(xlim=(-1.05, 1.1), ylim=(-.5, .48), xlabel='Mount-local X [m]', ylabel='Mount-local Z [m]',
           title=f'{name}: final material samples and audited target-pose solids')
    ax.set_aspect('equal')
    ax.spines[['top', 'right']].set_visible(False)
    ax.text(.01, .98, 'Grey: bed | Orange: head history | Green: trough history | Blue: rear deposition\n'
            'Solids are the audited target pose; final rotor phases may differ. Dots are MPM samples, not sand grains.',
            transform=ax.transAxes, va='top', fontsize=8)
    fig.savefig(out / f'{name}-section.png', dpi=180)
    plt.close(fig)
print(out)
