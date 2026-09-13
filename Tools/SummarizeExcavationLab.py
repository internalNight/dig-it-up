"""Generate auditable tables and plots from completed local Unreal runs."""
import csv
import hashlib
import math
import json
import re
import shutil
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'Saved/ExcavationLab'
OUT = ROOT / 'Docs/ExcavationLabEvidence'
OUT.mkdir(parents=True, exist_ok=True)
for pilot in ['SoilUnconfinedPilot','SoilBeforeMeanFix','HelixBeforeHandednessFix','DriveBeforeBrakeFix','HoldingBeforeBrakeFix']:
    for path in (DATA/pilot).glob('*.csv'):
        target=OUT/'Pilots'/pilot
        target.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,target/path.name)
plt.rcParams.update({'font.family': 'Microsoft YaHei', 'axes.unicode_minus': False,
                     'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False,
                     'svg.hashsalt': 'excavation-lab-v2'})

def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))

def diagnostics(text, prefix):
    return [dict(re.findall(r'(\w+)=([^ ]+)', m))
            for m in re.findall(prefix+r' ([^\r\n]+)', text)]

def write_csv(path, rr):
    if not rr:
        return
    fields = list(dict.fromkeys(k for r in rr for k in r))
    with path.open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rr)

all_data = {}
provenance = []
for suite in ['Soil', 'Sensitivity', 'Heads', 'Drive', 'Holding']:
    folder = DATA/suite
    summary = folder/'results.json'
    if not summary.exists():
        continue
    cases = json.loads(summary.read_text(encoding='utf-8-sig'))
    if isinstance(cases, dict):
        cases = [cases]
    for case in cases:
        name = case['case']
        path = folder/(name+'.csv')
        rr = rows(path)
        logpath = folder/f'Lab_{suite}_{name}.log'
        log = logpath.read_text(encoding='utf-8-sig', errors='replace')
        config = re.findall(r'(?:SOIL_CONFIG|HEAD_CONFIG|MPM quality:)[^\r\n]*', log)
        hh = diagnostics(log, 'HEAD_DIAGNOSTIC')
        ff = diagnostics(log, 'FEED_CONTROL')
        cc = diagnostics(log, 'CHASSIS_COUPLING')
        for r in rr:
            assert abs(float(r['massError'])) <= .001 and int(r['nonfinite']) == 0
        all_data[suite, name] = {'rows': rr, 'head': hh, 'feed': ff, 'coupling': cc}
        shutil.copy2(path, OUT/f'{suite}_{name}.csv')
        for suffix, extra in [('head', hh), ('feed', ff), ('coupling', cc)]:
            write_csv(OUT/f'{suite}_{name}_{suffix}.csv', extra)
        provenance.append({'suite': suite, 'case': name, 'flags': case['flags'],
                           'runUtc': case.get('runUtc', 'see log'), 'config': config,
                           'rawLog': str(logpath.relative_to(ROOT)),
                           'logSha256': hashlib.sha256(logpath.read_bytes()).hexdigest(),
                           'csvSha256': hashlib.sha256(path.read_bytes()).hexdigest()})

source_files = list((ROOT/'Plugins/SandSimulation/Source').rglob('*.cpp'))
source_files += list((ROOT/'Plugins/SandSimulation/Source').rglob('*.h'))
source_files += list((ROOT/'Plugins/SandSimulation/Shaders').rglob('*.usf'))
source_files += list((ROOT/'Plugins/SandSimulation/Shaders').rglob('*.ush'))
manifest = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
(OUT/'provenance.json').write_text(json.dumps({'runs': provenance, 'finalSourceHashes': manifest}, ensure_ascii=False, indent=2), encoding='utf-8')
automation_path=DATA/'Automation/index.json'
automation=None
if automation_path.exists():
    result=json.loads(automation_path.read_text(encoding='utf-8-sig'))
    automation={k:result[k] for k in ['reportCreatedOn','succeeded','succeededWithWarnings','failed','notRun','inProcess']}
    automation['tests']=[{k:t[k] for k in ['fullTestPath','state']} for t in result['tests']]
    (OUT/'automation-summary.json').write_text(json.dumps(automation,ensure_ascii=False,indent=2),encoding='utf-8')

def last(suite, name):
    return all_data[suite, name]['rows'][-1]

doc = ['# 第二轮数值结果', '',
       '由 `Tools/SummarizeExcavationLab.py` 从本机原始文件生成。每个工况为一次运行，无重复试验置信区间；小数为日志精度。本文所有数值均是未标定的数值试验结果。', '',
       '## 小土槽', '',
       '|工况|平均绝对水平力/N|水平走刀功/J|末态动能/J|末态质心高度/m|',
       '|---|---:|---:|---:|---:|']
for (suite, name), data in all_data.items():
    if suite in ('Soil', 'Sensitivity'):
        r = data['rows'][-1]
        doc.append(f"|{name}|{float(r['meanAbsFxN']):.2f}|{float(r['workJ']):.2f}|{float(r['kineticJ']):.5f}|{float(r['comZ']):.5f}|")

if ('Soil', 'Blade60Fine') in all_data:
    fine = float(last('Soil', 'Blade60')['meanAbsFxN'])
    coarse = float(last('Soil', 'Blade60Fine')['meanAbsFxN'])
    change = (fine/coarse-1)*100
    doc += ['', f'将物理网格从 3.125 cm 加密到 2.5 cm，60°平均绝对水平力变化 {change:+.1f}%（以3.125 cm结果为分母）；默认内部子步也随网格变化。这不是纯网格误差隔离试验，尚不能宣布收敛。']
if ('Sensitivity', 'TimeHalf') in all_data:
    value = float(last('Sensitivity', 'TimeHalf')['meanAbsFxN'])
    base = float(last('Soil', 'Blade60')['meanAbsFxN'])
    doc += [f'固定 2.5 cm 网格、内部时间步减半，60°平均绝对水平力相对基准变化 {(value/base-1)*100:+.1f}%。这检查内部积分，尚未改变每次工具/底盘反馈间隔。']
doc += ['', '刀角试验保持标称切深和走刀速度一致；平均绝对力与有符号水平功是不同指标。未标定结果不提供真实砂的最优角。细网格仍只有一物质点/格；刀板厚度与最细网格同阶，接触和每格采样数量仍需检查。', '',
        '![刀角、网格与土性敏感性](ExcavationLabEvidence/soil-results.png)', '',
        '## 固定送料台工作头', '',
        '|构型|累计过尾部质量/kg|末态转速/rpm|末态转矩/N·m|', '|---|---:|---:|---:|']
for (suite, name), data in all_data.items():
    if suite == 'Heads':
        r = data['rows'][-1]
        doc.append(f"|{name}|{float(r['delivered']):.3f}|{float(r['rpm']):.2f}|{float(r['torque']):.2f}|")
doc += ['', '初始供料89.6 kg，固定送料台、强制送料机构，时长16.8 s。累计量按物质点身份去重，表示曾经过槽内再越过尾部，不保证已落入实体收集容器；不是有效开挖量、净流率或整机效率。停止对照也停止输送链。单一末态低转速不能代表全程堵转占时。', '',
        '![工作头送料与底盘位移](ExcavationLabEvidence/machine-results.png)', '',
        '## 底盘与进给', '',
        '|工况|末态三维位移/m|末态水平位移/m|末态速度/m·s⁻¹|累计过尾部质量/kg|', '|---|---:|---:|---:|---:|']
for (suite, name), data in all_data.items():
    if suite in ('Drive', 'Holding'):
        h = data['head'][-1]
        d = data['rows'][-1]
        doc.append(f"|{name}|{h['displacementM']}|{h.get('horizontalDisplacementM', '未记录')}|{h['chassisSpeedMps']}|{d['delivered']}|")
doc += ['', 'Drive三组包含主动油门，不叫滑移量；有限牵引改变了油门到速度的映射，Controlled还改变切深，不能把三组位移差全部归因于抓地力。Holding三组无油门、持续制动、固定安装角；水平位移仍包含初始化后的支承调整，不等于稳态滑转率。', '',
        '旧材料用于Drive的Legacy/Limited/Controlled及Holding，新干砂材料用于Soil/Sensitivity/Heads。ControlledDry另外检查快捷入口的组合模式，不能与Legacy的差异归因于单一底盘改动。完整沙箱出渣未通过的工况必须保留为失败。', '',
        '|底盘工况|日志采样最大原始合力/N|日志采样最大滤后合力/N|', '|---|---:|---:|']
for (suite,name),data in all_data.items():
    if suite not in ('Drive','Holding') or not data['coupling']: continue
    norms = lambda prefix: [math.sqrt(sum(float(r[prefix+a+'N'])**2 for a in ['x','y','z'])) for r in data['coupling']]
    doc.append(f"|{name}|{max(norms('rawF')):.1f}|{max(norms('filteredF')):.1f}|")
doc += ['', '上表是稀疏日志采样最大值，不是真实峰值；原始值包含数值接触修正，未经实验标定。两列分别取最大值，不一定来自同一时刻。它们用于揭示反力传递处理，不应作为真实掘进机载荷。', '',
        '先导失败与制动修正前的CSV在 `ExcavationLabEvidence/Pilots`，不计入本页最终工况数。它们用于保留失败与计量修正历史，不参与最终均值比较。', '',
        '## 证据与门槛', '',
        f'本汇总包含 {len(all_data)} 个工况。逐次采样质量误差≤0.001 kg，非有限物质点为0；这是软件健康检查，不代表力、能量或真实砂标定通过。', '',
        '逐工况CSV、派生诊断与文件哈希位于本页同级的 `ExcavationLabEvidence`；原始日志与PNG保存在 `Saved/ExcavationLab`。源文件哈希记录汇总时源码，不应误解为每个历史运行都使用该最终源码；运行配置与保留的先导试验在主报告中说明。', '',
        '[返回模型、文献和复现说明](ExcavationLabV2.md)', '']
(ROOT/'Docs/ExcavationLabResults.md').write_text('\n'.join(doc), encoding='utf-8')
if automation:
    with (ROOT/'Docs/ExcavationLabResults.md').open('a',encoding='utf-8') as f:
        f.write(f"\nUnreal自动化：{automation['succeeded']}项通过、{automation['failed']}项失败、{automation['notRun']}项未运行；逐项状态见 `ExcavationLabEvidence/automation-summary.json`。测试包括共享GPU本构更新、旋转接触运动学与有限牵引/制动预算。\n")

fig, ax = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
for name in ['Blade30', 'Blade60', 'Blade90']:
    if ('Soil', name) not in all_data: continue
    rr = [r for r in all_data['Soil', name]['rows'] if 1 < float(r['t']) < 8]
    ax[0,0].plot([(float(r['t'])-1)*.05 for r in rr], [float(r['FxN']) for r in rr], label=name.replace('Blade','')+'°')
ax[0,0].set(xlabel='水平走刀位移 / m', ylabel='采样水平反力 / N', title='刀角：相同标称切深 0.10 m')
ax[0,0].legend()
names = [n for n in ['Blade30','Blade60','Blade90','Blade60Fine','Blade60Dilation'] if ('Soil',n) in all_data]
ax[0,1].bar(names, [float(last('Soil',n)['meanAbsFxN']) for n in names], color='#237f92')
ax[0,1].tick_params(axis='x', rotation=25)
ax[0,1].set(ylabel='平均绝对水平力 / N', title='角度、网格与剪胀：一次运行')
for n in ['Idle25', 'Idle50']:
    if ('Soil',n) not in all_data: continue
    rr=all_data['Soil',n]['rows']
    ax[1,0].plot([float(r['t']) for r in rr], [1000*(float(r['comZ'])-.2) for r in rr],label=n)
ax[1,0].set(xlabel='MPM时间 / s', ylabel='质心高度变化 / mm', title='静置：相同质量与填砂尺寸')
ax[1,0].legend()
names=[n for n in ['Phi30','Phi40','Depth05','Depth15','TimeHalf'] if ('Sensitivity',n) in all_data]
ax[1,1].bar(names,[float(last('Sensitivity',n)['meanAbsFxN']) for n in names],color='#cb803b')
ax[1,1].set(ylabel='平均绝对水平力 / N',title='独立变量敏感性；其余沿用60°基准')
fig.suptitle('未标定数值试验 · 不作为工程选型值', fontsize=16)
fig.savefig(OUT/'soil-results.png', dpi=160)
fig.savefig(OUT/'soil-results.svg', metadata={'Date':None})
plt.close(fig)

fig, ax=plt.subplots(1,2,figsize=(12,4.8),constrained_layout=True)
names=[name for suite,name in all_data if suite=='Heads']
values=[float(last('Heads',n)['delivered']) for n in names]
bars=ax[0].bar(names,values,color='#237f92')
ax[0].bar_label(bars,fmt='%.2f',padding=3)
ax[0].tick_params(axis='x',rotation=30)
ax[0].set(ylabel='累计过尾部质量 / kg',title='固定送料台：不等于完整机器效率',ylim=(0,max(values+[1])*1.15))
for (suite,name),data in all_data.items():
    if suite not in ('Drive','Holding'):continue
    hh=data['head']
    ax[1].plot([float(r['t']) for r in hh],[float(r['displacementM']) for r in hh],label=name,linestyle='--' if suite=='Holding' else '-')
ax[1].set(xlabel='MPM时间 / s（与底盘时钟未统一）',ylabel='距初始位置的三维位移 / m',title='主动行驶与无油门保持分别解释')
ax[1].legend(fontsize=8)
fig.suptitle('原型性能筛查 · 保留堵转与零出料结果',fontsize=15)
fig.savefig(OUT/'machine-results.png',dpi=160)
fig.savefig(OUT/'machine-results.svg', metadata={'Date':None})
plt.close(fig)
for path in OUT.glob('*-results.svg'):
    path.write_text('\n'.join(line.rstrip() for line in path.read_text(encoding='utf-8').splitlines())+'\n',encoding='utf-8')
print(f'Wrote report, evidence and plots for {len(all_data)} cases to {OUT}')
