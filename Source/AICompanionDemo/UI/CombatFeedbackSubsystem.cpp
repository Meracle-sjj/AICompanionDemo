/**
 * [INPUT]: Health提交的模式/减伤结算后完整伤害/语义/内部归属值快照，既有金币口，Slate粗体描边/矢量与世界投影。
 * [OUTPUT]: 金币整数与全伤害两位四舍五入裁尾零（正值不足0.005显示<0.01）；白小剑与十族伤害图样，独立融化在原火伤上行绘冰火双图/混色数字，暂停冻结/终局清理。
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
	const FLinearColor FireRed(1.f, .12f, .025f);
	const FLinearColor FireCore(1.f, .66f, .06f);
	const FLinearColor FrostBlue(.60f, .87f, 1.f);
	const FLinearColor MeltTint = FMath::Lerp(FrostBlue, FireRed, .5f);
	const FLinearColor BloodRed(.58f, .025f, .065f);
	const FLinearColor ShockPurple(.77f, .36f, 1.f);

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
		Icon = ECombatFeedbackIcon::Sword; Tint = FLinearColor::White;
		switch (Kind)
		{
		case ECombatDamageKind::Frost: Icon = ECombatFeedbackIcon::Frost; Tint = FrostBlue; break;
		case ECombatDamageKind::Melt: Icon = ECombatFeedbackIcon::Melt; Tint = MeltTint; break;
		case ECombatDamageKind::Explosion:
		case ECombatDamageKind::Burn:
		case ECombatDamageKind::Fireball:
		case ECombatDamageKind::Radiance: Icon = ECombatFeedbackIcon::Flame; Tint = FireRed; break;
		case ECombatDamageKind::Bleed: Icon = ECombatFeedbackIcon::Drop; Tint = BloodRed; break;
		case ECombatDamageKind::Shock: Icon = ECombatFeedbackIcon::Lightning; Tint = ShockPurple; break;
		case ECombatDamageKind::Execute: Icon = ECombatFeedbackIcon::Cleaver; break;
		case ECombatDamageKind::Spike:
		case ECombatDamageKind::Seismic: Icon = ECombatFeedbackIcon::Spikes; break;
		case ECombatDamageKind::Pulse: Icon = ECombatFeedbackIcon::Ray; Tint = FLinearColor(.49f, .99f, .54f); break;
		case ECombatDamageKind::Beam: Icon = ECombatFeedbackIcon::Ray; break;
		case ECombatDamageKind::Reflect:
		case ECombatDamageKind::Melee:
		case ECombatDamageKind::MobFeedback: Icon = ECombatFeedbackIcon::Impact; break;
		default: break;
		}
	}

	FString FormatCombatFeedbackAmount(const FCombatFeedbackEntry& Entry)
	{
		if (Entry.Icon == ECombatFeedbackIcon::Coin)
		{
			return FString::Printf(TEXT("%.0f"), static_cast<double>(Entry.Amount));
		}
		const double ResolvedDamage = static_cast<double>(Entry.Amount);
		if (ResolvedDamage < .005) return TEXT("<0.01");
		// 两位精度表达模式/减伤结算后的完整伤害，致死时不被目标剩余HP截小。
		const double DisplayDamage = FMath::RoundToDouble(ResolvedDamage * 100.) / 100.;
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
		if (Icon == ECombatFeedbackIcon::Melt)
		{
			// 反应标记直接复用两种元素剪影，各保留原色，不把融化误读为单纯火伤。
			DrawIcon(Elements, LayerId, Geometry, Center + FVector2f(-11.f, 0.f) * Scale,
				Scale * .7f, ECombatFeedbackIcon::Frost, FrostBlue, Alpha);
			DrawIcon(Elements, LayerId, Geometry, Center + FVector2f(10.f, 0.f) * Scale,
				Scale * .7f, ECombatFeedbackIcon::Flame, FireRed, Alpha);
			return;
		}
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
			const auto Line = [&](TArray<FVector2f> Points, float Width = 4.f, const FLinearColor* Accent = nullptr)
			{
				for (FVector2f& Point : Points) Point = Center + Point * Scale;
				FLinearColor LineColor = Accent ? *Accent : Color; LineColor.A = Alpha;
				Stroke(Elements, LayerId, Geometry, Points, Dark, (Width + 4.f) * Scale);
				Stroke(Elements, LayerId + 1, Geometry, Points, LineColor, Width * Scale);
			};
			// 火焰用不对称多尖外焰与橙黄内焰，流血保留单尖血滴；剪影和亮度同时区分。
			switch (Icon)
			{
			case ECombatFeedbackIcon::Sword:
				// 尖刃、横向护手与外露剑柄三段构成小剑，物理攻击仍统一纯白。
				Line({{-5, 3}, {8, -13}, {14, -18}, {12, -10}, {-1, 7}, {-5, 3}}, 3.f);
				Line({{-10, -1}, {4, 11}}, 4.f);
				Line({{-3, 6}, {-11, 15}}, 5.f);
				Line({{-14, 12}, {-9, 17}}, 3.f); break;
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
			case ECombatFeedbackIcon::Flame:
				Line({{-1, -20}, {-5, -10}, {-10, -13}, {-11, -4}, {-15, 2}, {-13, 11},
					{-8, 16}, {0, 18}, {9, 14}, {13, 7}, {11, -2}, {8, -8}, {6, 0}, {3, -6}, {-1, -20}}, 4.f);
				Line({{0, -7}, {-5, 2}, {-5, 9}, {0, 13}, {6, 9}, {6, 3}, {4, -2}, {2, 4}, {0, -7}}, 3.f, &FireCore); break;
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
		// 融化是一笔额外伤害，独立上行只调整画面，不合并或改写原火伤entry。
		const float ReactionLift = Entry.Icon == ECombatFeedbackIcon::Melt ? 54.f * Pop : 0.f;
		const FVector2f Position(Pixel.X / Dpi, Pixel.Y / Dpi - Ratio * 52.f - ReactionLift);
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 32.f * Pop);
		Font.OutlineSettings.OutlineSize = 3;
		Font.OutlineSettings.OutlineColor = FLinearColor(Ink.R, Ink.G, Ink.B, Alpha);
		const FString Text = FormatCombatFeedbackAmount(Entry);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
		const float IconAdvance = Entry.Icon == ECombatFeedbackIcon::Melt ? 32.f : 20.f;
		const FVector2f TextOrigin = Position + FVector2f(IconAdvance * Pop, -TextSize.Y * .5f);
		DrawIcon(Elements, Base + 1, Geometry, Position, Pop, Entry.Icon, Entry.Tint, Alpha);
		const FLinearColor Color = Entry.Icon == ECombatFeedbackIcon::Coin
			? FLinearColor(1.f, .87f, .31f, Alpha) : FLinearColor(Entry.Tint.R, Entry.Tint.G, Entry.Tint.B, Alpha);
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

void UCombatFeedbackSubsystem::ShowExecute(UWorld* World, const FVector& Location, float ResolvedDamage, int32 MarkCount)
{
	ShowDamage(World, {ECombatDamageKind::Execute, Location, MarkCount}, ResolvedDamage, ECombatDamageSource::Unknown);
}

void UCombatFeedbackSubsystem::ShowDamage(UWorld* World, const FCombatDamageContext& Context, float ResolvedDamage, ECombatDamageSource Source)
{
	if (!World || !FMath::IsFinite(ResolvedDamage) || ResolvedDamage <= 0.f || Context.Location.ContainsNaN()
		|| static_cast<uint8>(Context.Kind) > static_cast<uint8>(ECombatDamageKind::MobFeedback)
		|| static_cast<uint8>(Source) > static_cast<uint8>(ECombatDamageSource::Hostile)) return;
	if (UCombatFeedbackSubsystem* State = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		FCombatFeedbackEntry Entry;
		Entry.Location = Context.Location; Entry.Amount = ResolvedDamage; Entry.MarkCount = Context.MarkCount;
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
		Values += FString::Printf(TEXT("{\"amount\":%.9g,\"kind\":%d,\"kindName\":\"%s\",\"source\":%d,\"sourceName\":\"%s\",\"age\":%.9g,\"marks\":%d,\"icon\":%d,\"display\":\"%s\",\"tint\":[%.9g,%.9g,%.9g],\"location\":[%.9g,%.9g,%.9g]}"),
			static_cast<double>(Entry.Amount), bCoin ? -1 : static_cast<int32>(Entry.Kind), bCoin ? TEXT("gold") : KindName(Entry.Kind),
			static_cast<int32>(Entry.Source), SourceName(Entry.Source), static_cast<double>(Entry.Age), Entry.MarkCount,
			static_cast<int32>(Entry.Icon), *FormatCombatFeedbackAmount(Entry), Entry.Tint.R, Entry.Tint.G, Entry.Tint.B,
			Entry.Location.X, Entry.Location.Y, Entry.Location.Z);
	}
	return FString::Printf(TEXT("{\"entries\":%d,\"layerVisible\":%s,\"endingMuted\":%s,\"sourceBadgeVisible\":false,\"damageAmountMode\":\"resolved_before_health_clamp\",\"lifetime\":%.2f,\"limit\":%d,\"values\":[%s]}"),
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
