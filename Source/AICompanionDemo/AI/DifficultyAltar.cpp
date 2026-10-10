/**
 * [INPUT]: 依赖 DifficultyAltar.h 的组件契约、Engine 的 StaticMesh/PointLight/MaterialInstanceDynamic、DrawDebugHelpers（猩红光柱谢幕）、Kismet 的 GetPlayerPawn、本模块 WaveDirector（TrySpendGold/IncreaseDifficulty/难度乘子读数）
 * [OUTPUT]: 对外提供 ADifficultyAltar 的实现——Tick 接近提示（未献祭期）→ TrySacrifice 三步：扣费 → 难度+1（头顶播报当前档与乘子）→ 猩红光柱谢幕 0.8s 销毁
 * [POS]: AI/ 交互物家族实现第三件：每座祭坛一档，多座祭坛=玩家自选的加压梯度——"求战者加码、避战者绕行"的双路线
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "DifficultyAltar.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 祭坛猩红（危险交易的语言——与信标青/宝箱金分家）
	const FLinearColor kAltarRed(1.f, 0.25f, 0.15f);
}

ADifficultyAltar::ADifficultyAltar()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	AltarLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AltarLight"));
	AltarLight->SetupAttachment(RootComponent);
	AltarLight->SetRelativeLocation(FVector(0.f, 0.f, 130.f));
	AltarLight->SetLightColor(kAltarRed);
	AltarLight->SetIntensity(9.f);
	AltarLight->SetAttenuationRadius(650.f);
	AltarLight->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BasicMat = BasicMatFinder.Succeeded() ? BasicMatFinder.Object : nullptr;
	if (CylinderFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 0.9f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BasicMat)
	{
		BodyMesh->SetMaterial(0, BasicMat);
	}
}

void ADifficultyAltar::BeginPlay()
{
	Super::BeginPlay();

	// 换装（信标同款）：ini 生效即 Fab 仪式石顶替几何柱
	if (MeshOverride.ToSoftObjectPath().IsValid())
	{
		if (UStaticMesh* Skinned = MeshOverride.LoadSynchronous())
		{
			BodyMesh->SetStaticMesh(Skinned);
			BodyMesh->SetRelativeScale3D(FVector(MeshOverrideScale));
			for (int32 Slot = 0; Slot < BodyMesh->GetNumMaterials(); ++Slot)
			{
				BodyMesh->SetMaterial(Slot, nullptr);
			}
		}
		else
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Altar] 换装加载失败：%s——回退几何柱"), *MeshOverride.ToSoftObjectPath().ToString());
		}
	}
	else if (BasicMat)
	{
		if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), kAltarRed);
		}
	}

	// 探地落位（信标同款）
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(AltarDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y,
			FloorHit.ImpactPoint.Z + MeshOverrideScale * 25.f));
	}
}

void ADifficultyAltar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bSacrificed)
	{
		return;
	}
	DrawInteractHint();
}

void ADifficultyAltar::DrawInteractHint()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	if (FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= InteractRadius)
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 90.f),
			FString::Printf(TEXT("按 E 献祭（%d 金）——难度 +1，敌更强但奖励更肥"), SacrificeCost),
			this, FColor::Red, 0.f, false, 1.1f);
	}
}

bool ADifficultyAltar::TrySacrifice()
{
	if (bSacrificed)
	{
		return false;
	}

	const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
	AWaveDirector* Director = GameMode ? GameMode->GetWaveDirector() : nullptr;
	if (!Director)
	{
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn || !Director->TrySpendGold(SacrificeCost))
	{
		if (PlayerPawn)
		{
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 180.f),
				FString::Printf(TEXT("金币不足（还差 %d）"), SacrificeCost - Director->GetGold()),
				PlayerPawn, FColor::Silver, 1.0f, false, 1.f);
		}
		return false;
	}

	bSacrificed = true;
	Director->IncreaseDifficulty();

	// 播报当前加压结果（猩红光柱 + 头顶档位播报——"你到底买了什么"全程透明）
	DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + FVector(0.f, 0.f, 3200.f),
		FColor::Red, false, 0.8f, 0, 14.f);
	if (PlayerPawn)
	{
		DrawDebugString(GetWorld(), PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 200.f),
			FString::Printf(TEXT("难度 %d 档！敌 HP ×%.2f | 经验 ×%.2f"),
				Director->GetDifficultyLevel(), Director->GetDifficultyHpMult(), Director->GetDifficultyXpMult()),
			PlayerPawn, FColor::Red, 2.5f, false, 1.2f);
	}
	SetLifeSpan(0.8f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Altar] 献祭完成——难度 %d 档（敌 HP ×%.2f 经验 ×%.2f）"),
		Director->GetDifficultyLevel(), Director->GetDifficultyHpMult(), Director->GetDifficultyXpMult());
	return true;
}
