/**
 * [INPUT]: Health提交的实际伤害/语义/来源值快照，既有金币口，Slate粗体描边/矢量与世界投影。
 * [OUTPUT]: 金币整数与全伤害两位四舍五入裁尾零（正值不足0.005显示<0.01）；八族伤害图样与来源徽记，暂停冻结/终局清理。
 * [POS]: 既有UI数值层的渲染与生命周期实现；不读取可变伤害账本或当前模式，不新增HUD/事件队列/图片字体资源。
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

	const TCHAR* KindName(ECombatDamageKind Kind)
	{
		static const TCHAR* Names[] = {TEXT("generic"), TEXT("projectile"), TEXT("frost"), TEXT("melt"),
			TEXT("explosion"), TEXT("shock"), TEXT("execute"), TEXT("ricochet"), TEXT("radiance"),
			TEXT("pulse"), TEXT("spike"), TEXT("seismic"), TEXT("reflect"), TEXT("bleed"),
			TEXT("burn"), TEXT("fireball"), TEXT("beam"), TEXT("melee"), TEXT("mob_feedback")};
		return Names[static_cast<uint8>(Kind)];
	}

	const TCHAR* SourceName(ECombatDamageSource Source)
	{
		static const TCHAR* Names[] = {TEXT("unknown"), TEXT("player"), TEXT("companion_guard"),
			TEXT("companion_berserk"), TEXT("hostile")};
		return Names[static_cast<uint8>(Source)];
	}

	void DamageStyle(ECombatDamageKind Kind, ECombatFeedbackIcon& Icon, FLinearColor& Tint)
	{
		Icon = ECombatFeedbackIcon::Bullet; Tint = FLinearColor(1.f, .89f, .45f);
		switch (Kind)
		{
		case ECombatDamageKind::Frost: Icon = ECombatFeedbackIcon::Frost; Tint = FLinearColor(.60f, .87f, 1.f); break;
		case ECombatDamageKind::Melt: Icon = ECombatFeedbackIcon::Frost; Tint = FLinearColor(1.f, .73f, .39f); break;
		case ECombatDamageKind::Explosion:
		case ECombatDamageKind::Burn:
		case ECombatDamageKind::Fireball: Icon = ECombatFeedbackIcon::Drop; Tint = FLinearColor(1.f, .48f, .17f); break;
		case ECombatDamageKind::Bleed: Icon = ECombatFeedbackIcon::Drop; Tint = FLinearColor(1.f, .16f, .28f); break;
		case ECombatDamageKind::Shock: Icon = ECombatFeedbackIcon::Lightning; Tint = FLinearColor(.78f, .65f, 1.f); break;
		case ECombatDamageKind::Execute: Icon = ECombatFeedbackIcon::Cleaver; Tint = FLinearColor(.98f, .98f, 1.f); break;
		case ECombatDamageKind::Ricochet: Tint = FLinearColor(.97f, .56f, 1.f); break;
		case ECombatDamageKind::Spike:
		case ECombatDamageKind::Seismic: Icon = ECombatFeedbackIcon::Spikes; Tint = FLinearColor(.96f, .76f, .47f); break;
		case ECombatDamageKind::Radiance: Icon = ECombatFeedbackIcon::Ray; Tint = FLinearColor(.35f, .94f, .83f); break;
		case ECombatDamageKind::Pulse: Icon = ECombatFeedbackIcon::Ray; Tint = FLinearColor(.49f, .99f, .54f); break;
		case ECombatDamageKind::Beam: Icon = ECombatFeedbackIcon::Ray; Tint = FLinearColor(1.f, .44f, .48f); break;
		case ECombatDamageKind::Reflect: Icon = ECombatFeedbackIcon::Impact; Tint = FLinearColor(.87f, .70f, .41f); break;
		case ECombatDamageKind::Melee: Icon = ECombatFeedbackIcon::Impact; Tint = FLinearColor(1.f, .43f, .43f); break;
		case ECombatDamageKind::MobFeedback: Icon = ECombatFeedbackIcon::Impact; Tint = FLinearColor(.93f, .81f, 1.f); break;
		default: break;
		}
	}

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
		FVector2f Center, float Scale, ECombatFeedbackIcon Icon, const FLinearColor& Tint, float Alpha)
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
		if (Icon != ECombatFeedbackIcon::Cleaver)
		{
			FLinearColor Color = Tint; Color.A = Alpha;
			const auto Line = [&](TArray<FVector2f> Points, float Width = 4.f)
			{
				for (FVector2f& Point : Points) Point = Center + Point * Scale;
				Stroke(Elements, LayerId, Geometry, Points, Dark, (Width + 4.f) * Scale);
				Stroke(Elements, LayerId + 1, Geometry, Points, Color, Width * Scale);
			};
			// 七族新剪影与原Cleaver共八族；火/血共用滴形，以类别色区分，不另建纹理。
			switch (Icon)
			{
			case ECombatFeedbackIcon::Bullet:
				Line({{-12, 8}, {0, -12}, {9, -10}, {13, -3}, {1, 15}, {-12, 8}}, 5.f);
				Line({{-6, 5}, {5, 11}}, 2.f); break;
			case ECombatFeedbackIcon::Frost:
				for (int32 I = 0; I < 3; ++I)
				{
					const float Angle = I * PI / 3.f;
					const FVector2f Axis(FMath::Cos(Angle), FMath::Sin(Angle));
					Line({Axis * -15.f, Axis * 15.f}, 3.f);
				}
				Line({{-5, -3}, {0, -6}, {5, -3}, {5, 3}, {0, 6}, {-5, 3}, {-5, -3}}, 2.f); break;
			case ECombatFeedbackIcon::Drop:
				Line({{0, -17}, {-10, -2}, {-12, 7}, {-7, 14}, {4, 15}, {11, 8}, {10, 1}, {0, -17}}, 6.f);
				Line({{-5, 3}, {-5, 8}, {-2, 10}}, 2.f); break;
			case ECombatFeedbackIcon::Lightning:
				Line({{5, -17}, {-10, 2}, {0, 2}, {-5, 17}, {12, -4}, {2, -4}, {5, -17}}, 4.f); break;
			case ECombatFeedbackIcon::Spikes:
				Line({{-17, 13}, {-12, -3}, {-7, 13}, {0, -17}, {7, 13}, {12, -3}, {17, 13}, {-17, 13}}, 3.f); break;
			case ECombatFeedbackIcon::Ray:
				Line(Ring(FVector2f::ZeroVector, 8.f), 4.f);
				for (int32 I = 0; I < 4; ++I)
				{
					const float Angle = I * PI / 2.f;
					const FVector2f Axis(FMath::Cos(Angle), FMath::Sin(Angle));
					Line({Axis * 12.f, Axis * 18.f}, 2.f);
				}
				break;
			case ECombatFeedbackIcon::Impact:
				Line({{-16, -11}, {-5, -7}, {0, -17}, {5, -5}, {17, -9}, {9, 2}, {16, 13},
					{4, 9}, {-2, 17}, {-7, 6}, {-17, 8}, {-10, -1}, {-16, -11}}, 3.f); break;
			default: break;
			}
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

	void DrawSourceBadge(FSlateWindowElementList& Elements, int32 LayerId, const FGeometry& Geometry,
		FVector2f Position, ECombatDamageSource Source, float Alpha)
	{
		const TCHAR* Label = TEXT("");
		FLinearColor Color(.82f, .84f, .88f, Alpha);
		switch (Source)
		{
		case ECombatDamageSource::Player: Label = TEXT("玩家"); Color = FLinearColor(1.f, .91f, .56f, Alpha); break;
		case ECombatDamageSource::CompanionGuard: Label = TEXT("护卫"); Color = FLinearColor(.39f, .89f, 1.f, Alpha); break;
		case ECombatDamageSource::CompanionBerserk: Label = TEXT("歼灭"); Color = FLinearColor(.89f, .54f, 1.f, Alpha); break;
		case ECombatDamageSource::Hostile: Label = TEXT("敌方"); Color = FLinearColor(1.f, .35f, .37f, Alpha); break;
		default: return;
		}
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12.f);
		Font.OutlineSettings.OutlineSize = 2;
		Font.OutlineSettings.OutlineColor = FLinearColor(Ink.R, Ink.G, Ink.B, Alpha);
		const FString BadgeLabel(Label);
		const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(BadgeLabel, Font);
		Stroke(Elements, LayerId, Geometry,
			{Position + FVector2f(-3, -1), Position + FVector2f(Size.X + 5, -1),
			Position + FVector2f(Size.X + 2, Size.Y + 1), Position + FVector2f(-5, Size.Y + 1), Position + FVector2f(-3, -1)},
			FLinearColor(Ink.R, Ink.G, Ink.B, Alpha), 5.f);
		FSlateDrawElement::MakeText(Elements, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(Position)), BadgeLabel, Font, ESlateDrawEffect::None, Color);
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
		DrawIcon(Elements, Base + 1, Geometry, Position, Pop, Entry.Icon, Entry.Tint, Alpha);
		const FLinearColor Color = Entry.Icon == ECombatFeedbackIcon::Coin
			? FLinearColor(1.f, .87f, .31f, Alpha) : FLinearColor(Entry.Tint.R, Entry.Tint.G, Entry.Tint.B, Alpha);
		FSlateDrawElement::MakeText(Elements, Base + 5,
			Geometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(TextOrigin)), Text, Font,
			ESlateDrawEffect::None, Color);
		if (Entry.Icon != ECombatFeedbackIcon::Coin)
			DrawSourceBadge(Elements, Base + 6, Geometry, TextOrigin + FVector2f(0.f, TextSize.Y + 1.f), Entry.Source, Alpha);
	}
	return Base + 7;
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
	ShowDamage(World, {ECombatDamageKind::Execute, Location, MarkCount}, ActualDamage, ECombatDamageSource::Unknown);
}

void UCombatFeedbackSubsystem::ShowDamage(UWorld* World, const FCombatDamageContext& Context, float ActualDamage, ECombatDamageSource Source)
{
	if (!World || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f || Context.Location.ContainsNaN()
		|| static_cast<uint8>(Context.Kind) > static_cast<uint8>(ECombatDamageKind::MobFeedback)
		|| static_cast<uint8>(Source) > static_cast<uint8>(ECombatDamageSource::Hostile)) return;
	if (UCombatFeedbackSubsystem* State = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		FCombatFeedbackEntry Entry;
		Entry.Location = Context.Location; Entry.Amount = ActualDamage; Entry.MarkCount = Context.MarkCount;
		Entry.Kind = Context.Kind; Entry.Source = Source;
		DamageStyle(Context.Kind, Entry.Icon, Entry.Tint);
		State->Push(Entry);
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
	FString Values;
	for (const FCombatFeedbackEntry& Entry : Entries)
	{
		if (!Values.IsEmpty()) Values += TEXT(",");
		const bool bCoin = Entry.Icon == ECombatFeedbackIcon::Coin;
		Values += FString::Printf(TEXT("{\"amount\":%.9g,\"kind\":%d,\"kindName\":\"%s\",\"source\":%d,\"sourceName\":\"%s\",\"age\":%.9g,\"marks\":%d,\"icon\":%d,\"display\":\"%s\",\"location\":[%.9g,%.9g,%.9g]}"),
			static_cast<double>(Entry.Amount), bCoin ? -1 : static_cast<int32>(Entry.Kind), bCoin ? TEXT("gold") : KindName(Entry.Kind),
			static_cast<int32>(Entry.Source), SourceName(Entry.Source), static_cast<double>(Entry.Age), Entry.MarkCount,
			static_cast<int32>(Entry.Icon), *FormatCombatFeedbackAmount(Entry), Entry.Location.X, Entry.Location.Y, Entry.Location.Z);
	}
	return FString::Printf(TEXT("{\"entries\":%d,\"layerVisible\":%s,\"endingMuted\":%s,\"lifetime\":%.2f,\"limit\":%d,\"values\":[%s]}"),
		Entries.Num(), Layer && Layer->IsInViewport() ? TEXT("true") : TEXT("false"), bEndingMuted ? TEXT("true") : TEXT("false"), FeedbackLifetime, FeedbackLimit, *Values);
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

void UCombatFeedbackSubsystem::DebugEmitDamageFeedback(int32 Kind, float Amount, int32 Source, int32 Marks)
{
#if WITH_EDITOR
	if (Kind < 0 || Kind > static_cast<int32>(ECombatDamageKind::MobFeedback)
		|| Source < 0 || Source > static_cast<int32>(ECombatDamageSource::Hostile)) return;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		ShowDamage(GetWorld(), {static_cast<ECombatDamageKind>(Kind), Player->GetActorLocation() + FVector(0.f, 0.f, 210.f), Marks},
			Amount, static_cast<ECombatDamageSource>(Source));
#endif
}
