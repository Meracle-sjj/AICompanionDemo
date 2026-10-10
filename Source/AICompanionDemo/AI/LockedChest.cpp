/**
 * [INPUT]: 依赖 LockedChest.h 的组件契约、Engine 的 StaticMesh/PointLight/MaterialInstanceDynamic、DrawDebugHelpers（大奖爆闪）、Kismet 的 GetPlayerPawn/GetGameMode、本模块 WaveDirector（AddGold/GrantFreeUpgrade 奖励口）、本模块 AICompanionDemoCharacter 的持钥查询
 * [OUTPUT]: 对外提供 ALockedChest 的实现——Tick 双文案提示（无钥"需要宝箱钥匙"灰字 / 有钥"按 E 开锁"金字）→ TryOpen：持钥校验 → AddGold(15) + GrantFreeUpgrade（免费三选一）→ 三层金爆闪谢幕 0.9s 销毁
 * [POS]: AI/ 交互物家族实现第四件：大奖的"金色夸张度"必须压过普通宝箱——免费三选一是核心甜头（普通箱花钱、锁箱白给）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "LockedChest.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
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
	// 锁箱浓金（比普通宝箱宝蓝更"贵"的语言）
	const FLinearColor kLockedGold(1.f, 0.78f, 0.25f);
}

ALockedChest::ALockedChest()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	ChestLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ChestLight"));
	ChestLight->SetupAttachment(RootComponent);
	ChestLight->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	ChestLight->SetLightColor(kLockedGold);
	ChestLight->SetIntensity(10.f);
	ChestLight->SetAttenuationRadius(700.f);
	ChestLight->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BasicMat = BasicMatFinder.Succeeded() ? BasicMatFinder.Object : nullptr;
	if (CubeFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(1.2f, 0.9f, 0.9f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BasicMat)
	{
		BodyMesh->SetMaterial(0, BasicMat);
	}
}

void ALockedChest::BeginPlay()
{
	Super::BeginPlay();

	// 换装：Fab 带锁巨箱顶替几何箱
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
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[LockedChest] 换装加载失败：%s——回退几何箱"), *MeshOverride.ToSoftObjectPath().ToString());
		}
	}
	else if (BasicMat)
	{
		if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), kLockedGold);
		}
	}

	// 探地落位（交互物家族标配）
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(LockedChestDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y,
			FloorHit.ImpactPoint.Z + MeshOverrideScale * 90.f));
	}
}

void ALockedChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bOpened)
	{
		return;
	}
	DrawInteractHint();
}

void ALockedChest::DrawInteractHint()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	const AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(PlayerPawn);
	if (FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) > InteractRadius)
	{
		return;
	}

	if (PlayerChar && PlayerChar->HasTreasureKey())
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 90.f),
			FString::Printf(TEXT("按 E 开锁！奖励 %d 金 + 免费升级"), GoldReward),
			this, FColor::Yellow, 0.f, false, 1.2f);
	}
	else
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 90.f),
			TEXT("上锁的大箱——需要宝箱钥匙"),
			this, FColor::Silver, 0.f, false, 1.1f);
	}
}

bool ALockedChest::TryOpen()
{
	if (bOpened)
	{
		return false;
	}

	const AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(
		UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!PlayerChar || !PlayerChar->HasTreasureKey())
	{
		// 无钥匙：头顶飘提示（交互资格未达——不是失败，是钩子）
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 180.f),
				TEXT("上锁了——去地图上找宝箱钥匙"), PlayerPawn, FColor::Silver, 1.2f, false, 1.f);
		}
		return false;
	}

	bOpened = true;

	// 大奖：15 金 + 免费三选一（普通箱花钱买、锁箱白给——甜头必须压过普通箱）
	const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
	AWaveDirector* Director = GameMode ? GameMode->GetWaveDirector() : nullptr;
	if (Director)
	{
		Director->AddGold(GoldReward);
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			DrawDebugString(GetWorld(), PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 200.f),
				FString::Printf(TEXT("+%d 金！"), GoldReward), PlayerPawn, FColor::Yellow, 1.5f, false, 1.3f);
		}
	}

	PlayRewardFx();
	SetLifeSpan(0.9f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[LockedChest] 开锁！奖励 %d 金 + 免费三选一"), GoldReward);

	// 免费三选一最后触发（进暂停冻结世界——爆闪的 0.9s 会在暂停里延后，观感为"锁箱爆闪定格选卡"）
	if (Director)
	{
		Director->GrantFreeUpgrade();
	}
	return true;
}

void ALockedChest::PlayRewardFx()
{
	DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + FVector(0.f, 0.f, 3200.f),
		FColor::Yellow, false, 0.9f, 0, 16.f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), 50.f, 12, FColor::Yellow, false, 0.7f, 0, 3.f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), 85.f, 12, FColor::Yellow, false, 0.5f, 0, 2.f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), 120.f, 12, FColor::Yellow, false, 0.3f, 0, 1.5f);
}
