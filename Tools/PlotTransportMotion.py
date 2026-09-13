"""Plot measured chassis motion, actuator response and loads from saved runs."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
parser=argparse.ArgumentParser()
parser.add_argument('cases',nargs='+')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
fig,axs=plt.subplots(2,2,figsize=(11,7),layout='constrained')
for name in args.cases:
    folder=root/'Saved/TransportV3'/name
    read=lambda f:list(csv.DictReader((folder/(f+'.csv')).open(encoding='utf-8-sig')))
    pose=read('HEAD_DIAGNOSTIC')
    feed=read('FEED_CONTROL')
    for ax,rows,key in [(axs[0,0],pose,'signedAdvanceM'),(axs[0,1],pose,'mountCm'),(axs[1,0],pose,'chassisZm'),(axs[1,1],feed,'fraction')]:
        ax.plot([float(r['t']) for r in rows],[float(r[key]) for r in rows],label=name)
for ax,title,ylabel in zip(axs.flat,['Signed forward displacement','Physical lift position','Chassis elevation','Load-limited feed command'],['Distance [m]','Mount height [cm]','World height [m]','Fraction']):
    ax.set(title=title,xlabel='Physical time [s]',ylabel=ylabel)
    ax.grid(alpha=.15)
    ax.spines[['top','right']].set_visible(False)
axs[0,0].axhline(0,color='#555555',linewidth=.6)
axs[0,0].legend(fontsize=8)
fig.savefig(root/'Docs/TransportV3/motion-control.png',dpi=180)
