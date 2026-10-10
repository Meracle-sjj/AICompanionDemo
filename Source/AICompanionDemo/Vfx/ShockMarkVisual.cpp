/** [INPUT]: ShockMarkVisual、Slate MakeLines、Health真实死亡委托。
 * [OUTPUT]: 稳定原创闪电字形；同一组件刷新不重建，深色衬线保证沙地可见。
 * [POS]: World Widget纯显示；相机朝向/停Tick/死亡及EndPlay资源清理。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md */
#include "ShockMarkVisual.h"
#include "Health/HealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

class SShockGlyph final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShockGlyph) {} SLATE_END_ARGS()
	void Construct(const FArguments&) { SetCanTick(false); SetVisibility(EVisibility::HitTestInvisible); }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(96.f,96.f); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D Size = Geometry.GetLocalSize();
		const float Scale = FMath::Min(Size.X, Size.Y)/96.f;
		const FVector2f Offset((Size.X-96.f*Scale)*.5f,(Size.Y-96.f*Scale)*.5f);
		TArray<FVector2f> Bolt;
		for (const FVector2f P : {FVector2f(59,12),FVector2f(31,51),FVector2f(50,49),FVector2f(37,83)})
			Bolt.Add(Offset+P*Scale);
		const auto Paint = [&](const FLinearColor& Color, float Width, int32 AtLayer)
		{
			FSlateDrawElement::MakeLines(Elements,AtLayer,Geometry.ToPaintGeometry(),Bolt,
				ESlateDrawEffect::None,Color*Style.GetColorAndOpacityTint(),true,Width*Scale);
		};
		Paint(FLinearColor(.005f,.025f,.07f,.95f),14.f,Layer);
		Paint(FLinearColor(.12f,.68f,1.f,1.f),9.f,Layer+1);
		Paint(FLinearColor(.85f,.97f,1.f,1.f),3.f,Layer+2);
		return Layer+2;
	}
};

UShockMarkVisual::UShockMarkVisual(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetWidgetSpace(EWidgetSpace::World); SetDrawSize(FVector2D(96,96));
	SetPivot(FVector2D(.5f,.5f)); SetBlendMode(EWidgetBlendMode::Transparent);
	SetBackgroundColor(FLinearColor::Transparent); SetTwoSided(true); SetWindowFocusable(false);
	SetManuallyRedraw(true); SetTickMode(ETickMode::Enabled); SetTickWhenOffscreen(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision); SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false); SetCastShadow(false); SetReceivesDecals(false);
	SetAbsolute(false,true,true); SetRelativeScale3D(FVector(.46f));
	SetVisibility(false); SetHiddenInGame(true);
	PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
	PrimaryComponentTick.bStartWithTickEnabled=false; bTickInEditor=false;
}

void UShockMarkVisual::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner=GetOwner())
	{
		ObservedHealth=Owner->FindComponentByClass<UHealthComponent>();
		if (ObservedHealth.IsValid()) ObservedHealth->OnDeath.AddUniqueDynamic(this,&UShockMarkVisual::HandleDeath);
	}
}

void UShockMarkVisual::SetShocked(bool bInShocked)
{
	bShocked=bInShocked && (!ObservedHealth.IsValid() || !ObservedHealth->IsDead());
	if (bShocked)
	{
		if (!Glyph.IsValid()) { Glyph=SNew(SShockGlyph); SetSlateWidget(Glyph); RequestRedraw(); }
		FaceCamera();
	}
	SetVisibility(bShocked); SetHiddenInGame(!bShocked); SetComponentTickEnabled(bShocked);
}

void UShockMarkVisual::FaceCamera()
{
	if (const APlayerController* PC=UGameplayStatics::GetPlayerController(this,0))
	{
		FVector Location; FRotator Rotation; PC->GetPlayerViewPoint(Location,Rotation);
		const FVector Facing=Location-GetComponentLocation();
		if (!Facing.IsNearlyZero()) SetWorldRotation(FRotationMatrix::MakeFromXZ(Facing,Rotation.RotateVector(FVector::UpVector)).Rotator());
	}
}

void UShockMarkVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	if (ObservedHealth.IsValid() && ObservedHealth->IsDead()) SetShocked(false);
	if (bShocked) FaceCamera();
	Super::TickComponent(DeltaTime,TickType,TickFunction);
}

void UShockMarkVisual::HandleDeath(UHealthComponent*) { SetShocked(false); }
void UShockMarkVisual::EndPlay(const EEndPlayReason::Type Reason)
{
	SetShocked(false);
	if (ObservedHealth.IsValid()) ObservedHealth->OnDeath.RemoveDynamic(this,&UShockMarkVisual::HandleDeath);
	ObservedHealth.Reset(); Super::EndPlay(Reason);
}
void UShockMarkVisual::ReleaseResources() { Super::ReleaseResources(); Glyph.Reset(); }
FString UShockMarkVisual::DebugVisualState() const
{
	return FString::Printf(TEXT("{\"active\":%s,\"visible\":%s,\"hidden\":%s,\"widget_ready\":%s,\"render_target\":%s,\"tick\":%s,\"registered\":%s,\"collision\":%d}"),
		bShocked?TEXT("true"):TEXT("false"),IsVisible()?TEXT("true"):TEXT("false"),bHiddenInGame?TEXT("true"):TEXT("false"),
		Glyph.IsValid()&&GetSlateWidget()==Glyph?TEXT("true"):TEXT("false"),GetRenderTarget()?TEXT("true"):TEXT("false"),
		IsComponentTickEnabled()?TEXT("true"):TEXT("false"),IsRegistered()?TEXT("true"):TEXT("false"),static_cast<int32>(GetCollisionEnabled()));
}
