/**
 * [INPUT]: 依赖 SpeechBubble.h 的组件契约、Slate 的 SBorder/STextBlock（Widgets/Layout、Widgets/Text）、FSlateRoundedBoxBrush（零资产圆角底板——CompanionHud 同款实证）、FCoreStyle 默认字体、未暂停的Slate播放时钟
 * [OUTPUT]: 固定584宽换行/配色；显式奖励继续旗贯穿早词缓存与Slate时钟，真实暂停全冻结，空词清除参数；不按文本/类别猜优先级。
 * [POS]: UI/ 四件套实现之一：世界空间展示层（挂 WidgetComponent，Screen 模式）；Bark 台词的"气泡感"正装——替代 DrawDebugString 的日志观感
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "SpeechBubble.h"
#include "RunEventStyle.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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

void USpeechBubble::ShowLine(const FString& Line, float Duration, ERunEventKind Kind, bool bContinueDuringReward)
{
	if (Line.IsEmpty())
	{
		PendingLine.Reset();
		bPendingContinueDuringReward = false;
		bContinuesDuringReward = false;
		bShowing = false;
		ElapsedSeconds = 0.f;
		if (LineText.IsValid()) LineText->SetText(FText::GetEmpty());
		SetRenderOpacity(0.f);
		return;
	}
	if (LineText.IsValid())
	{
		PendingLine.Reset();
		bPendingContinueDuringReward = false;
		LineText->SetText(FText::FromString(FString::Printf(TEXT("「%s」"), *Line)));
		LineText->SetColorAndOpacity(RunEventColor(Kind));
	}
	else
	{
		// 早词缓存（09-28 空气泡案根治）：CreateWidget 后 Slate 树惰性构建（首帧 TakeWidget 才跑
		// RebuildWidget），开局首句 Bark 到达时 LineText 未出生——此前直接静默丢词，而挂载侧
		// SetVisibility(true) 已亮底板 = 空气泡。词先落袋，RebuildWidget 末尾补挂完整生命周期。
		PendingLine = Line;
		PendingDuration = Duration;
		PendingKind = Kind;
		bPendingContinueDuringReward = bContinueDuringReward;
		return;
	}
	HoldDuration = Duration;
	bContinuesDuringReward = bContinueDuringReward;
	ElapsedSeconds = 0.f;
	bShowing = true;
}

TSharedRef<SWidget> USpeechBubble::RebuildWidget()
{
	BubbleBrush = MakeUnique<FSlateRoundedBoxBrush>(kBubbleBg, 10.f, kBubbleOutline, 1.2f);

	// 台词字号（09-27 用户实测"太小看不清"定罪：引擎默认约 12pt）——气泡是注意力焦点，22pt 起步
	const FSlateFontInfo BubbleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22);

	TSharedRef<SWidget> Tree = SAssignNew(BubbleBorder, SBorder)
		.BorderImage(BubbleBrush.Get())
		.Padding(FMargin(16.f, 10.f, 16.f, 10.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(584.f)
			[
			SAssignNew(LineText, STextBlock)
			.Font(BubbleFont)
			.ColorAndOpacity(FSlateColor(kBubbleText))
			.ShadowOffset(FVector2D(1.5f, 1.5f))
			.AutoWrapText(false).WrapTextAt(584.f)
			.Text(FText::GetEmpty())
			]
		];

	// 早词补挂：树已成形，缓存的台词此刻才可能真正落进 LineText（时序正确的完整生命周期）
	if (!PendingLine.IsEmpty())
	{
		const FString Line = MoveTemp(PendingLine);
		ShowLine(Line, PendingDuration, PendingKind, bPendingContinueDuringReward);
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

	// 实际最高事件跨奖励冻结演出；真实暂停仍停计，优先级由Audio显式旗标提供。
	if (!GetWorld() || GetWorld()->IsPaused()
		|| (UGameplayStatics::GetGlobalTimeDilation(this) < .01f && !bContinuesDuringReward)) { return; }
	ElapsedSeconds += InDeltaTime;
	const float Elapsed = ElapsedSeconds;

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

FString USpeechBubble::DebugSpeechBubbleState() const
{
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("text"), LineText.IsValid() ? LineText->GetText().ToString() : FString());
	State->SetStringField(TEXT("pendingText"), PendingLine);
	State->SetBoolField(TEXT("showing"), bShowing);
	State->SetBoolField(TEXT("continueDuringReward"), bContinuesDuringReward);
	State->SetBoolField(TEXT("pendingContinueDuringReward"), bPendingContinueDuringReward);
	State->SetNumberField(TEXT("elapsed"), ElapsedSeconds);
	State->SetNumberField(TEXT("duration"), HoldDuration);
	State->SetNumberField(TEXT("fixedWrapWidth"), 584.f);
	State->SetNumberField(TEXT("opacity"), GetRenderOpacity());
	if (LineText.IsValid())
	{
		const FVector2D Desired = LineText->GetDesiredSize();
		const FVector2D Allocated = LineText->GetCachedGeometry().GetLocalSize();
		State->SetNumberField(TEXT("desiredWidth"), Desired.X);
		State->SetNumberField(TEXT("desiredHeight"), Desired.Y);
		State->SetNumberField(TEXT("allocatedWidth"), Allocated.X);
		State->SetNumberField(TEXT("allocatedHeight"), Allocated.Y);
	}
	FString Result;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
