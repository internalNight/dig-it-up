# 掘进试验平台：构型、干砂与底盘第二轮

2026-09-13；承接 [第一轮研究](ExcavationDesignStudy.md) 和 [历史原型试验](HeadVariantValidation.md)。本轮从 `574a7b9` 开始，分支 `codex/excavation-lab-v2`。这是一套可以运行和复查的数值试验平台，尚不是经过实物砂土标定的工程预测软件。

本轮已把讨论转成五种实体接触工作头和独立试验：螺旋修改后固定台出料6.40 kg、停机0；斗轮能出料但出现堵转。刀角30/60/90°的平均绝对水平力约124/208/177 N，结果非单调；同一60°刀具切深5/10/15 cm约90/208/351 N，浅切对当前负载影响明显。

真实性判断仍是不足：网格从3.125 cm到2.5 cm时平均力变化27.3%，固定网格时间步减半又变化18.8%。底盘无油门保持，最终有限制动模式水平位移约0.410 m，旧模式约0.296 m，有限模式停刀约0.005 m；新模式尚未优于旧辅助保持。下一优先项是接触修正的冲量/能量审计、统一底盘与砂体时钟、实物标定，之后再做机械效率排名。

## 1. 现在可以操作的内容

- 五种接触驱动工作头：Paddle 拨轮、Chevron 分段斜列滚筒、BucketWheel 八个 L 形兜砂单元、Spoke 轴向辐条、Helix 24 段斜板螺旋。运动表面速度与可见部件使用同一运动学，接触的是原有 MPM 物质点，掘进机不使用挖斗携砂绑定。
- `DryCorotated` 无黏聚干砂研究预设：应力共旋更新、非关联 Drucker–Prager 返回映射及可生效的剪胀角。旧材料路径保留，便于回归比较。
- 独立小土槽：静置、平板切削、释放砂柱；角度、切深、速度、φ、ψ、界面摩擦和内部子步可通过参数改变；导出力、功、质量、动能、质心及展宽。
- 可选有限牵引与进给辅助：共享纵横向摩擦预算，过载减小正向进给并抬高工作头；HUD 显示进给百分比、牵引预算和目标速度。

快捷入口：`PlayExcavationLab.cmd` 启动干砂预设、有限牵引和进给辅助；`PlayHeadBucketWheel.cmd` 查看斗轮原型。进入后 T 启停、G 反转、Q/E 抬降、W/S 行驶、空格制动、C 切换视角。所有入口调用本机 UE 编辑器，不是新打包的独立发行版。

[五种刀头与土槽的实际引擎截图](ExcavationHeadGallery.md) 可用于检查本轮做出的几何与界面。

```powershell
Tools/PlayMachines.ps1 -Head Chevron -Material DryCorotated -Traction -FeedControl
Tools/PlayMachines.ps1 -Head Helix -Bench -InspectHead -Material DryCorotated
Tools/PlayMachines.ps1 -Head BucketWheel -Bench -Material DryCorotated -Phi 35 -Dilation 5
Tools/TestExcavationLab.ps1 -Suite Soil
Tools/TestExcavationLab.ps1 -Suite Sensitivity
Tools/TestExcavationLab.ps1 -Suite Heads
Tools/TestExcavationLab.ps1 -Suite Drive
Tools/TestExcavationLab.ps1 -Suite Holding
Tools/TestExcavationLabAutomation.ps1
```

不指定质量档时，固定台使用 2.5 cm，全沙箱使用 5 cm。Fine 为 3.125 cm。显示表面体素不是这些物理网格尺寸。`-InspectHead` 是标记为 CUTAWAY 的显示剖视，隐藏罩壳仍有物理接触。

可编辑的 [场景—构型—指标矩阵](ExcavationScenarioMatrix.csv) 和 [实物标定记录空表](ExcavationCalibrationTemplate.csv) 一并提供。空表没有填入虚构实验数据。

## 2. 构型选择应包含配套系统

| 构型 | 实物参考与推荐任务依据 | 游戏中状态 | 进一步比较必须配套 |
|---|---|---|---|
| 拨轮/兜砂轮 | Vermeer 对软土开沟提供斗轮附件；TAKRAF 大型斗轮按台阶与物料条件配置 | 拨轮基线 + 新八斗近似原型 | 斗容、卸料位置、导料、有效切入深度与堵转 |
| 横轴斜列刀鼓 | 第一轮 Komatsu/Vermeer 资料对应连续切削、表层开采等任务 | 八片错列斜板；未复现破岩截齿 | 齿距/相位、每转进给、受力峰值、煤岩与干砂分开 |
| 螺旋工作头 | 第一轮 American Augers 与螺旋输送论文 | 24 块倾斜盒形叶片组成一圈，轴向相位与板面倾角一致；仍非连续 CAD 螺旋 | 套筒、间隙、迎料与出口；固定台失败不得用空转当成功 |
| 圆盘/辐条工作头 | 第一轮 Herrenknecht EPB 资料 | 只有辐条接触原型，没有完整盾构刀盘 | 土仓、开口、排渣、掌子面支护与推进反力支承 |
| 链式开沟、对向斗鼓 | 第一轮厂家资料与 NASA RASSOR | 文献候选，未在本轮做完整机器 | 独立闭环刀链；或两套电机与合力/合力矩平衡 |

新增厂家依据：[Vermeer 斗轮与岩轮附件说明](https://protips.vermeer.com/underground/2023/01/10/rockwheel-and-bucket-wheel-attachments-deliver-versatility-to-your-vermeer-trencher/)、[TAKRAF 斗轮产品](https://www.takraf.com/portfolio/detail/bucket-wheel-excavators/)。它们支持按任务和物料选配，不能提供一个普遍有效的“内摩擦角—最佳刀头”查表。

建议最终做两层比较：第一层保持同一底盘/安装/出料位置，研究换头的影响；第二层给每种构型配适合自己的排渣和支承，比较同等总功率、机重或任务资源下的整机效果。横轴头现有输送槽的优势不应被解释为该类产品普遍优于轴向头。

推进反力的来源也属于整机构型。ITA工作组的[TBM推力技术报告](https://www.societaitalianagallerie.it/public/files/WG%202%20e%20WG%2014.pdf)区分了撑靴抵住岩体与单护盾顶推管片的支承方式。把大刀盘装到自由履带上，并没有同时复现这些反力条件。游戏中的支腿、撑靴或锚固若要算物理改进，也必须由接触与土体承载提供反力，不能仅把车身锁在世界坐标。

## 3. 新补充的文献及实际用途

第一轮六篇文献继续保留。以下补充专门对应材料真实性、斗轮受力与牵引验证；不是系统综述，不以引用数代替证据。

| 来源 | 本轮读取范围 | 可迁移到项目的内容 | 不能直接迁移 |
|---|---|---|---|
| Fern & Soga (2016), *The role of constitutive models in MPM simulations of granular column collapses*, Acta Geotechnica 11:659–678，DOI 10.1007/s11440-016-0436-x | 出版商全文 HTML，作者机构题录 | 初始密度、本构软化、耗能都会影响破坏和堆积；同时验启动、流动与沉积 | 不能仅靠堆积角或换 MPM 算法名证明准确，也不能把我们的固定 ψ 模型叫 NorSand |
| *Investigation on cutting resistance characteristic of bucket wheel excavator using DEM and DOE methods* (2021), Simulation Modelling Practice and Theory 111:102339，DOI 10.1016/j.simpat.2021.102339 | 出版商题录及摘要，全文受限；作者表未在本轮完整核验 | 先用切削阻力试验验证，再用 DOE 分析工况；斗轮需要时变载荷指标 | 不采用未读全文中的系数、最优角与砂参数 |
| Mocera, Somà & Nicolini (2020), *Grousers Effect in Tracked Vehicle Multibody Dynamics with Deformable Terrain Contact Model*, Applied Sciences 10:6581，DOI 10.3390/app10186581 | 机构题录、公开摘要与出版商索引片段 | 履刺、沉陷和土体条件共同影响履带接地；光滑 μN 只是初级近似 | 文中某个履刺尺寸不能直接作为游戏最优尺寸 |
| Schepelmann 等，NASA/TM-20250006958 (2025), *An Overview of Tire-Ground Contact Modeling Approaches for Surface Mobility Applications* | 作者公开 PDF；重点查阅测试指标与建模选择 | 牵引试验同时测法向载荷、牵引、速度/滑转、沉陷及接触条件；标定工况需覆盖用途 | 轮胎模型不是完整履带模型，本游戏目标速度不等于真实履带带速 |
| Miedema (2013), *Dredging Processes I: The Cutting of Sand, Clay & Rock—Theory* | TU Delft 书目/摘要；这是书，不计作期刊论文 | 分开干砂、饱和砂、黏土与岩石，明确刀角定义，用力/功率/比能描述切削 | 不把水下切削或大刀角土楔系数直接移植为当前小土槽基准 |

来源链接：[Fern & Soga 全文](https://link.springer.com/article/10.1007/s11440-016-0436-x)，[斗轮 DEM/DOE 论文](https://www.sciencedirect.com/science/article/abs/pii/S1569190X21000551)，[履刺研究机构记录](https://iris.polito.it/handle/11583/2847761)，[NASA 技术报告作者公开版](https://aschepelmann.github.io/papers/NASATM2025.pdf)，[TU Delft 切削理论书目](https://repository.tudelft.nl/record/uuid:2ad35f71-13ae-40db-9772-7ecfd76aca35)。

对应场景至少记录：ρ（体积密度）、φ（土内摩擦）、ψ（剪胀）、c（黏聚）、μ（土—钢界面）、粒径与含水率、刀宽/切深、速度、机器质量/重心及支承。松散易回流的材料更重视收料与防回填；密实材料更重视切削峰值、剪胀和浅切进给。这是待试验检验的设计假设，φ 本身不能替代密实度。

## 4. 干砂模型到底完善了什么

新预设采用 ρ=1600 kg/m³、φ=35°、c=0、ψ=0、E=250 kPa、ν=0.2、土—工具 μ=0.5、速度阻尼 0.15/s，关闭旧压实硬化。这些是研究起点，不是某批真实砂的测值。`-SandPhi`、`-SandDilation`、`-SandToolMu` 可独立覆盖。

`SandMaterial.ush` 共旋更新旧应力，再加入对称速度梯度弹性增量。采用压缩 p 为正、q 为等效偏应力的 f=q−Mp−C；非关联塑性势使用 Mψ，正 ψ 对受约束塑性剪胀产生压力修正。GPU 检查覆盖应力纯旋转、静水应力保持、ψ=0 返回屈服面、ψ>0 压力修正。

边界：这是共旋率式亚弹性 DP 原型，不是完整有限应变弹塑性模型；没有更新每个物质点的变形梯度/真实孔隙比，也没有临界状态密度演化、软化和颗粒碰撞耗散。正 ψ 已进入应力求解，但不代表已准确模拟密砂的体积变化。旧保护逻辑、接触穿透修正与阻尼仍需能量/动量预算。GPU 算术测试通过只说明指定算例，不证明一般大变形客观性或实验准确性。

小土槽长 1.8 m、宽 0.8 m、砂深 0.4 m，边界约束保持原有接触实现。静置和刀具工况整槽填充；坍塌工况只填 0.7×0.4×0.4 m 的砂柱。不同网格边缘采用裁切体积，避免初始总质量变化混入网格比较。初始应力采用竖向自重、K₀=1−sinφ 的假定。

因此 φ 扫描也通过 K₀ 公式改变了初始侧向应力，表示该初始化假设下的综合敏感性，不是固定初始应力时只改屈服面。实物标定后应使用测得的初始状态，并另做固定 K₀ 对照。坍塌土槽是有限三维长方柱，不能直接与论文的平面应变或圆柱无量纲曲线重合比较。

平板宽 0.28 m、厚 0.025 m、长 0.30 m；角度是板面与水平面的夹角，和整机 Q/E 俯仰角不同。前 1 秒垂直入土，随后以 0.05 m/s 水平走刀 7 秒，刀尖标称切深 0.10 m。日志的 workJ 仅积分水平切削阶段 Fₓv 的有符号功，meanAbsFxN 是该阶段平均绝对水平阻力；不是整机能耗/比能，也未扣空载阻力。

第一组未约束砂柱在切削开始前已经塌开，产生虚假的近零刀力，保留于 `Saved/ExcavationLab/SoilUnconfinedPilot`，明确排除切削比较。不得把它作为低阻力构型结果。

## 5. 滑移改进与仍存在的根本限制

有限牵引模式用接地判据及 N≈max(0,mg−Fz) 估算载荷，纵向驱动和横向稳定共享 μN 预算，目标速度偏差用有限力纠正；离地时预算为零。默认 μ=0.6、行驶响应时间 0.25 s、满油门目标 0.12 m/s，均为原型设定。制动改为请求一个时间步内停止所需的冲量，再受同一摩擦预算限制，超过预算仍允许滑动。它消除了该模式中直接按帧乘系数的水平制动，但未替换高度弹簧、姿态辅助和全部角速度处理，也不是完整隐式静摩擦接触求解。

进给辅助以工作头原始水平反力和负载转矩为输入；25–65 N 的反力区间、80–180 N·m 的转矩区间线性减小进给，过载抬头。阈值是调试值；与当前 12 kg 机体的量纲一致性还需要整机参数设计和实物标定。它改变切深，因此比较时必须同时看有效开挖量，不能只看位移变小。

`SandTrackMu`是独立的经验牵引参数，不等于tanφ；当前显式牵引预算没有建立土体φ、沉陷与剪切位移的完整关系。因此不能用本轮“改变φ后的整机表现”直接验证真实地基牵引能力。

**尚未解决的核心限制：**Chaos 底盘与异步 MPM 仍非统一物理时钟；砂反力仍经过约 15 N 量级限幅和平滑、施加在质心，完整反力矩尚未传递。因此现在只能称“有限牵引与防过载体验对照”，不能称真实牵引—滑转模型已经验收。不能取消限幅后靠增加机重掩盖不稳定。

Drive 对照含主动油门，总位移不等于滑移；Holding 对照固定安装俯仰 −8°、油门为零、制动指令持续，比较刀头转动与停止，观察被动位移。原日志记录三维距初始位置的距离，包含沉陷/姿态变化；Holding 新增独立水平位移和底盘世界时钟记录。没有真实履带带速，不报告滑转率。挖掘机旧携砂机制仍存在，可以用 `-SandDisableBucketCarry` 做后续同口径对照，不能由此宣布挖掘机物理更准确。

## 6. 验证顺序与交付门槛

1. 先固定底盘，在小土槽收敛网格、子步、物质点数和边界；本轮运行属于数值敏感性检查。
2. 取得同一批实物干砂的密度/含水/级配，做直剪或三轴、土—钢摩擦、板贯入和坍塌；校准 φ、ψ、刚度和密度状态，保留未参与拟合工况。
3. 带水平/竖向测力的单刀土槽，分别改变角度和切深；同时比较力—位移与土体扰动，不只比较终态。
4. 先实现并验证双向线/角动量耦合，再做牵引台、坡面、沉陷、被动滑移、载荷转移；最后比较完整机器的有效开挖、净收集、比能和任务时间。

项目暂定数值门槛可用均值随加密变化<5%、峰值<10%，但不是行业标准；本轮若未达到应标为不收敛。每种材料/刀具的实验需重复，GPU 试验也需独立重复后才讨论差异是否可靠。粒径与刀缝同阶的筛分、架桥和堵塞可另用局部 DEM 对照，避免让连续介质 MPM 承担未建模的颗粒细节。

## 7. 本机实测结果

最终汇总由 `Tools/SummarizeExcavationLab.py` 从原始 CSV/日志生成，见 [数值结果与图](ExcavationLabResults.md)。自动化报告、原始日志、逐工况 CSV 和截图位于 `Saved/ExcavationLab`。任何质量检查通过都不替代材料、力、能量或整机的物理验收。

最终27个数值工况的质量/有限性检查完成，14项自动化通过。Drive的四种完整场景模式（含新干砂组合入口）累计出料均为0，整机连续开挖这一门槛明确未通过。图表脚本在本机Python 3.11、matplotlib 3.10.9运行；生成器读取保存数据，不会启动或改动仿真。当前实际Python位于 `%LOCALAPPDATA%\Programs\Python\Python311\python.exe`。

中间土槽日志的均值分母曾包含走刀结束后的等待时间；已改为累计实际走刀时长，并重新运行 Soil 组。旧版保留在 `SoilBeforeMeanFix`，不混用均值。螺旋旋向修正前的零出料记录保留在 `HelixBeforeHandednessFix`。其它刀头的本轮结果取自修正前构建，但这些修正不改变它们的物理路径；截图取最终构建。

第一次有限牵引把制动也设为0.25 s响应，HoldLimited末态水平移动1.2999 m，高于HoldLegacy的0.2957 m，未达到保持目的。因此改成受预算限制的单步制动冲量后重跑Drive/Holding；失败记录保留于 `DriveBeforeBrakeFix` 与 `HoldingBeforeBrakeFix`。这次修正针对数值控制，不意味着土体牵引曲线已标定。

新增GPU参数后，旧砂柱自动化入口曾遗漏初始化新字段，造成一项回归失败；补齐该入口的参数与缓冲区后，本机14项自动化通过、0项失败，没有降低原测试的展宽阈值。旧失败报告保留于 `AutomationBeforeInitFix`。自动化脚本检查逐项JSON结果，不将编辑器退出码0当作测试全部通过。
