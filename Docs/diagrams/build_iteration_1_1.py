"""Generate the original 1.1 iteration / reward queue mechanism diagram.

Run: python build_iteration_1_1.py
Uses the existing build_diagrams.Canvas, palette, fonts, and raster exporter.
Writes only 04-iteration-1-1.svg/.png and its independent layout audit.

Source snapshot read on 2026-10-10:
  Docs/ITERATION_1.1.md: original feedback and public recording protocol.
  AI/WaveDirector.h: ERewardSource / ERewardPool / FRewardChoice.
  AI/WaveDirector.cpp: AddXp, EnterUpgradePause, StartUpgradeReward,
      FlipCurrentReward, TryGrantStarterUpgrade, TrySpendGoldForUpgrade.
  AI/WaveDirectorRelics.cpp: ProcessNextChoice / CompleteChoice.

This is an authored rule illustration, not a game screenshot or a complete
verification claim. State captions intentionally retain ongoing verification
and the external trusted-signing-certificate limitation.
"""
from html import escape
import json
import sys

# Importing the shared renderer should not add unrelated cache files to Docs.
sys.dont_write_bytecode = True
from build_diagrams import Canvas, C, ROOT, W, H


NAME = "04-iteration-1-1"
TITLE = "1.1 反馈迭代：规则、验收与公开记录"
SUBTITLE = "异机试玩触发改动；先公开预期，后追加实现与证据。成长队列保留每次奖励的来源与发生等级。"
ISSUE_URL = "https://github.com/Meracle-sjj/AICompanionDemo/issues/1"


def small_box(p, x, y, w, label, color):
    p.rect(x, y, w, 58, stroke=color, radius=12)
    p.text(x + 18, y + 15, label, 24, color, True, maxw=w - 36)


def build():
    p = Canvas(NAME, TITLE, SUBTITLE)
    # The root SVG description is useful when the diagram is read without pixels.
    p.svg[1] = (
        f'<title id="title">{escape(TITLE)}</title>'
        '<desc id="desc">原创机制图。上部依次展示反馈与先行预期、设计判断、'
        '真实验收、GitHub追加记录，后续反馈返回下一轮公开预期。下部展示奖励'
        '保存Source、EarnedLevel和BossIndex，并按顺序领取。仅经验等级奖励从'
        'Lv.9玩家池、Lv.10 AI池交替。Flip只替换当前玩家等级奖励为AI候选，'
        '不领取下一次奖励。1.1专项验收进行中；可信数字签名证书尚未具备。'
        '此图不是运行截图，不代表全部功能已通过验收。</desc>'
    )

    workflow = [
        (60, "01 反馈与先行预期", "异机首次试玩",
         ["11项观察 → 验收条件", "先公开 GitHub issue #1"], C["blue"]),
        (440, "02 设计判断", "把问题转成规则",
         ["实读源码，记录取舍", "方案变化先追加公开预期"], C["gold"]),
        (820, "03 真实验收", "构建与运行取证",
         ["日志 / 截图核对实际行为", "1.1 专项验收进行中"], C["blue"]),
        (1200, "04 GitHub保留记录", "追加结果与边界",
         ["模块提交关联 issue #1", "保留1.0与原始反馈"], C["gold"]),
    ]
    for x, kicker, title, lines, color in workflow:
        p.card(x, 220, 340, 190, kicker, title, lines, color)
    for x in (60, 440, 820):
        p.arrow([(x + 340, 315), (x + 380, 315)], C["gold"])

    p.arrow([(1370, 410), (1370, 455), (230, 455), (230, 410)],
            C["gold"], 2, dash=True)
    p.rect(573, 434, 454, 41, C["bg"], None, 0)
    p.text(602, 441, "后续反馈 → 下一轮公开预期", 23, C["gold"], maxw=410)

    # Source is a durable part of the reward; late level changes cannot relabel it.
    p.rect(60, 500, 450, 348, stroke=C["blue"])
    p.text(82, 518, "R11 · 奖励入队", 19, C["blue"], True, maxw=406)
    p.text(82, 551, "来源随奖励保存", 29, C["ink"], True, maxw=406)
    p.text(82, 600, "经验升级：保存发生等级", 23, C["muted"], maxw=406)
    p.text(82, 638, "普通箱 / 初始箱 / 锁箱 / Boss遗物", 23,
           C["muted"], maxw=406)
    p.rect(82, 681, 406, 52, stroke=C["gold"], radius=10)
    p.text(99, 695, "Source · EarnedLevel · BossIndex", 21,
           C["gold"], True, maxw=372)
    p.text(82, 754, "顺序领取，依据奖励的发生等级分池", 22,
           C["muted"], maxw=406)
    p.text(82, 794, "宝箱与遗物保持各自领取规则", 22,
           C["muted"], maxw=406)
    p.arrow([(510, 675), (570, 675)], C["blue"])

    p.rect(570, 500, 500, 348, stroke=C["gold"])
    p.text(592, 518, "R11 · 领取时归属", 19, C["gold"], True, maxw=456)
    p.text(592, 551, "Lv.9 起，玩家与AI交替", 29,
           C["ink"], True, maxw=456)
    p.text(592, 600, "仅 Source = LevelUp 参与轮换", 22,
           C["muted"], maxw=456)
    for y, left, right in ((642, "Lv.9 · 玩家池", "Lv.10 · AI池"),
                           (715, "Lv.11 · 玩家池", "Lv.12 · AI池")):
        small_box(p, 592, y, 205, left, C["blue"])
        small_box(p, 839, y, 209, right, C["gold"])
        p.arrow([(797, y + 29), (839, y + 29)], C["gold"], 2)
    p.text(592, 791, "Flip：当前玩家候选 → AI三选一", 22,
           C["blue"], True, maxw=456)
    p.text(592, 821, "每次奖励限一次；后续队列与等级归属不变", 19,
           C["muted"], maxw=456)

    # No badge implies a globally passed release. A certificate remains external.
    p.rect(1120, 500, 420, 348, stroke=C["blue"])
    p.text(1142, 518, "当前状态 · 保留边界", 19,
           C["blue"], True, maxw=376)
    p.text(1142, 551, "验收不等于发布", 29, C["ink"], True, maxw=376)
    p.text(1142, 600, "1.1 专项验收进行中", 24, C["gold"], True, maxw=376)
    p.text(1142, 638, "接线 / 编译不代表全部体验通过", 22,
           C["muted"], maxw=376)
    p.text(1142, 674, "证据按实际覆盖范围追加", 22,
           C["muted"], maxw=376)
    p.line([(1142, 717), (1518, 717)], C["line"], 2)
    p.text(1142, 733, "数字签名：可信证书尚未具备", 22,
           C["muted"], maxw=376)
    p.text(1142, 771, "发布者告警保留为外部限制", 22,
           C["muted"], maxw=376)
    p.text(1142, 809, "不以自签名冒充可信发布者", 22,
           C["muted"], maxw=376)

    p.footer(
        "规则来源：Docs/ITERATION_1.1.md · AI/WaveDirector.h/.cpp · AI/WaveDirectorRelics.cpp；公开记录：GitHub issue #1",
        "2026-10-10规则快照 · 原创可维护示意；验收仍在进行，发布状态以实际版本记录与可下载附件为准。",
    )
    bounds = p.save()
    report = {
        "dimensions": [W, H],
        "diagramType": "original code-authored mechanism illustration, not a screenshot",
        "generator": "build_iteration_1_1.py",
        "styleDependency": "build_diagrams.py",
        "publicRecord": ISSUE_URL,
        "state": "1.1 targeted verification in progress; trusted signing certificate unavailable",
        "sourceSnapshotDate": "2026-10-10",
        "sourceInterfaces": {
            "AI/WaveDirector.h": ["ERewardSource", "ERewardPool", "FRewardChoice"],
            "AI/WaveDirector.cpp": ["AddXp", "EnterUpgradePause", "StartUpgradeReward", "FlipCurrentReward"],
            "AI/WaveDirectorRelics.cpp": ["ProcessNextChoice", "CompleteChoice"],
        },
        "figure": bounds,
    }
    (ROOT / (NAME + "-layout-validation.json")).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Generated {NAME}.svg" +
          (f" and {NAME}.png; text width validation passed." if p.im
           else "; Pillow absent, PNG export skipped."))


if __name__ == "__main__":
    build()
