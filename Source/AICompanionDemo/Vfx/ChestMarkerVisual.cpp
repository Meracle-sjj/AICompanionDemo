/**
 * [INPUT]: ChestMarkerVisual.h、CoreStyle内置Bold/描边字体与Slate矢量线、相机和真实暂停/低流速。
 * [OUTPUT]: 384x160世界画布；FREE!紫白粗字、普通锁与真实金币大数、金色钥匙无数字；10m到4m smoothstep及时间平滑渐显。
 * [POS]: 宝箱纯视觉实现；不读写奖励账本，无调试文字、外部图片或新增资产，Slate仅持值。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "ChestMarkerVisual.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

class SChestMarkerGlyph final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SChestMarkerGlyph) {} SLATE_END_ARGS()
	void Construct(const FArguments&) { SetCanTick(false); SetVisibility(EVisibility::HitTestInvisible); }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(384.f, 160.f); }
	bool SetPresentation(EChestMarkerKind Kind, int32 Price, bool bNearby, bool bHasKey)
	{
		if (DisplayKind == Kind && DisplayPrice == Price && bInteract == bNearby && bKey == bHasKey) { return false; }
		DisplayKind = Kind; DisplayPrice = Price; bInteract = bNearby; bKey = bHasKey;
		Invalidate(EInvalidateWidgetReason::Paint);
		return true;
	}
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
	{
		const FLinearColor Tint = Style.GetColorAndOpacityTint();
		const FLinearColor Dark(.012f, .008f, .025f, 1.f);
		const FLinearColor Gold(1.f, .79f, .22f, 1.f);
		const FLinearColor Violet(.82f, .55f, 1.f, 1.f);
		const FLinearColor Main = DisplayKind == EChestMarkerKind::Free ? Violet : Gold;
		auto Stroke = [&](const TArray<FVector2f>& Points, const FLinearColor& Color, float Width)
		{
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Dark * Tint, true, Width + 7.f);
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color * Tint, true, Width);
		};
		auto Circle = [&](float X, float Y, float R, const FLinearColor& Color, float Width)
		{
			TArray<FVector2f> Points;
			for (int32 I = 0; I <= 48; ++I)
			{
				const float A = I * 2.f * PI / 48.f;
				Points.Add(FVector2f(X + FMath::Cos(A) * R, Y + FMath::Sin(A) * R));
			}
			Stroke(Points, Color, Width);
		};
		auto Text = [&](const FString& Value, float CenterX, float CenterY, float Size, const FLinearColor& Color)
		{
			FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size, FFontOutlineSettings(5, Dark));
			const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, Font);
			const FVector2D Offset(CenterX - TextSize.X * .5, CenterY - TextSize.Y * .5);
			FSlateDrawElement::MakeText(Elements, Layer + 2,
				Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Offset)), Value, Font, ESlateDrawEffect::None, Color * Tint);
		};
		if (DisplayKind == EChestMarkerKind::Free)
		{
			// 大写粗字为主信息；细高光/双侧星芒保持沙漠上清晰的剪影。
			Text(TEXT("FREE!"), 192.f, 67.f, 70.f, FLinearColor(1.f, .92f, 1.f, 1.f));
			Stroke({FVector2f(49, 108), FVector2f(335, 108)}, Violet, 4.f);
			for (const float X : {25.f, 359.f})
			{
				Stroke({FVector2f(X, 42), FVector2f(X, 78)}, Violet, 4.f);
				Stroke({FVector2f(X - 12, 60), FVector2f(X + 12, 60)}, Violet, 4.f);
			}
		}
		else if (DisplayKind == EChestMarkerKind::Gold)
		{
			// 圆弧锁梁与闭合锁身，数字直接来自导演的下一箱价格。
			TArray<FVector2f> Shackle;
			for (int32 I = 0; I <= 24; ++I)
			{
				const float A = PI + I * PI / 24.f;
				Shackle.Add(FVector2f(70 + FMath::Cos(A) * 22, 57 + FMath::Sin(A) * 27));
			}
			Stroke(Shackle, Gold, 8.f);
			Stroke({FVector2f(39, 57), FVector2f(101, 57), FVector2f(101, 104), FVector2f(39, 104), FVector2f(39, 57)}, Gold, 8.f);
			Stroke({FVector2f(70, 75), FVector2f(70, 87)}, FLinearColor::White, 6.f);
			const FString Price = FString::FromInt(FMath::Max(0, DisplayPrice));
			Text(Price, 234.f, 70.f, Price.Len() > 3 ? 55.f : 70.f, FLinearColor(1.f, .97f, .87f, 1.f));
		}
		else
		{
			// 钥匙轮廓复用HUD既有圆柄/杆/双齿语言；锁箱没有价格数字。
			const FLinearColor KeyColor = bKey ? FLinearColor(1.f, .95f, .65f, 1.f) : Gold;
			Circle(157.f, 49.f, 24.f, KeyColor, 9.f);
			Stroke({FVector2f(176, 68), FVector2f(231, 123), FVector2f(246, 108)}, KeyColor, 10.f);
			Stroke({FVector2f(211, 103), FVector2f(226, 88)}, KeyColor, 10.f);
		}
		if (bInteract)
		{
			Stroke({FVector2f(175, 123), FVector2f(209, 123), FVector2f(209, 156), FVector2f(175, 156), FVector2f(175, 123)}, Main, 2.5f);
			Text(TEXT("E"), 192.f, 139.f, 21.f, FLinearColor::White);
		}
		return Layer + 2;
	}
private:
	EChestMarkerKind DisplayKind = EChestMarkerKind::Gold;
	int32 DisplayPrice = -1;
	bool bInteract = false;
	bool bKey = false;
};

UChestMarkerVisual::UChestMarkerVisual(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetWidgetSpace(EWidgetSpace::World); SetGeometryMode(EWidgetGeometryMode::Plane);
	SetDrawSize(FVector2D(384.f, 160.f)); SetPivot(FVector2D(.5f, .5f)); SetDrawAtDesiredSize(false);
	SetBlendMode(EWidgetBlendMode::Transparent); SetBackgroundColor(FLinearColor::Transparent);
	SetTwoSided(true); SetWindowFocusable(false); SetManuallyRedraw(true); SetTickMode(ETickMode::Enabled);
	SetTickWhenOffscreen(false); SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false); SetCanEverAffectNavigation(false); SetCastShadow(false); SetReceivesDecals(false);
	SetAbsolute(true, true, true); SetRelativeScale3D(FVector(.5f));
	SetVisibility(false); SetHiddenInGame(true);
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bTickInEditor = false;
}

void UChestMarkerVisual::Present(EChestMarkerKind Kind, int32 Price, bool bInteractNearby, bool bHasKey)
{
	if (bRetired) { return; }
	DisplayKind = Kind; DisplayPrice = Price; bNearby = bInteractNearby; bKeyOwned = bHasKey;
	if (!Glyph.IsValid()) { Glyph = SNew(SChestMarkerGlyph); SetSlateWidget(Glyph); RequestRenderUpdate(); }
	if (Glyph->SetPresentation(Kind, Price, bNearby, bKeyOwned)) { RequestRenderUpdate(); }
	if (Kind == EChestMarkerKind::Free) { Opacity = 1.f; TargetOpacity = 1.f; SetTintColorAndOpacity(FLinearColor::White); }
	SetVisibility(Opacity > .001f); SetHiddenInGame(Opacity <= .001f); SetComponentTickEnabled(true);
	FaceCamera();
}

void UChestMarkerVisual::FaceCamera()
{
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector View; FRotator Rotation; PC->GetPlayerViewPoint(View, Rotation);
		const FVector Facing = View - GetComponentLocation();
		if (!Facing.IsNearlyZero()) { SetWorldRotation(FRotationMatrix::MakeFromXZ(Facing, Rotation.RotateVector(FVector::UpVector)).Rotator()); }
	}
}

void UChestMarkerVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	if (!bRetired)
	{
		const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
		PlayerDistance = Hero && GetOwner() ? FVector::Dist2D(Hero->GetActorLocation(), GetOwner()->GetActorLocation()) : MAX_flt;
		const float Near = FMath::Clamp((1000.f - PlayerDistance) / 600.f, 0.f, 1.f);
		TargetOpacity = DisplayKind == EChestMarkerKind::Free ? 1.f : Near * Near * (3.f - 2.f * Near);
		const bool bFrozen = GetWorld() && (GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f);
		if (!bFrozen)
		{
			Opacity = FMath::Lerp(Opacity, TargetOpacity, 1.f - FMath::Exp(-8.f * FMath::Max(DeltaTime, 0.f)));
			if (FMath::Abs(Opacity - TargetOpacity) < .001f) { Opacity = TargetOpacity; }
			SetTintColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Opacity));
		}
		SetVisibility(Opacity > .001f); SetHiddenInGame(Opacity <= .001f);
		FaceCamera();
	}
	Super::TickComponent(DeltaTime, TickType, TickFunction);
}

void UChestMarkerVisual::Retire()
{
	bRetired = true; Opacity = 0.f; TargetOpacity = 0.f;
	SetVisibility(false); SetHiddenInGame(true); SetComponentTickEnabled(false);
	SetSlateWidget(nullptr); Glyph.Reset();
}

void UChestMarkerVisual::EndPlay(const EEndPlayReason::Type Reason) { Retire(); Super::EndPlay(Reason); }
void UChestMarkerVisual::ReleaseResources() { Super::ReleaseResources(); Glyph.Reset(); }

FString UChestMarkerVisual::DebugVisualState() const
{
	return FString::Printf(TEXT("{\"kind\":%d,\"text\":\"%s\",\"price\":%d,\"keyOwned\":%s,\"interactNearby\":%s,\"opacity\":%.5f,\"targetOpacity\":%.5f,\"playerDistance\":%.3f,\"visible\":%s,\"retired\":%s,\"widgetReady\":%s,\"renderTarget\":%s,\"registered\":%s,\"collision\":%d,\"worldWidth\":192,\"font\":\"CoreStyle.Bold\"}"),
		static_cast<int32>(DisplayKind), DisplayKind == EChestMarkerKind::Free ? TEXT("FREE!") : DisplayKind == EChestMarkerKind::Gold ? TEXT("LOCK+PRICE") : TEXT("KEY"),
		DisplayPrice, bKeyOwned ? TEXT("true") : TEXT("false"), bNearby ? TEXT("true") : TEXT("false"), Opacity, TargetOpacity, PlayerDistance,
		IsVisible() ? TEXT("true") : TEXT("false"), bRetired ? TEXT("true") : TEXT("false"), Glyph.IsValid() && GetSlateWidget() == Glyph ? TEXT("true") : TEXT("false"),
		GetRenderTarget() ? TEXT("true") : TEXT("false"), IsRegistered() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetCollisionEnabled()));
}
