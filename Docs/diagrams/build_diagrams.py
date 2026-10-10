"""Original portfolio diagrams. SVG is dependency-free; Pillow exports the paired PNGs.

Usage: python build_diagrams.py
Fonts: Windows Microsoft YaHei or Linux Noto Sans CJK; no fonts are redistributed.
"""
from pathlib import Path
from html import escape
import math
import json

ROOT = Path(__file__).resolve().parent
W, H = 1600, 1000
C = dict(bg="#14191d", panel="#1c2429", line="#43515a", ink="#f5eddd",
         muted="#b1b9bb", gold="#dfbc77", blue="#86bfdb", red="#dc958c",
         green="#90c9b4", faded="#707b82")
FONT_STACK = "Microsoft YaHei, Noto Sans CJK SC, PingFang SC, sans-serif"
try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

def font(size, bold=False):
    names = ([r"C:\Windows\Fonts\msyhbd.ttc"] if bold else [r"C:\Windows\Fonts\msyh.ttc"])
    names += ["/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"]
    for name in names:
        if Path(name).exists():
            return ImageFont.truetype(name, size)
    raise RuntimeError("Install Microsoft YaHei or Noto Sans CJK for PNG export")

class Canvas:
    def __init__(self, name, title, subtitle):
        self.name, self.bounds = name, []
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-labelledby="title desc">',
                    f'<title id="title">{escape(title)}</title><desc id="desc">{escape(subtitle)}。原创规则示意，非游戏截图。</desc>']
        self.im = Image.new("RGB", (W,H), C["bg"]) if Image else None
        self.draw = ImageDraw.Draw(self.im) if self.im else None
        self.rect(0,0,W,H,C["bg"],None,0)
        self.text(60,48,title,46,C["ink"],True,maxw=1420)
        self.text(60,118,subtitle,25,C["muted"],maxw=1450)
        self.line([(60,174),(1540,174)],C["gold"],2)
    def rect(self,x,y,w,h,fill=C["panel"],stroke=C["line"],radius=16):
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke or "none"}" stroke-width="2"/>')
        if self.draw:
            self.draw.rounded_rectangle((x,y,x+w,y+h),radius=radius,fill=fill,outline=stroke,width=2)
    def text(self,x,y,txt,size=25,color=C["ink"],bold=False,maxw=None):
        self.svg.append(f'<text x="{x}" y="{y+size}" font-family="{FONT_STACK}" font-size="{size}" font-weight="{700 if bold else 400}" fill="{color}">{escape(txt)}</text>')
        if self.draw:
            f = font(size,bold)
            width = self.draw.textlength(txt,font=f)
            if maxw and width > maxw+1:
                raise ValueError(f"{self.name}: text too wide: {txt!r} {width:.1f}>{maxw}")
            self.draw.text((x,y+size),txt,font=f,fill=color,anchor="ls")
            self.bounds.append(dict(text=txt,x=x,y=y,width=round(width,2),size=size))
    def line(self,pts,color=C["gold"],width=3,dash=False):
        pointstr=" ".join(f"{x},{y}" for x,y in pts)
        self.svg.append(f'<polyline points="{pointstr}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round"'+(' stroke-dasharray="9 7"' if dash else '')+'/>')
        if self.draw:
            if dash:
                for (ax,ay),(bx,by) in zip(pts,pts[1:]):
                    length=math.hypot(bx-ax,by-ay)
                    for start in range(0,int(length),16):
                        end=min(start+9,length)
                        self.draw.line((ax+(bx-ax)*start/length,ay+(by-ay)*start/length,
                                        ax+(bx-ax)*end/length,ay+(by-ay)*end/length),fill=color,width=width)
            else: self.draw.line(pts,fill=color,width=width,joint="curve")
    def arrow(self,pts,color=C["gold"],width=3,dash=False):
        self.line(pts,color,width,dash)
        (ax,ay),(bx,by)=pts[-2:]
        a=math.atan2(by-ay,bx-ax)
        ps=[(bx,by),(bx-13*math.cos(a)+6*math.sin(a),by-13*math.sin(a)-6*math.cos(a)),
            (bx-13*math.cos(a)-6*math.sin(a),by-13*math.sin(a)+6*math.cos(a))]
        self.svg.append(f'<polygon points="'+" ".join(f"{x:.2f},{y:.2f}" for x,y in ps)+f'" fill="{color}"/>')
        if self.draw: self.draw.polygon(ps,fill=color)
    def card(self,x,y,w,h,kicker,title,lines,color=C["gold"]):
        self.rect(x,y,w,h,stroke=color)
        self.text(x+22,y+18,kicker,19,color,True,maxw=w-44)
        self.text(x+22,y+51,title,29,C["ink"],True,maxw=w-44)
        for i,line in enumerate(lines): self.text(x+22,y+99+i*34,line,23,C["muted"],maxw=w-44)
    def footer(self,line1,line2):
        self.line([(60,890),(1540,890)],C["line"],2)
        self.text(60,910,line1,20,C["muted"],maxw=1480)
        self.text(60,947,line2,20,C["faded"],maxw=1480)
    def save(self):
        self.svg.append("</svg>")
        (ROOT/(self.name+".svg")).write_text("\n".join(self.svg),encoding="utf-8")
        if self.im: self.im.save(ROOT/(self.name+".png"))
        return self.bounds

def core():
    p=Canvas("01-core-loop","核心循环：让自动战斗持续产生选择","玩家负责站位、策略、构筑和挑战时机；队友承担自动感知与输出。")
    nodes=[("01 玩家决策","走位与模式",["靠近 / 拉开队友","Q 护卫 / 歼灭"]),
           ("02 协作输出","自动战斗",["前期队友火力","Lv.6 玩家自动射击"]),
           ("03 击杀收益","经验与金币",["经验推动等级","金币形成机会成本"]),
           ("04 成长入口","选择强化",["升级 / 宝箱 / 遗物","前置、互斥与叠加"]),
           ("05 局内构筑","改变战局",["弹道与元素协作","治疗、机动与风险"])]
    xs=[60,358,656,954,1252]
    for x,(k,t,ls) in zip(xs,nodes): p.card(x,220,288,183,k,t,ls)
    for x in xs[:-1]: p.arrow([(x+288,309),(x+298,309)])
    p.arrow([(1396,403),(1396,445),(204,445),(204,403)],C["gold"])
    p.rect(680,420,244,46,C["bg"],None,0)
    p.text(711,425,"重组站位与处理顺序",22,C["gold"],maxw=210)
    p.card(60,488,460,354,"资源出口","金币与探索",["普通箱：3 + 2 × 已开箱数", "锁箱：钥匙资格 → 15金 + 六选二", "钥匙：本局持有资格，不消耗", "难度祭坛：8金 → 档位L + 1", "敌HP × (1 + 0.3L)", "敌XP × (1 + 0.25L)"],C["blue"])
    p.card(570,488,460,354,"供给约束","保护选择质量",["普通升级：三选一", "一次性卡离池，可叠加卡保留", "Lv.6前排除玩家武器相关卡", "融合：满足前置后入池", "稀有度相对权重：1 / 0.75 / 0.45", "Boss遗物：每Boss固定三选一"],C["gold"])
    p.card(1080,488,460,354,"阶段压力","挑战与离场",["3 / 6 / 9分钟：Boss祭坛出现", "接近祭坛开始对应挑战", "12分钟：开启狂暴压力", "蠕虫击倒 → 归途门", "先领奖，再近门松E、重新按E", "死亡结算 / 入门感谢页"],C["red"])
    p.footer("现役规则：WaveDirector.cpp/.h · WaveDirectorRelics.cpp · GameMode.cpp · EndingPortal.cpp", "2026-10-10规则快照 · 示意图不代表固定出卡概率、完整组合平衡或运行性能；证据入口见 Docs/EVIDENCE.md")
    return p.save()

def ai():
    p=Canvas("02-companion-ai","协作AI：把护主预期拆成可解释的规则","引擎负责感知与寻路；C++ FSM决定状态，Utility决定目标，Q模式决定移动取向。")
    p.card(60,220,385,162,"输入A：世界感知","AI Perception",["视觉26m / 丢失30m"],C["blue"])
    p.card(60,417,385,162,"输入B：玩家受击","OnDamaged来源",["ICombatant活攻击者加入候选"],C["red"])
    p.arrow([(445,300),(492,300),(492,359),(535,359)],C["blue"])
    p.arrow([(445,499),(492,499),(492,414),(535,414)],C["red"])
    p.rect(535,220,575,360,stroke=C["gold"])
    p.text(560,241,"决策：0.25秒更新候选分数",21,C["gold"],True,maxw=520)
    p.text(560,287,"Score = 0.3D + 0.6T + 0.1V",28,C["ink"],True,maxw=520)
    p.text(560,332,"− 0.1 × 非现目标 − 0.35 × 无视线",26,C["ink"],maxw=520)
    p.text(560,391,"D：距队友越近越高",25,C["muted"],maxw=520)
    p.text(560,430,"T：越威胁玩家越高；正在攻击时为1",25,C["muted"],maxw=520)
    p.text(560,469,"V：损血占比，低权重避免锁死首目标",25,C["muted"],maxw=520)
    p.text(560,523,"切换惩罚抑制横跳，LOS惩罚优先可打目标",23,C["gold"],maxw=520)
    p.arrow([(1110,400),(1160,400)])
    p.rect(1160,220,380,360,stroke=C["green"])
    p.text(1183,239,"状态：Follow / Engage",21,C["green"],True,maxw=332)
    p.text(1183,286,"有活候选 → Engage",27,C["ink"],True,maxw=332)
    p.text(1183,334,"无活候选 → Follow",27,C["ink"],True,maxw=332)
    p.line([(1183,390),(1517,390)],C["line"],2)
    p.text(1183,408,"NavMesh + MoveTo",27,C["ink"],maxw=332)
    p.text(1183,452,"移动与FireTimer并行",25,C["muted"],maxw=332)
    p.text(1183,496,"基础：8m / 0.8s / 10伤",24,C["muted"],maxw=332)
    p.text(1183,536,"候选清空：停火、归队",23,C["muted"],maxw=332)
    p.card(60,630,460,208,"Q 模式：玩家选择取舍","护卫",["移动目标：玩家", "玩家收到治疗 +10%"],C["blue"])
    p.card(570,630,460,208,"Q 模式：共享评分与火力","歼灭",["移动目标：评分最优敌人", "队友伤害 +10%"],C["red"])
    p.card(1080,630,460,208,"可观察性","日志与评分板",["ENTER / SWITCH记录原因", "评分板默认关闭，可显式开启"],C["green"])
    p.footer("现役规则：AI/CompanionAIController.cpp/.h · Health/HealthComponent.cpp；倍率在Health唯一入口结算", "2026-10-10规则快照 · 离线确定性游戏AI；固定事件台词使用预合成语音。示意不代表权重最优性或完整回归。")
    return p.save()

def iteration():
    p=Canvas("03-feedback-iteration","反馈迭代：可见火球与实际命中必须一致","案例：召唤师火球飞过玩家头顶仍扣血。把体感问题转成几何约束，再核对实际结果。")
    p.card(60,220,460,214,"01 作者试玩反馈","看得见躲开，却仍掉血",["两次反馈：火球从头顶飞过", "要求三线改回单线、保留速度"],C["red"])
    p.card(570,220,460,214,"02 旧版隔离PIE复现","球心高出胶囊顶部310cm",["血量952 → 940，扣12血", "旧XY100cm判定忽略高度/地形"],C["gold"])
    p.card(1080,220,460,214,"03 设计约束","显示、命中与遮挡同构",["可见核心 = 查询半径35cm", "三维直线，地形先挡就销毁"],C["blue"])
    p.arrow([(520,327),(570,327)],C["red"])
    p.arrow([(1030,327),(1080,327)],C["gold"])
    p.rect(60,486,970,348,stroke=C["blue"])
    p.text(84,507,"04 实装：连续扫掠实际胶囊，并比较首次接触",28,C["ink"],True,maxw=920)
    p.text(84,559,"本帧起点",23,C["blue"],maxw=250)
    p.text(699,559,"拟达终点",23,C["blue"],maxw=250)
    p.line([(172,636),(746,636)],C["blue"],4)
    p.arrow([(173,636),(330,636)],C["blue"],4)
    for x in (170,745):
        p.svg.append(f'<circle cx="{x}" cy="636" r="35" fill="{C["panel"]}" stroke="{C["blue"]}" stroke-width="3"/>')
        if p.draw: p.draw.ellipse((x-35,601,x+35,671),fill=C["panel"],outline=C["blue"],width=3)
    p.rect(415,580,45,126,C["panel"],C["gold"],22)
    p.text(362,714,"实际玩家胶囊",22,C["gold"],maxw=220)
    p.rect(600,590,26,112,C["line"],C["muted"],0)
    p.text(558,714,"BlockPawn实体",22,C["muted"],maxw=220)
    p.text(84,763,"按Hit.Time取先到接触；同距地形优先。示意不按世界比例。",23,C["muted"],maxw=910)
    p.rect(1080,486,460,348,stroke=C["green"])
    p.text(1103,506,"05 验证与保留边界",21,C["green"],True,maxw=414)
    p.text(1103,551,"新版自然单发迹线",28,C["ink"],True,maxw=414)
    for i,t in enumerate(["常态0.6s / 1600cm/s", "眩晕1.2s / 800cm/s", "地形命中销毁日志", "作者2026-10-09晚确认正常", "未宣称全地形/全角度覆盖"]):
        p.text(1103,605+i*36,t,23,C["muted"] if i!=3 else C["green"],maxw=414)
    p.footer("现役规则：AI/BossOrb.cpp/.h · AI/BossSummonerBarrage.cpp；历史复现与作者验收摘要见 Docs/EVIDENCE.md", "失败复现、实现事实、作者体验验收分别记录；这个案例不替代完整可分发包、自然整局平衡或长期性能验证。")
    return p.save()

if __name__ == "__main__":
    report={"dimensions":[W,H],"diagramType":"original code-authored mechanism illustrations, not screenshots",
            "figures":{}}
    for fn in (core,ai,iteration):
        report["figures"][fn.__name__]=fn()
    (ROOT/"layout-validation.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    print("Generated 3 SVGs"+(" and 3 PNGs; text width validation passed." if Image else "; Pillow absent, PNG export skipped."))
