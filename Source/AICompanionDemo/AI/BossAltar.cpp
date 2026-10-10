/**
 * [INPUT]: 依赖 BossAltar.h 的组件契约、Engine 的 StaticMeshComponent/PointLightComponent/BasicShapeMaterial 红染（信标黑柱案正解：SetMaterial(0, BasicMat)）、DrawDebugHelpers（常驻红柱）、Kismet 的 GetPlayerPawn、本模块 BossBase（激活 spawn）
 * [OUTPUT]: 对外提供 ABossAltar 的实现——构造（红染圆柱 1.5m 祭坛+红点光）+ BeginPlay 探地落位（8000/24000 高空化同款）+ Tick（常驻红柱 25m 标识 + 玩家接近 500cm 自动 ActivateBoss）+ ActivateBoss（祭坛原位 spawn Boss 后自毁）
 * [POS]: AI/ 的 Boss 终局引导件实现：祭坛出现本身即是事件（红柱+光），玩家走近=选择应战时机
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "BossAltar.h"

#include "AI/BossBase.h"
#include "AICompanionDemo.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FLinearColor kBossAltarRed(0.9f, 0.12f, 0.08f);
}

ABossAltar::ABossAltar()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	AltarLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AltarLight"));
	AltarLight->SetupAttachment(RootComponent);
	AltarLight->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	AltarLight->SetLightColor(kBossAltarRed);
	AltarLight->SetIntensity(1200.f);   // 与钥匙点光同标准：室外日照下要上千坎德拉才可见
	AltarLight->SetAttenuationRadius(3000.f);
	AltarLight->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(1.5f, 1.5f, 1.2f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BasicMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, BasicMatFinder.Object);   // 信标黑柱案正解：槽 0 显式挂 BasicMat 再染红
		if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), kBossAltarRed);
		}
	}
}

void ABossAltar::BeginPlay()
{
	Super::BeginPlay();

	// 探地落位（高空化同款：起点 +80m 探 240m——v15 全地形覆盖）
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(BossAltarDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorHit.ImpactPoint.Z));
	}
}

void ABossAltar::SetBossInfo(TSubclassOf<ABossBase> InBossClass, const FString& InName, int32 InIndex)
{
	BossClass = InBossClass;
	BossName = InName;
	BossIndex = InIndex;
}

void ABossAltar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 常驻红色光柱（09-30 用户点名"红色或骷髅头标出"——DrawDebugLine 每帧重画，25m 外可见）
	DrawDebugLine(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f),
		GetActorLocation() + FVector(0.f, 0.f, 3000.f), FColor::Red, false, 0.1f, 0, 12.f);

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn && FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= 500.f)
	{
		ActivateBoss();
	}
}

void ABossAltar::ActivateBoss()
{
	if (!BossClass)
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[BossAltar] BossClass 为空——激活失败（注入链断裂，检查 WaveDirector 构造）"));
		Destroy();
		return;
	}

	// 09-30 用户实测"祭坛触发但 Boss 不出现"定罪：spawn 位置=祭坛地表 Z 无抬升，大胶囊半截嵌入地形
	// → 默认碰撞策略直接销毁新生成体。修复=强制生成+抬升 180cm（AdjustIfPossibleButAlwaysSpawn）
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector SpawnLoc = GetActorLocation() + FVector(0.f, 0.f, 180.f);
	AActor* Spawned = GetWorld()->SpawnActor<ABossBase>(BossClass, SpawnLoc, FRotator::ZeroRotator, Params);
	if (!Spawned)
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[BossAltar] Boss spawn 失败（类=%s）——失败必须可见"), *BossClass->GetName());
		Destroy();
		return;
	}
	if (ABossBase* Boss = Cast<ABossBase>(Spawned))
	{
		Boss->SetBossIdentity(BossName, BossIndex);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[BossAltar] 祭坛激活——%s 降世！"), *BossName);

	// 红光爆闪谢幕后自毁（祭坛使命完成）
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 100.f), 200.f, 16, FColor::Red, false, 0.8f, 0, 6.f);
	Destroy();
}
