/**
 * [INPUT]: 依赖 DebugPanel.h 的面板契约、Slate 的 SOverlay/SScrollBox/SVerticalBox/SHorizontalBox/SButton/STextBlock/SBorder/SBox、FCoreStyle 默认字体、本模块 AICompanionDemoGameMode 的调试转发组
 * [OUTPUT]: 对外提供 UDebugPanel 的实现——RebuildWidget 手搭滚动列（四节：BOSS/怪物/玩家/全卡直加）；按钮回调经宿主转发组执行
 * [POS]: UI/ 界面层实现件：仅测试模式挂载（正常模式零痕迹）——验收提速专用面板
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "DebugPanel.h"

#include "AICompanionDemoGameMode.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void UDebugPanel::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
}

TSharedRef<SWidget> UDebugPanel::MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick)
{
	return SNew(SButton)
		.OnClicked_Lambda([OnClick]()
		{
			OnClick();
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.Padding(FMargin(8.f, 5.f))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.05f, 0.05f, 0.1f, 0.9f)))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.9f, 1.f)))
				.Text(FText::FromString(Label))
			]
		];
}

TSharedRef<SWidget> UDebugPanel::RebuildWidget()
{
	// 节头
	auto SectionTitle = [](const FString& Title) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.8f, 0.3f)))
			.Text(FText::FromString(Title));
	};

	// 单按钮行
	auto Row = [this](const FString& Label, const TFunction<void()>& OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.HeightOverride(34.f)
			.Padding(0.f, 2.f)
			[
				MakeDebugButton(Label, OnClick)
			];
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(0.f, 0.f, 8.f, 8.f)
		[
			SNew(SBox)
			.WidthOverride(340.f)
			[
				SNew(SBorder)
				.Padding(FMargin(6.f))
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.02f, 0.02f, 0.06f, 0.55f)))
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(2.f)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
						.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)))
						.Text(FText::FromString(TEXT("测 试 模 式")))
					]

					// ---------- BOSS ----------
					+ SScrollBox::Slot().Padding(2.f) [ SectionTitle(TEXT("— BOSS —")) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("召唤 召唤师"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(0); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("召唤 裂地巨像"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(1); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("召唤 沙狱蠕虫"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(2); } }) ]

					// ---------- 怪物 ----------
					+ SScrollBox::Slot().Padding(2.f) [ SectionTitle(TEXT("— 怪物 —")) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("刷 5 只杂兵"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnEnemies(5); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("刷 15 只杂兵"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnEnemies(15); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("清空全场怪"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugClearEnemies(); } }) ]

					// ---------- 玩家 ----------
					+ SScrollBox::Slot().Padding(2.f) [ SectionTitle(TEXT("— 玩家 —")) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("金币 +100"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugAddGold(100); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("等级 +1（回满）"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugAddLevel(); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("无敌 开/关"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugToggleGod(); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("召唤师掉血诊断"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugLogBossSummoner(); } }) ]

					// ---------- 全卡直加（EUpgradeId 枚举序号 0-23，与 WaveDirector 枚举顺序同步） ----------
					+ SScrollBox::Slot().Padding(2.f) [ SectionTitle(TEXT("— 全卡直加 —")) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("闪烁"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(0); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("散射(AI)"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(1); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("穿透(AI)"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(2); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("狂暴速射"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(3); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("重击弹头"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(4); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("辉耀领域"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(5); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("贴贴疗愈"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(6); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("玻璃大炮"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(7); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("血祭强化"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(8); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("强身"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(9); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("弹幕风暴"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(10); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("闪现轰击"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(11); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("辉耀过载"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(12); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("折射(AI)"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(13); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("疾行"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(14); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("高爆弹头"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(15); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("寒霜弹(AI)"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(16); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("辉耀结界"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(17); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("回响脉冲"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(18); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("寒霜弹(玩家)"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(19); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("感电弹"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(20); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("猎杀印记"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(21); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("感电过载"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(22); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("猎杀大师"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(23); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("深度冻结"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(24); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("玩家散射"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(25); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("玩家穿透"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(26); } }) ]
					+ SScrollBox::Slot().Padding(2.f) [ Row(TEXT("玩家折射"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugApplyUpgradeByIdx(27); } }) ]
				]
			]
		];
}
