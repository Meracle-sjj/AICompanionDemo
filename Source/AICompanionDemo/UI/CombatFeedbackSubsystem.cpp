/**
 * [INPUT]: CombatFeedbackSubsystem.h、Slate粗体字体/描边/矢量线、UMG视口DPI与世界到屏幕投影
 * [OUTPUT]: 常驻透明反馈层，金币整数/处决标准两位四舍五入裁尾零（正值不足0.005显示<0.01）与原创图标弹跳上浮；暂停冻结、终局清理、世界释放
 * [POS]: UI数值反馈的渲染与生命周期实现；不写金币/伤害账本，不依赖下载图标或新字体资产
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "CombatFeedbackSubsystem.h"
#include "AICompanionDemoGameMode.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	constexpr float FeedbackLifetime = .85f;
	constexpr int32 FeedbackLimit = 64;
	const FLinearColor Ink(.045f, .025f, .025f);
	const FLinearColor CoinGold(1.f, .76f, .16f);
	const FLinearColor BladeSilver(.88f, .97f, 1.f);

	FString FormatCombatFeedbackAmount(const FCombatFeedbackEntry& Entry)
	{
		if (Entry.Icon == ECombatFeedbackIcon::Coin)
		{
			return FString::Printf(TEXT("%.0f"), static_cast<double>(Entry.Amount));
		}
		const double ActualDamage = static_cast<double>(Entry.Amount);
		if (ActualDamage < .005) return TEXT("<0.01");
		// 两位精度表达实际结算值，不把二进制浮点误差向下放大一分。
		const double DisplayDamage = FMath::RoundToDouble(ActualDamage * 100.) / 100.;
		FString Text = FString::Printf(TEXT("%.2f"), DisplayDamage);
		while (Text.EndsWith(TEXT("0"))) Text.LeftChopInline(1);
		if (Text.EndsWith(TEXT("."))) Text.LeftChopInline(1);
		return Text;
	}

	void Stroke(FSlateWindowElementList& Elements, int32 LayerId, const FGeometry& Geometry,
		const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, LayerId, Geometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	TArray<FVector2f> Ring(FVector2f Center, float Radius)
	{
		TArray<FVector2f> Points;
		for (int32 I = 0; I <= 32; ++I)
		{
			const float Angle = I * 2.f * PI / 32.f;
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	void DrawIcon(FSlateWindowElementList& Elements, int32 LayerId, const FGeometry& Geometry,
		FVector2f Center, float Scale, ECombatFeedbackIcon Icon, float Alpha)
	{
		FLinearColor Dark = Ink; Dark.A = Alpha;
		if (Icon == ECombatFeedbackIcon::Coin)
		{
			FLinearColor Gold = CoinGold; Gold.A = Alpha;
			FLinearColor Light(1.f, .94f, .59f, Alpha);
			Stroke(Elements, LayerId, Geometry, Ring(Center + FVector2f(2, 2) * Scale, 14.f * Scale), Dark, 5.f * Scale);
			Stroke(Elements, LayerId + 1, Geometry, Ring(Center, 8.f * Scale), Gold, 14.f * Scale);
			Stroke(Elements, LayerId + 2, Geometry, Ring(Center, 12.f * Scale), Light, 2.f * Scale);
			Stroke(Elements, LayerId + 2, Geometry,
				{Center + FVector2f(-3, -7) * Scale, Center + FVector2f(-3, 6) * Scale}, Dark, 2.f * Scale);
			Stroke(Elements, LayerId + 3, Geometry,
				{Center + FVector2f(-7, -7) * Scale, Center + FVector2f(-3, -10) * Scale}, Light, 3.f * Scale);
			return;
		}
		FLinearColor Silver = BladeSilver; Silver.A = Alpha;
		FLinearColor Edge(.34f, .61f, .73f, Alpha);
		// 厚背/斜刃/外露刀柄形成单一剪影，避免文字或Unicode缺字替代类别。
		const FVector2f A = Center + FVector2f(-13, -11) * Scale;
		const FVector2f B = Center + FVector2f(11, -9) * Scale;
		const FVector2f C = Center + FVector2f(8, 10) * Scale;
		const FVector2f D = Center + FVector2f(-12, 5) * Scale;
		Stroke(Elements, LayerId, Geometry, {A, B, C, D, A}, Dark, 6.f * Scale);
		Stroke(Elements, LayerId + 1, Geometry,
			{Center + FVector2f(-10, -2) * Scale, Center + FVector2f(9, 0) * Scale}, Silver, 13.f * Scale);
		Stroke(Elements, LayerId + 2, Geometry, {C, D}, Edge, 2.f * Scale);
		Stroke(Elements, LayerId, Geometry,
			{Center + FVector2f(11, -9) * Scale, Center + FVector2f(19, -16) * Scale}, Dark, 7.f * Scale);
		Stroke(Elements, LayerId + 2, Geometry,
			{Center + FVector2f(12, -10) * Scale, Center + FVector2f(18, -15) * Scale}, FLinearColor(.53f, .22f, .1f, Alpha), 3.f * Scale);
	}
}

void UCombatFeedbackLayer::Setup(UCombatFeedbackSubsystem* InSource) { Source = InSource; }

TSharedRef<SWidget> UCombatFeedbackLayer::RebuildWidget()
{
	return SNew(SBox).Visibility(EVisibility::HitTestInvisible);
}

int32 UCombatFeedbackLayer::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Base = Super::NativePaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled);
	const UCombatFeedbackSubsystem* State = Source.Get();
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!State || !PC) return Base;
	const float Dpi = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), .01f);
	for (const FCombatFeedbackEntry& Entry : State->GetEntries())
	{
		FVector2D Pixel;
		if (!PC->ProjectWorldLocationToScreen(Entry.Location, Pixel, true)) continue;
		const float Ratio = FMath::Clamp(Entry.Age / FeedbackLifetime, 0.f, 1.f);
		const float Alpha = FMath::Clamp((1.f - Ratio) / .25f, 0.f, 1.f);
		const float Pop = 1.f + .22f * (1.f - FMath::Clamp(Entry.Age / .13f, 0.f, 1.f));
		const FVector2f Position(Pixel.X / Dpi, Pixel.Y / Dpi - Ratio * 52.f);
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 32.f * Pop);
		Font.OutlineSettings.OutlineSize = 3;
		Font.OutlineSettings.OutlineColor = FLinearColor(Ink.R, Ink.G, Ink.B, Alpha);
		const FString Text = FormatCombatFeedbackAmount(Entry);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
		const FVector2f TextOrigin = Position + FVector2f(20.f * Pop, -TextSize.Y * .5f);
		DrawIcon(Elements, Base + 1, Geometry, Position, Pop, Entry.Icon, Alpha);
		const FLinearColor Color = Entry.Icon == ECombatFeedbackIcon::Coin
			? FLinearColor(1.f, .87f, .31f, Alpha) : FLinearColor(.98f, .98f, 1.f, Alpha);
		FSlateDrawElement::MakeText(Elements, Base + 5,
			Geometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(TextOrigin)), Text, Font,
			ESlateDrawEffect::None, Color);
	}
	return Base + 5;
}

void UCombatFeedbackSubsystem::ShowGold(UWorld* World, const FVector& Location, int32 Amount)
{
	if (!World || Amount <= 0) return;
	if (UCombatFeedbackSubsystem* State = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		State->Push({Location, static_cast<float>(Amount), 0.f, 0, ECombatFeedbackIcon::Coin});
	}
}

void UCombatFeedbackSubsystem::ShowExecute(UWorld* World, const FVector& Location, float ActualDamage, int32 MarkCount)
{
	if (!World || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f) return;
	if (UCombatFeedbackSubsystem* State = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		State->Push({Location, ActualDamage, 0.f, MarkCount, ECombatFeedbackIcon::Cleaver});
	}
}

void UCombatFeedbackSubsystem::Push(const FCombatFeedbackEntry& Entry)
{
	const auto* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	if (bEndingMuted || (GM && GM->IsEndingShown())) return;
	if (!Layer)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			Layer = CreateWidget<UCombatFeedbackLayer>(PC, UCombatFeedbackLayer::StaticClass());
			if (Layer)
			{
				Layer->Setup(this); Layer->SetVisibility(ESlateVisibility::HitTestInvisible); Layer->AddToViewport(1);
			}
		}
	}
	if (!Layer) return;
	if (Entries.Num() >= FeedbackLimit) Entries.RemoveAt(0);
	FCombatFeedbackEntry Spread = Entry;
	Spread.Location.X += (Entries.Num() % 3 - 1) * 24.f;
	Entries.Add(Spread);
}

void UCombatFeedbackSubsystem::Tick(float DeltaTime)
{
	if (bEndingMuted || GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f) return;
	for (FCombatFeedbackEntry& Entry : Entries) Entry.Age += DeltaTime;
	Entries.RemoveAll([](const FCombatFeedbackEntry& Entry) { return Entry.Age >= FeedbackLifetime; });
	if (Layer) Layer->InvalidateLayoutAndVolatility();
}

void UCombatFeedbackSubsystem::ClearForEnding()
{
	bEndingMuted = true; Entries.Reset();
	if (Layer) { Layer->RemoveFromParent(); Layer = nullptr; }
}

void UCombatFeedbackSubsystem::Deinitialize()
{
	ClearForEnding(); Super::Deinitialize();
}

bool UCombatFeedbackSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCombatFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCombatFeedbackSubsystem, STATGROUP_Tickables);
}

FString UCombatFeedbackSubsystem::DebugFeedbackState() const
{
	return FString::Printf(TEXT("{\"entries\":%d,\"layerVisible\":%s,\"endingMuted\":%s,\"lifetime\":%.2f,\"limit\":%d}"),
		Entries.Num(), Layer && Layer->IsInViewport() ? TEXT("true") : TEXT("false"), bEndingMuted ? TEXT("true") : TEXT("false"), FeedbackLifetime, FeedbackLimit);
}

void UCombatFeedbackSubsystem::DebugEmitFeedback(bool bExecute, int32 Amount)
{
#if WITH_EDITOR
    if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        const FVector Location = Player->GetActorLocation() + FVector(0.f, 0.f, bExecute ? 220.f : 170.f);
        if (bExecute) ShowExecute(GetWorld(), Location, Amount, 1);
        else ShowGold(GetWorld(), Location, Amount);
    }
#endif
}
