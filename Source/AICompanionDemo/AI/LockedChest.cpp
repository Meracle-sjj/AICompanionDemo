/**
 * [INPUT]: 依赖 LockedChest.h 的组件契约、Engine 的 StaticMesh/PointLight/MaterialInstanceDynamic、DrawDebugHelpers（大奖爆闪/失败短提示）、Kismet 的 GetPlayerPawn/GetGameMode、WaveDirector实际金币差额与奖励口、UI/CombatFeedbackSubsystem金币显示、AICompanionDemoCharacter持钥查询
 * [OUTPUT]: 实际网格九点顺坡/轻埋沙；接近平滑显现钥匙无价格数字，持钥近身E提示；保留15金+免费六选二与原半径，仅钱包实际正增量显示金币图标，成功统一前三台词和短SFX，开启/死亡/终局/EndPlay清理。
 * [POS]: AI/ 交互物家族实现第四件：大奖的"金色夸张度"必须压过普通宝箱——免费三选一是核心甜头（普通箱花钱、锁箱白给）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "LockedChest.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "AICompanionDemoGameMode.h"
#include "Audio/SFXLibrary.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/CombatFeedbackSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Vfx/ChestGrounding.h"
#include "Vfx/ChestMarkerVisual.h"

namespace
{
	// 锁箱浓金（比普通宝箱宝蓝更"贵"的语言）
	const FLinearColor kLockedGold(1.f, 0.78f, 0.25f);
}

ALockedChest::ALockedChest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	ChestMarker = CreateDefaultSubobject<UChestMarkerVisual>(TEXT("ChestMarker"));
	ChestMarker->SetupAttachment(RootComponent);

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

	if (!ChestGrounding::Place(this, BodyMesh, GroundContact))
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[LockedChest] 实际网格探地失败：%s，保留生成位置供诊断"), *GetActorLocation().ToCompactString());
	}
	DrawInteractHint();
}

void ALockedChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bOpened)
	{
		return;
	}
	const AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* Health = Hero ? Hero->FindComponentByClass<UHealthComponent>() : nullptr;
	if ((Health && Health->IsDead()) || (Director && Director->IsRunTerminated()))
	{
		if (ChestMarker) { ChestMarker->Retire(); }
		return;
	}
	if (GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f) { return; }
	DrawInteractHint();
}

void ALockedChest::DrawInteractHint()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(PlayerPawn);
	if (ChestMarker)
	{
		const bool bHasKey = PlayerChar && PlayerChar->HasTreasureKey();
		const bool bNearby = PlayerPawn && FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= InteractRadius;
		ChestMarker->SetWorldLocation(ChestGrounding::MarkerAnchor(BodyMesh));
		ChestMarker->Present(EChestMarkerKind::Key, 0, bHasKey && bNearby, bHasKey);
	}
}

void ALockedChest::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ChestMarker) { ChestMarker->Retire(); }
	Super::EndPlay(Reason);
}

FString ALockedChest::DebugChestState() const
{
#if WITH_EDITOR
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	return FString::Printf(TEXT("{\"locked\":true,\"opened\":%s,\"goldReward\":%d,\"gold\":%d,\"hasKey\":%s,\"interactRadius\":%.3f,\"contact\":%s,\"marker\":%s}"),
		bOpened ? TEXT("true") : TEXT("false"), GoldReward, Director ? Director->GetGold() : -1,
		Hero && Hero->HasTreasureKey() ? TEXT("true") : TEXT("false"), InteractRadius,
		*GroundContact.ToJson(), *(ChestMarker ? ChestMarker->DebugVisualState() : TEXT("{}")));
#else
	return TEXT("{}");
#endif
}

bool ALockedChest::DebugTryOpen()
{
#if WITH_EDITOR
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Hero || FVector::DistSquared(Hero->GetActorLocation(), GetActorLocation()) > FMath::Square(InteractRadius)) { return false; }
	return TryOpen();
#else
	return false;
#endif
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
	if (ChestMarker) { ChestMarker->Retire(); }

	// 大奖：15 金 + 免费三选一（普通箱花钱买、锁箱白给——甜头必须压过普通箱）
	const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
	AWaveDirector* Director = GameMode ? GameMode->GetWaveDirector() : nullptr;
	if (Director)
	{
		const int32 GoldBefore = Director->GetGold();
		Director->AddGold(GoldReward);
		const int32 ActualGoldGained = FMath::Max(Director->GetGold() - GoldBefore, 0);
		if (ActualGoldGained > 0)
		{
			if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
			{
				UCombatFeedbackSubsystem::ShowGold(GetWorld(), PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 200.f), ActualGoldGained);
			}
		}
	}

	PlayRewardFx();
	SetLifeSpan(0.9f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[LockedChest] 开锁！奖励 %d 金 + 免费三选一"), GoldReward);

	// 免费三选一最后触发（进暂停冻结世界——爆闪的 0.9s 会在暂停里延后，观感为"锁箱爆闪定格选卡"）
	if (Director)
	{
		Director->GrantFreeUpgrade();
		if (!Director->IsRunTerminated())
		{
			USFXLibrary::PlaySFX(GetWorld(), EGameSFX::ChestOpen);
			USFXLibrary::PlayChestOpenVoice(GetWorld(), GetActorLocation());
		}
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
