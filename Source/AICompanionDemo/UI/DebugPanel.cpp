/**
 * [INPUT]: 依赖 DebugPanel.h 的面板契约、Slate 的 SOverlay/SScrollBox/SVerticalBox/SHorizontalBox/SButton/STextBlock/SBorder/SBox、FCoreStyle 默认字体、本模块 AICompanionDemoGameMode 的调试转发组
 * [OUTPUT]: 固定顶部返回/收起、内部展开入口和四节滚动列；取消34px强制行高，按实际中文字体高度+内容padding留足空间，回读实际文字几何
 * [POS]: UI/ 界面层实现件：仅测试模式挂载（正常模式零痕迹）——验收提速专用面板
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "DebugPanel.h"

#include "AICompanionDemoGameMode.h"
#include "AI/WaveDirector.h"
#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
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

void UDebugPanel::PrepareButtonStyle()
{
	if (bStylesReady) { return; }
	ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.05f, 0.07f, 0.12f, 0.96f)))
		.SetHovered(FSlateColorBrush(FLinearColor(0.11f, 0.22f, 0.29f, 1.f)))
		.SetPressed(FSlateColorBrush(FLinearColor(0.05f, 0.16f, 0.23f, 1.f)))
		.SetDisabled(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.08f, 0.8f)))
		.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
	bStylesReady = true;
}

TSharedRef<SWidget> UDebugPanel::MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick)
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FText Text = FText::FromString(Label);
	float TextHeight = 0.f;
	if (FSlateApplication::IsInitialized())
	{
		TextHeight = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).Y);
	}
	TSharedPtr<STextBlock> LabelWidget;
	TSharedRef<SWidget> Button = SNew(SBox).MinDesiredHeight(FMath::Max(38.f, FMath::CeilToFloat(TextHeight) + 14.f))
	[
	SNew(SButton)
		.ButtonStyle(&ButtonStyle)
		.ContentPadding(FMargin(10.f, 7.f))
		.HAlign(HAlign_Left).VAlign(VAlign_Center)
		.OnClicked_Lambda([OnClick]()
		{
			OnClick();
			return FReply::Handled();
		})
		[
			SAssignNew(LabelWidget, STextBlock).Font(Font)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.9f, 1.f)))
				.Text(Text).AutoWrapText(true)
		]
	];
	LabelWidgets.Add(LabelWidget);
	return Button;
}

TSharedRef<SWidget> UDebugPanel::RebuildWidget()
{
	PrepareButtonStyle();
	LabelWidgets.Reset();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
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
		return MakeDebugButton(Label, OnClick);
	};
	TSharedRef<SVerticalBox> UpgradeButtons = SNew(SVerticalBox);
	RenderedUpgradeCount = 0;
	if (AAICompanionDemoGameMode* GM = Host.Get())
	{
		if (AWaveDirector* Director = GM->GetWaveDirector())
		{
			RenderedUpgradeCount = Director->DebugUpgradeCardCount();
			for (int32 Index = 0; Index < RenderedUpgradeCount; ++Index)
			{
				UpgradeButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
				[ Row(Director->DebugUpgradeCardLabel(Index), [this, Index]()
					{ if (AAICompanionDemoGameMode* CurrentHost = Host.Get()) { CurrentHost->DebugApplyUpgradeByIdx(Index); } }) ];
			}
		}
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(0.f, 0.f, 8.f, 8.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
			.WidthOverride(340.f)
			[
				SAssignNew(ExpandedPanel, SBorder)
				.BorderImage(WhiteBrush)
				.Visibility(bCollapsed ? EVisibility::Collapsed : EVisibility::Visible)
				.Padding(FMargin(6.f))
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.02f, 0.02f, 0.06f, 0.55f)))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 2.f, 2.f, 6.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
						[ MakeDebugButton(TEXT("返回主菜单"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ReturnToMainMenu(); } }) ]
						+ SHorizontalBox::Slot().AutoWidth()
						[ MakeDebugButton(TEXT("收起面板"), [this]() { SetCollapsed(true); }) ]
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
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

					// 数量与名称来自导演；Flip只转换当前合法玩家等级奖励，不授予角色技能。
					+ SScrollBox::Slot().Padding(2.f) [ SectionTitle(TEXT("— 升级 / 当前奖励操作 —")) ]
					+ SScrollBox::Slot().Padding(2.f) [ UpgradeButtons ]
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
		[
			SAssignNew(CollapsedBar, SBorder).BorderImage(WhiteBrush).Padding(4.f)
				.BorderBackgroundColor(FLinearColor(0.02f, 0.04f, 0.08f, 0.9f))
				.Visibility(bCollapsed ? EVisibility::Visible : EVisibility::Collapsed)
			[ MakeDebugButton(TEXT("展开测试面板  ›"), [this]() { SetCollapsed(false); }) ]
		];
}

void UDebugPanel::SetCollapsed(bool bInCollapsed)
{
	bCollapsed = bInCollapsed;
	if (ExpandedPanel) { ExpandedPanel->SetVisibility(bCollapsed ? EVisibility::Collapsed : EVisibility::Visible); }
	if (CollapsedBar) { CollapsedBar->SetVisibility(bCollapsed ? EVisibility::Visible : EVisibility::Collapsed); }
}

void UDebugPanel::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ExpandedPanel.Reset();
	CollapsedBar.Reset();
	LabelWidgets.Reset();
	// 成员style保留：旧SObjectWidget树尚可能持有其地址。
}

FString UDebugPanel::DebugPanelState() const
{
	int32 Measured = 0;
	int32 Undersized = 0;
	FString Rows;
	for (const TWeakPtr<STextBlock>& WeakLabel : LabelWidgets)
	{
		if (const TSharedPtr<STextBlock> Label = WeakLabel.Pin())
		{
			const float Desired = Label->GetDesiredSize().Y;
			const float Allotted = Label->GetCachedGeometry().GetLocalSize().Y;
			if (Allotted > 0.f)
			{
				++Measured;
				if (Allotted + 0.5f < Desired) { ++Undersized; }
			}
			Rows += FString::Printf(TEXT("%s desired=%.2f allotted=%.2f; "), *Label->GetText().ToString(), Desired, Allotted);
		}
	}
	return FString::Printf(TEXT("collapsed=%d inViewport=%d measured=%d undersized=%d labels=%d upgradeButtons=%d | %s"),
		bCollapsed, IsInViewport(), Measured, Undersized, LabelWidgets.Num(), RenderedUpgradeCount, *Rows);
}

void UDebugPanel::DebugSetCollapsed(bool bInCollapsed)
{
#if WITH_EDITOR
	if (IsInViewport()) { SetCollapsed(bInCollapsed); }
#endif
}
