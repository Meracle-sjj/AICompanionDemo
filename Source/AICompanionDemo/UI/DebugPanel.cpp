/**
 * [INPUT]: DebugPanel契约、Slate分区/滚动/按钮与真实字体Measure，GameMode测试转发、导演当前升级名称
 * [OUTPUT]: 青蓝测试控制台与常驻55×40 F1键帽/两行动作状态提示，展开/收起均可见且HitTestInvisible；只拆GameMode原提示，未知文本完整保留，原全文/显示几何可回读；不接输入或另存玩法状态
 * [POS]: UI/ 界面层实现件：仅测试模式挂载（正常模式零痕迹）——验收提速专用面板
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "DebugPanel.h"
#include "InputKeycap.h"

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

namespace
{
	struct FTestInputPrompt
	{
		FString Original;
		FString Action;
		FString State;
		bool bBlocked = true;
	};

	FTestInputPrompt ReadTestInputPrompt(const AAICompanionDemoGameMode* Host)
	{
		FTestInputPrompt Prompt;
		Prompt.Original = Host ? Host->GetTestInputHint() : TEXT("F1 切换鼠标模式");
		FString State;
		FString Action;
		if (Prompt.Original.Split(TEXT(" · F1 "), &State, &Action)
			&& ((State == TEXT("战斗模式 · 鼠标已锁定") && Action == TEXT("切换到点选"))
				|| (State == TEXT("点选模式 · 鼠标自由 / 视角固定") && Action == TEXT("回到战斗"))))
		{
			Prompt.Action = Action;
			Prompt.State = State.Replace(TEXT(" / "), TEXT(" · "));
			Prompt.bBlocked = false;
		}
		else if (Prompt.Original == TEXT("菜单或奖励选择中 · 完成后恢复鼠标模式"))
		{
			Prompt.Action = TEXT("完成后恢复");
			Prompt.State = TEXT("菜单或奖励选择中 · 暂不可切换");
		}
		else
		{
			// 宿主新增或未知提示不能被样式拆分丢弃；整句显示在状态行。
			Prompt.Action = TEXT("鼠标模式提示");
			Prompt.State = Prompt.Original;
		}
		return Prompt;
	}
}

void UDebugPanel::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
}

void UDebugPanel::PrepareButtonStyle()
{
	if (bStylesReady) { return; }
	ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.035f, 0.075f, 0.10f, 1.f)))
		.SetHovered(FSlateColorBrush(FLinearColor(0.065f, 0.19f, 0.24f, 1.f)))
		.SetPressed(FSlateColorBrush(FLinearColor(0.03f, 0.13f, 0.17f, 1.f)))
		.SetDisabled(FSlateColorBrush(FLinearColor(0.025f, 0.04f, 0.05f, 0.8f)))
		.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
	bStylesReady = true;
}

TSharedRef<SWidget> UDebugPanel::MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick, float WrapWidth)
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
		.IsFocusable(false)
		.ContentPadding(FMargin(10.f, 7.f))
		.ToolTipText(Text)
		.HAlign(HAlign_Left).VAlign(VAlign_Center)
		.OnClicked_Lambda([OnClick]()
		{
			OnClick();
			return FReply::Handled();
		})
		[
			SAssignNew(LabelWidget, STextBlock).Font(Font)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.93f, 0.96f)))
				.Text(Text).AutoWrapText(false).WrapTextAt(WrapWidth)
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
	auto Row = [this](const FString& Label, const TFunction<void()>& OnClick) -> TSharedRef<SWidget>
	{
		return MakeDebugButton(Label, OnClick);
	};
	auto Section = [WhiteBrush](const TCHAR* Title, const TCHAR* Subtitle, TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(WhiteBrush).Padding(10.f)
			.BorderBackgroundColor(FLinearColor(0.017f, 0.037f, 0.052f, 1.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Title))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
					.ColorAndOpacity(FLinearColor(0.34f, 0.83f, 0.88f)).AutoWrapText(false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
				[
					SNew(STextBlock).Text(FText::FromString(Subtitle))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FLinearColor(0.52f, 0.66f, 0.72f)).AutoWrapText(false).WrapTextAt(280.f)
				]
				+ SVerticalBox::Slot().AutoHeight()[Content]
			];
	};
	TSharedRef<SVerticalBox> BossButtons = SNew(SVerticalBox);
	BossButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("召唤师"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(0); } }) ];
	BossButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("裂地巨像"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(1); } }) ];
	BossButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("沙狱蠕虫"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnBoss(2); } }) ];
	TSharedRef<SVerticalBox> EnemyButtons = SNew(SVerticalBox);
	EnemyButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("生成 5 只杂兵"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnEnemies(5); } }) ];
	EnemyButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("生成 15 只杂兵"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugSpawnEnemies(15); } }) ];
	EnemyButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("清空全场怪物"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugClearEnemies(); } }) ];
	TSharedRef<SVerticalBox> PlayerButtons = SNew(SVerticalBox);
	PlayerButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("金币 +100"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugAddGold(100); } }) ];
	PlayerButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("等级 +1 · 生命回满"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugAddLevel(); } }) ];
	PlayerButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("切换无敌"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugToggleGod(); } }) ];
	PlayerButtons->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[ Row(TEXT("召唤师伤害诊断"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->DebugLogBossSummoner(); } }) ];
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

	return SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(0.f, 12.f, 12.f, 12.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
			.WidthOverride(344.f)
			[
				SAssignNew(ExpandedPanel, SBorder)
				.BorderImage(WhiteBrush)
				.Visibility(bCollapsed ? EVisibility::Collapsed : EVisibility::Visible)
				.Padding(FMargin(12.f))
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.009f, 0.022f, 0.035f, 0.97f)))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("测试控制台")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
						.ColorAndOpacity(FLinearColor(0.88f, 0.97f, 1.f)).AutoWrapText(false)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("自由组合技能，快速体验战斗。")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
						.ColorAndOpacity(FLinearColor(0.52f, 0.66f, 0.72f)).AutoWrapText(false)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
						[ MakeDebugButton(TEXT("返回主菜单"), [this]() { if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ReturnToMainMenu(); } }, 0.f) ]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[ MakeDebugButton(TEXT("收起面板"), [this]() { SetCollapsed(true); }, 0.f) ]
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SScrollBox).ScrollBarAlwaysVisible(true).ScrollBarThickness(FVector2D(5.f, 5.f))
						.ScrollBarPadding(FMargin(5.f, 0.f, 0.f, 0.f)).AllowOverscroll(EAllowOverscroll::No)
						+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
						[ Section(TEXT("首领战"), TEXT("选择首领，立即开始战斗。"), BossButtons) ]
						+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
						[ Section(TEXT("敌人"), TEXT("调整战场密度或清空当前敌人。"), EnemyButtons) ]
						+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
						[ Section(TEXT("玩家"), TEXT("补给、成长与战斗诊断。"), PlayerButtons) ]
						+ SScrollBox::Slot()
						[ Section(TEXT("升级测试"), TEXT("点击添加升级；Flip用于转换当前等级奖励。"), UpgradeButtons) ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					[
						// 为独立F1提示预留底部高度；收起后该提示仍保持可见。
						SNew(SBox).HeightOverride(64.f)
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(12.f)
		[
			SAssignNew(CollapsedBar, SBorder).BorderImage(WhiteBrush).Padding(4.f)
				.BorderBackgroundColor(FLinearColor(0.009f, 0.022f, 0.035f, 0.97f))
				.Visibility(bCollapsed ? EVisibility::Visible : EVisibility::Collapsed)
			[ MakeDebugButton(TEXT("展开测试面板  ›"), [this]() { SetCollapsed(false); }, 0.f) ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(24.f)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				SAssignNew(TestInputHintPanel, SBorder).BorderImage(WhiteBrush).Padding(FMargin(10.f, 8.f))
				.BorderBackgroundColor(FLinearColor(0.009f, 0.022f, 0.035f, 0.94f))
				.Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SAssignNew(TestInputKeycap, SBox).WidthOverride(55.f).HeightOverride(40.f)
						[
							MakeInputKeycap(TEXT("F1"), TAttribute<bool>::CreateLambda([this]() { return !ReadTestInputPrompt(Host.Get()).bBlocked; }))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(TestInputAction, STextBlock)
							.Text_Lambda([this]() { return FText::FromString(ReadTestInputPrompt(Host.Get()).Action); })
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
							.ColorAndOpacity_Lambda([this]()
							{
								return FSlateColor(ReadTestInputPrompt(Host.Get()).bBlocked
									? FLinearColor(0.63f, 0.69f, 0.73f) : FLinearColor(0.89f, 0.96f, 0.98f));
							})
							.AutoWrapText(false).WrapTextAt(233.f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
						[
							SAssignNew(TestInputHint, STextBlock)
							.Text_Lambda([this]() { return FText::FromString(ReadTestInputPrompt(Host.Get()).State); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(FLinearColor(0.57f, 0.73f, 0.79f)).AutoWrapText(false).WrapTextAt(233.f)
						]
					]
				]
			]
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
	TestInputHintPanel.Reset();
	TestInputKeycap.Reset();
	TestInputAction.Reset();
	TestInputHint.Reset();
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
			const FVector2D DesiredSize = Label->GetDesiredSize();
			const FVector2D AllottedSize = Label->GetCachedGeometry().GetLocalSize();
			const float Desired = DesiredSize.Y;
			const float Allotted = AllottedSize.Y;
			if (Allotted > 0.f)
			{
				++Measured;
				if (Allotted + 0.5f < Desired) { ++Undersized; }
			}
			Rows += FString::Printf(TEXT("%s desired=%.2f allotted=%.2f desiredWidth=%.2f allottedWidth=%.2f; "),
				*Label->GetText().ToString(), Desired, Allotted, DesiredSize.X, AllottedSize.X);
		}
	}
	const FTestInputPrompt InputPrompt = ReadTestInputPrompt(Host.Get());
	const FVector2D KeycapSize = TestInputKeycap ? TestInputKeycap->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	const FVector2D HintSize = TestInputHintPanel ? TestInputHintPanel->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	const FVector2D ActionDesired = TestInputAction ? TestInputAction->GetDesiredSize() : FVector2D::ZeroVector;
	const FVector2D ActionAllotted = TestInputAction ? TestInputAction->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	const FVector2D StateDesired = TestInputHint ? TestInputHint->GetDesiredSize() : FVector2D::ZeroVector;
	const FVector2D StateAllotted = TestInputHint ? TestInputHint->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	const bool bHintMeasured = ActionAllotted.Y > 0.f && StateAllotted.Y > 0.f;
	const bool bHintUndersized = bHintMeasured && (ActionAllotted.X + 0.5f < ActionDesired.X || ActionAllotted.Y + 0.5f < ActionDesired.Y
		|| StateAllotted.X + 0.5f < StateDesired.X || StateAllotted.Y + 0.5f < StateDesired.Y);
	return FString::Printf(TEXT("collapsed=%d inViewport=%d measured=%d undersized=%d labels=%d upgradeButtons=%d inputHint=%s inputAction=%s inputState=%s inputBlocked=%d keycap=%.2fx%.2f hint=%.2fx%.2f actionDesired=%.2fx%.2f actionAllotted=%.2fx%.2f stateDesired=%.2fx%.2f stateAllotted=%.2fx%.2f hintMeasured=%d hintUndersized=%d | %s"),
		bCollapsed, IsInViewport(), Measured, Undersized, LabelWidgets.Num(), RenderedUpgradeCount,
		*InputPrompt.Original, TestInputAction ? *TestInputAction->GetText().ToString() : TEXT(""),
		TestInputHint ? *TestInputHint->GetText().ToString() : TEXT(""), InputPrompt.bBlocked,
		KeycapSize.X, KeycapSize.Y, HintSize.X, HintSize.Y, ActionDesired.X, ActionDesired.Y, ActionAllotted.X, ActionAllotted.Y,
		StateDesired.X, StateDesired.Y, StateAllotted.X, StateAllotted.Y, bHintMeasured, bHintUndersized, *Rows);
}

void UDebugPanel::DebugSetCollapsed(bool bInCollapsed)
{
#if WITH_EDITOR
	if (IsInViewport()) { SetCollapsed(bInCollapsed); }
#endif
}
