/**
 * [INPUT]: 依赖 DamageFlash.h 的组件契约、Slate 控件头（SOverlay/SBoxPanel/SBorder/SSpacer/SBox）、FPlatformTime 真实时钟
 * [OUTPUT]: 对外提供 UDamageFlash 的实现——全屏淡红 0.18×exp(-5.5t)（快闪即逝）+ 四边血条 0.55×exp(-4t)（先于全屏消散，留边缘"在流血"的余韵）
 * [POS]: UI/ 家族实现之一：方向箭头已删（用户裁决"游戏不需要判断敌人在哪"，且字符箭头有残留 bug）；血迹贴图待 Fab 素材，纯色先行
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "DamageFlash.h"

#include "AICompanionDemo.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

namespace
{
	// 受击红：全屏淡红（快闪）与四边血条（略慢）共用一色、两档峰值/衰减——层次感。
	// 09-24 二调：用户实测"完全没看到红"——0.18 快闪在亮背景（蓝天沙丘）下含蓄过头，加大到肉眼必见
	const FLinearColor kHurtRed(0.78f, 0.06f, 0.05f);
	constexpr float kFullPeakAlpha = 0.32f;
	constexpr float kFullDecayRate = 2.2f;
	constexpr float kBarPeakAlpha = 0.75f;
	constexpr float kBarDecayRate = 1.8f;
	constexpr float kEdgeBarThickness = 34.f;
}

void UDamageFlash::Pulse()
{
	PulseRealSeconds = FPlatformTime::Seconds();
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] 受击闪红触发"));
}

TSharedRef<SWidget> UDamageFlash::RebuildWidget()
{
	// 全屏淡红层：SSpacer 撑内容（塌缩教训 v1——空 SBorder 零尺寸从未显示过）
	TSharedPtr<SBorder> Full;
	TSharedRef<SWidget> FullWidget =
		SAssignNew(Full, SBorder)
		.Padding(0.f)
		// 09-24 根因修复：默认 brush 指向编辑器贴图（游戏运行时不存在）——红闪从未画过（用户三轮"没红色"定罪）
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FSlateColor(FLinearColor(kHurtRed.R, kHurtRed.G, kHurtRed.B, 0.f)))
		[
			SNew(SSpacer)
		];
	FullFlash = Full;

	// 四边血条造条器：SBox 限厚（不限厚会被 SSpacer 撑成全屏）
	auto MakeEdgeBar = [this](TSharedPtr<SBorder>& OutBar, bool bHorizontal) -> TSharedRef<SWidget>
	{
		TSharedPtr<SBorder> Bar;
		TSharedRef<SBox> Box = SNew(SBox);
		if (bHorizontal)
		{
			Box->SetHeightOverride(kEdgeBarThickness);
		}
		else
		{
			Box->SetWidthOverride(kEdgeBarThickness);
		}
		Box->SetContent(
			SAssignNew(Bar, SBorder)
			.Padding(0.f)
			// 09-24 根因修复：同全屏层——默认 brush 空资源，血条从未画过
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FSlateColor(FLinearColor(kHurtRed.R, kHurtRed.G, kHurtRed.B, 0.f)))
			[
				SNew(SSpacer)
			]);
		OutBar = Bar;
		return Box;
	};

	TSharedRef<SWidget> TopWidget = MakeEdgeBar(EdgeBars[0], true);
	TSharedRef<SWidget> BottomWidget = MakeEdgeBar(EdgeBars[1], true);
	TSharedRef<SWidget> LeftWidget = MakeEdgeBar(EdgeBars[2], false);
	TSharedRef<SWidget> RightWidget = MakeEdgeBar(EdgeBars[3], false);

	return SNew(SOverlay)

		// ---------- 全屏淡红（底层） ----------
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			FullWidget
		]

		// ---------- 四边血条 ----------
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top)
		[
			TopWidget
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			BottomWidget
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			LeftWidget
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill)
		[
			RightWidget
		];
}

void UDamageFlash::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateReveal();
}

void UDamageFlash::UpdateReveal()
{
	// 真实时钟衰减（世界冻结期 UMG 的 dt=0——世界时钟在此场景不可用，DeathScreen 同款教训）
	const double SincePulse = FPlatformTime::Seconds() - PulseRealSeconds;

	const float FullAlpha = FMath::Max(kFullPeakAlpha * static_cast<float>(FMath::Exp(-kFullDecayRate * SincePulse)) - 0.002f, 0.f);
	if (FullFlash.IsValid())
	{
		FullFlash->SetBorderBackgroundColor(FSlateColor(FLinearColor(kHurtRed.R, kHurtRed.G, kHurtRed.B, FullAlpha)));
	}

	const float BarAlpha = FMath::Max(kBarPeakAlpha * static_cast<float>(FMath::Exp(-kBarDecayRate * SincePulse)) - 0.002f, 0.f);
	for (const TSharedPtr<SBorder>& Bar : EdgeBars)
	{
		if (Bar.IsValid())
		{
			Bar->SetBorderBackgroundColor(FSlateColor(FLinearColor(kHurtRed.R, kHurtRed.G, kHurtRed.B, BarAlpha)));
		}
	}
}
