/**
 * [INPUT]: EndingPortal.h组件；UE基础Sphere/Cube/Cylinder几何与BasicShapeMaterial；WorldStatic探地、E键真实状态、宿主正式校验
 * [OUTPUT]: 48段紫环/12枚金色晶体旋转；近门纯提示E交互，不切鼠标输入；新资格首帧不接按键，后续先松E再接新按下，避免领奖同帧穿透；死亡/终局/卸载移除视图
 * [POS]: AI/归途门实现。资源组件归Actor，提示UPROPERTY归Actor；提交后不再访问可能已销毁的门
 * [PROTOCOL]: 变更时更新本头部，然后检查模块AGENTS.md
 */
#include "EndingPortal.h"
#include "AICompanionDemoGameMode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Health/HealthComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/EndingScreen.h"
#include "UObject/ConstructorHelpers.h"

AEndingPortal::AEndingPortal()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PortalRoot"));
	RootComponent = SceneRoot;
	PurpleRing = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PurpleRing"));
	PurpleRing->SetupAttachment(SceneRoot);
	GoldOrbit = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GoldOrbit"));
	GoldOrbit->SetupAttachment(SceneRoot);
	PortalFace = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalFace"));
	PortalFace->SetupAttachment(SceneRoot);
	PortalLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PortalLight"));
	PortalLight->SetupAttachment(SceneRoot);
	PortalLight->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	PortalLight->SetLightColor(FLinearColor(.72f, .15f, 1.f));
	PortalLight->SetIntensity(1200.f);
	PortalLight->SetAttenuationRadius(750.f);
	PortalLight->SetCastShadows(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BasicMaterial = Material.Object;
	if (Sphere.Succeeded()) { PurpleRing->SetStaticMesh(Sphere.Object); }
	if (Cube.Succeeded()) { GoldOrbit->SetStaticMesh(Cube.Object); }
	if (Cylinder.Succeeded()) { PortalFace->SetStaticMesh(Cylinder.Object); }
	for (UStaticMeshComponent* Mesh : { static_cast<UStaticMeshComponent*>(PurpleRing), static_cast<UStaticMeshComponent*>(GoldOrbit), PortalFace.Get() })
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		if (BasicMaterial) { Mesh->SetMaterial(0, BasicMaterial); }
	}
	PurpleRing->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	GoldOrbit->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	PortalFace->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	PortalFace->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	PortalFace->SetRelativeScale3D(FVector(3.2f, 3.2f, .035f));
}

void AEndingPortal::BeginPlay()
{
	Super::BeginPlay();
	Host = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	FHitResult Hit;
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EndingPortalGround), false, this);
	const FVector Top = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.f, 0.f, 24000.f), Objects, Params))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, Hit.ImpactPoint.Z + 5.f));
	}
	if (UMaterialInstanceDynamic* Mid = PurpleRing->CreateAndSetMaterialInstanceDynamic(0)) { Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(.74f, .10f, 1.f)); }
	if (UMaterialInstanceDynamic* Mid = GoldOrbit->CreateAndSetMaterialInstanceDynamic(0)) { Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, .68f, .16f)); }
	if (UMaterialInstanceDynamic* Mid = PortalFace->CreateAndSetMaterialInstanceDynamic(0)) { Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(.21f, .025f, .43f)); }
	for (int32 Index = 0; Index < 48; ++Index)
	{
		const float Angle = Index * 2.f * PI / 48.f;
		const FVector Position(0.f, FMath::Cos(Angle) * 190.f, FMath::Sin(Angle) * 190.f);
		PurpleRing->AddInstance(FTransform(FRotator::ZeroRotator, Position, FVector(.27f)));
	}
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Angle = Index * 2.f * PI / 12.f;
		const FVector Position(0.f, FMath::Cos(Angle) * 235.f, FMath::Sin(Angle) * 235.f);
		GoldOrbit->AddInstance(FTransform(FRotator(45.f, 0.f, FMath::RadiansToDegrees(Angle)), Position, FVector(.12f, .20f, .20f)));
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// 确保本帧PlayerInput已处理，再消费WasInputKeyJustPressed边沿。
		AddTickPrerequisiteActor(PC);
		Prompt = CreateWidget<UEndingPortalPrompt>(PC, UEndingPortalPrompt::StaticClass());
		if (Prompt)
		{
			Prompt->SetVisibility(ESlateVisibility::Collapsed);
			Prompt->AddToViewport(45);
		}
	}
}

bool AEndingPortal::IsPlayerNear() const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* Health = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	return Player && Health && !Health->IsDead() && FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) <= InteractRadius
		&& FMath::Abs(Player->GetActorLocation().Z - GetActorLocation().Z) < 400.f;
}

void AEndingPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PurpleRing->AddLocalRotation(FRotator(0.f, 0.f, DeltaSeconds * 35.f));
	GoldOrbit->AddLocalRotation(FRotator(0.f, 0.f, DeltaSeconds * -22.f));
	AAICompanionDemoGameMode* GM = Host.Get();
	const bool bNear = IsPlayerNear();
	const bool bAllowed = GM && GM->CanUseEndingPortal(this);
	if (Prompt)
	{
		Prompt->SetCanEnter(bAllowed);
		Prompt->SetVisibility(bNear ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const bool bEligible = bNear && bAllowed && PC;
	const bool bWasEligible = bInteractionEligible;
	const bool bWasArmed = bInteractKeyArmed;
	bInteractionEligible = bEligible;
	if (!bEligible || !bWasEligible)
	{
		// 奖励结束/走近的首帧不能消费原来的E；也不在这一帧凭松键重新上膛。
		bInteractKeyArmed = false;
		return;
	}
	const bool bPressed = PC->WasInputKeyJustPressed(EKeys::E);
	const bool bHeld = PC->IsInputKeyDown(EKeys::E);
	if (bWasArmed && bPressed)
	{
		bInteractKeyArmed = false;
		TryInteract();
		return;
	}
	// 先用旧armed判断，后用真实键状态更新；同帧down/up仍有bPressed，不能重新上膛。
	bInteractKeyArmed = !bHeld && !bPressed;
}

FString AEndingPortal::DebugInteractionState() const
{
#if WITH_EDITOR
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	return FString::Printf(TEXT("{\"key\":\"E\",\"near\":%s,\"eligible\":%s,\"armed\":%s,\"keyDown\":%s,\"promptShown\":%s}"),
		IsPlayerNear() ? TEXT("true") : TEXT("false"), bInteractionEligible ? TEXT("true") : TEXT("false"),
		bInteractKeyArmed ? TEXT("true") : TEXT("false"), PC && PC->IsInputKeyDown(EKeys::E) ? TEXT("true") : TEXT("false"),
		Prompt && Prompt->GetVisibility() != ESlateVisibility::Collapsed ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}

bool AEndingPortal::DebugInjectInteractKey(bool bDown)
{
#if WITH_EDITOR
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->PlayerInput) { return false; }
	// 使用引擎显式标记的合成事件，不直接改latch或调用TryInteract。
	PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	// InputKey返回的是Action绑定处理状态；E由轮询使用，成功收集后不要求Action绑定。
	return true;
#else
	return false;
#endif
}

bool AEndingPortal::TryInteract()
{
	AAICompanionDemoGameMode* GM = Host.Get();
	return GM && GM->TryEnterEndingPortal(this);
}

void AEndingPortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Prompt) { Prompt->RemoveFromParent(); Prompt = nullptr; }
	Super::EndPlay(EndPlayReason);
}
