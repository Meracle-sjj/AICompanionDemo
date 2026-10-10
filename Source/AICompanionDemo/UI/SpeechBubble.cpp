/**
 * [INPUT]: 依赖 SpeechBubble.h 的组件契约、Slate 的 SBorder/STextBlock（Widgets/Layout、Widgets/Text）、FSlateRoundedBoxBrush（零资产圆角底板——CompanionHud 同款实证）、FCoreStyle 默认字体、FPlatformTime
 * [OUTPUT]: 对外提供 USpeechBubble 的实现——ShowLine 置词+重置时钟（树未建时落 PendingLine 袋，RebuildWidget 末尾补挂——09-28 空气泡案根治）；单行排版（AutoWrapText=false，09-28 碎行案）；NativeTick 三段插值（浮现上浮 8px/淡出 0.25s），完毕后 opacity 归零静默待命（下词复用）
 * [POS]: UI/ 四件套实现之一：世界空间展示层（挂 WidgetComponent，Screen 模式）；Bark 台词的"气泡感"正装——替代 DrawDebugString 的日志观感
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "SpeechBubble.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// 气泡配色：深蓝黑底（HUD 面板同语言）+ 台词白字；生命周期节奏（检索惯例：短句 3-6s/淡出 0.2-0.3s）
	const FLinearColor kBubbleBg(0.03f, 0.05f, 0.08f, 0.85f);
	const FLinearColor kBubbleOutline(1.f, 1.f, 1.f, 0.25f);
	const FLinearColor kBubbleText(0.96f, 0.98f, 1.f);
	constexpr float kRiseInSeconds = 0.2f;
	constexpr float kFadeOutSeconds = 0.25f;
	constexpr float kRiseOffset = 8.f;   // 浮现上浮距离（px）
}

void USpeechBubble::ShowLine(const FString& Line, float Duration)
{
	if (LineText.IsValid())
	{
		LineText->SetText(FText::FromString(FString::Printf(TEXT("「%s」"), *Line)));
	}
	else
	{
		// 早词缓存（09-28 空气泡案根治）：CreateWidget 后 Slate 树惰性构建（首帧 TakeWidget 才跑
		// RebuildWidget），开局首句 Bark 到达时 LineText 未出生——此前直接静默丢词，而挂载侧
		// SetVisibility(true) 已亮底板 = 空气泡。词先落袋，RebuildWidget 末尾补挂完整生命周期。
		PendingLine = Line;
		PendingDuration = Duration;
		return;
	}
	HoldDuration = Duration;
	ShowTime = FPlatformTime::Seconds();
	bShowing = true;
}

TSharedRef<SWidget> USpeechBubble::RebuildWidget()
{
	BubbleBrush = MakeUnique<FSlateRoundedBoxBrush>(kBubbleBg, 10.f, kBubbleOutline, 1.2f);

	// 台词字号（09-27 用户实测"太小看不清"定罪：引擎默认约 12pt）——气泡是注意力焦点，22pt 起步
	FSlateFontInfo BubbleFont = FCoreStyle::Get().GetFontStyle(TEXT("Bold"));
	BubbleFont.Size = 22;

	TSharedRef<SWidget> Tree = SAssignNew(BubbleBorder, SBorder)
		.BorderImage(BubbleBrush.Get())
		.Padding(FMargin(16.f, 10.f, 16.f, 10.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(LineText, STextBlock)
			.Font(BubbleFont)
			.ColorAndOpacity(FSlateColor(kBubbleText))
			.ShadowOffset(FVector2D(1.5f, 1.5f))
			.AutoWrapText(false)   // 09-28 碎行案：420px 宽度下引擎按字硬断成 2-3 字碎行（「放开/我的主/人！」）——台词全部 ≤13 字，单行居中是 WoW 名牌正装；将来超长词再议主动按标点断句
			.Text(FText::GetEmpty())
		];

	// 早词补挂：树已成形，缓存的台词此刻才可能真正落进 LineText（时序正确的完整生命周期）
	if (!PendingLine.IsEmpty())
	{
		const FString Line = MoveTemp(PendingLine);
		ShowLine(Line, PendingDuration);
	}
	return Tree;
}

void USpeechBubble::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 待命态：不渲染（opacity 归零由上一轮淡出收尾保证），零成本静默
	if (!bShowing)
	{
		return;
	}

	// 真实时钟差分（世界 TimeDilation=0 冻结期 UMG 世界时钟 dt=0——铁律禁 UMG 动画）
	const double Now = FPlatformTime::Seconds();
	const float Elapsed = static_cast<float>(Now - ShowTime);

	if (Elapsed < kRiseInSeconds)
	{
		// 浮现：上浮 8px + 淡入（RenderTransform 平移不触发布局）
		const float T = Elapsed / kRiseInSeconds;
		SetRenderOpacity(T);
		if (LineText.IsValid())
		{
			LineText->SetRenderTransform(FSlateRenderTransform(FVector2D(0.f, (1.f - T) * kRiseOffset)));
		}
	}
	else if (Elapsed < HoldDuration)
	{
		// 停留：全亮
		SetRenderOpacity(1.f);
		if (LineText.IsValid())
		{
			LineText->SetRenderTransform(FSlateRenderTransform(FVector2D::ZeroVector));
		}
	}
	else if (Elapsed < HoldDuration + kFadeOutSeconds)
	{
		// 淡出
		SetRenderOpacity(1.f - (Elapsed - HoldDuration) / kFadeOutSeconds);
	}
	else
	{
		// 完毕：静默待命（下词 ShowLine 复活）
		bShowing = false;
		SetRenderOpacity(0.f);
	}
}
