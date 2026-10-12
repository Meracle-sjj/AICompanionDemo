/**
 * [INPUT]: FrostMarkVisual、Slate线段、Health死亡委托及世界游戏Delta/冻结门。
 * [OUTPUT]: 三朵原创雪花在固定小画布慢落，青白内芯/深色衬线，暂停冻结/无冰隐藏停Tick。
 * [POS]: WorldWidget纯显示实现；复用感电标记的存活/朝向/释放范式，不新增资产或Niagara源。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "FrostMarkVisual.h"
#include "Health/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

class SFrostGlyph final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrostGlyph) {} SLATE_END_ARGS()
	void Construct(const FArguments&) { SetCanTick(false); SetVisibility(EVisibility::HitTestInvisible); }
	void SetClock(float InClock) { Clock = InClock; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(96.f, 144.f); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D Size = Geometry.GetLocalSize();
		const float Scale = FMath::Min(Size.X / 96.f, Size.Y / 144.f);
		const FVector2f Offset((Size.X - 96.f * Scale) * .5f, (Size.Y - 144.f * Scale) * .5f);
		for (int32 Flake = 0; Flake < 3; ++Flake)
		{
			const float Fall = FMath::Frac(Clock / 3.6f + Flake / 3.f);
			const FVector2f Center = Offset + FVector2f(24.f + 24.f * Flake, 16.f + Fall * 112.f) * Scale;
			const float Alpha = FMath::Clamp(FMath::Min(Fall * 8.f, (1.f - Fall) * 8.f), 0.f, 1.f);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Angle = Axis * PI / 3.f;
				const FVector2f Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				const FVector2f Side(-Direction.Y, Direction.X);
				TArray<FVector2f> Points = {Center - Direction * 10.f * Scale, Center + Direction * 10.f * Scale};
				const auto Stroke = [&](const TArray<FVector2f>& Path, float Width)
				{
					FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Path,
						ESlateDrawEffect::None, FLinearColor(.025f, .065f, .10f, Alpha), true, (Width + 3.f) * Scale);
					FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Path,
						ESlateDrawEffect::None, FLinearColor(.80f, .96f, 1.f, Alpha) * Style.GetColorAndOpacityTint(), true, Width * Scale);
				};
				Stroke(Points, 2.f);
				for (float Sign : {-1.f, 1.f})
				{
					const FVector2f Tip = Center + Direction * Sign * 7.f * Scale;
					Stroke({Tip - Direction * Sign * 3.f * Scale + Side * 3.f * Scale, Tip,
						Tip - Direction * Sign * 3.f * Scale - Side * 3.f * Scale}, 1.5f);
				}
			}
		}
		return Layer + 1;
	}
private:
	float Clock = 0.f;
};

UFrostMarkVisual::UFrostMarkVisual(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetWidgetSpace(EWidgetSpace::World); SetDrawSize(FVector2D(96, 144));
	SetPivot(FVector2D(.5f, .5f)); SetBlendMode(EWidgetBlendMode::Transparent);
	SetBackgroundColor(FLinearColor::Transparent); SetTwoSided(true); SetWindowFocusable(false);
	SetManuallyRedraw(true); SetTickMode(ETickMode::Enabled); SetTickWhenOffscreen(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision); SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false); SetCastShadow(false); SetReceivesDecals(false);
	SetAbsolute(false, true, true); SetRelativeScale3D(FVector(.7f));
	SetVisibility(false); SetHiddenInGame(true);
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bStartWithTickEnabled = false; bTickInEditor = false;
}

void UFrostMarkVisual::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		ObservedHealth = Owner->FindComponentByClass<UHealthComponent>();
		if (ObservedHealth.IsValid()) ObservedHealth->OnDeath.AddUniqueDynamic(this, &UFrostMarkVisual::HandleDeath);
	}
}

void UFrostMarkVisual::SetFrosted(bool bInFrosted)
{
	bFrosted = bInFrosted && (!ObservedHealth.IsValid() || !ObservedHealth->IsDead());
	if (bFrosted)
	{
		if (!Glyph.IsValid()) { Glyph = SNew(SFrostGlyph); SetSlateWidget(Glyph); }
		Glyph->SetClock(Clock); FaceCamera(); RequestRedraw();
	}
	SetVisibility(bFrosted); SetHiddenInGame(!bFrosted); SetComponentTickEnabled(bFrosted);
}

void UFrostMarkVisual::FaceCamera()
{
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector Location; FRotator Rotation; PC->GetPlayerViewPoint(Location, Rotation);
		const FVector Facing = Location - GetComponentLocation();
		if (!Facing.IsNearlyZero()) SetWorldRotation(FRotationMatrix::MakeFromXZ(Facing, Rotation.RotateVector(FVector::UpVector)).Rotator());
	}
}

void UFrostMarkVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	if (ObservedHealth.IsValid() && ObservedHealth->IsDead()) SetFrosted(false);
	if (bFrosted)
	{
		if (!Glyph.IsValid()) SetFrosted(true);
		if (GetWorld() && !GetWorld()->IsPaused() && UGameplayStatics::GetGlobalTimeDilation(this) >= .01f)
			Clock = FMath::Fmod(Clock + DeltaTime, 3.6f);
		Glyph->SetClock(Clock); FaceCamera(); RequestRedraw();
	}
	Super::TickComponent(DeltaTime, TickType, TickFunction);
}

void UFrostMarkVisual::HandleDeath(UHealthComponent*) { SetFrosted(false); }
void UFrostMarkVisual::EndPlay(const EEndPlayReason::Type Reason)
{
	SetFrosted(false);
	if (ObservedHealth.IsValid()) ObservedHealth->OnDeath.RemoveDynamic(this, &UFrostMarkVisual::HandleDeath);
	ObservedHealth.Reset(); Super::EndPlay(Reason);
}
void UFrostMarkVisual::ReleaseResources() { Super::ReleaseResources(); Glyph.Reset(); }
FString UFrostMarkVisual::DebugVisualState() const
{
	return FString::Printf(TEXT("{\"active\":%s,\"visible\":%s,\"clock\":%.4f,\"registered\":%s}"),
		bFrosted ? TEXT("true") : TEXT("false"), IsVisible() ? TEXT("true") : TEXT("false"),
		Clock, IsRegistered() ? TEXT("true") : TEXT("false"));
}
