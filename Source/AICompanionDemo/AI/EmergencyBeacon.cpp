/**
 * [INPUT]: 依赖 EmergencyBeacon.h 的组件契约、Engine 的 StaticMesh/PointLight/MaterialInstanceDynamic、DrawDebugHelpers（拾取闪光）、Kismet 的 GetPlayerPawn、本模块 AICompanionDemoCharacter 的解锁口（UnlockRecall）、本模块 Audio/SFXLibrary（Recall 事件音效——信标拾取复用）
 * [OUTPUT]: 对外提供 AEmergencyBeacon 的实现——Tick 双段（待机浮动自转 / 玩家入圈 Collect）；Collect = 角色首次解锁/重复降低召回CD + 青色光柱教学闪 + 头顶飘字 + 自毁
 * [POS]: AI/ 拾取物家族实现第四系：**拾取即解锁能力**新语义（蛋糕=即时消耗 / 经验金币=账本入账 / 信标=技能解锁）；一审固定交互点方案已废
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "EmergencyBeacon.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
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
	// 信标青（与经验晶体同族——"成长/支援"色语言）
	const FLinearColor kBeaconCyan(0.2f, 0.85f, 1.f);
}

AEmergencyBeacon::AEmergencyBeacon()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	// 常驻青光：交互物的远距离邀约（宝箱金光/信标青光——色语言分家）
	BeaconLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(RootComponent);
	BeaconLight->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	BeaconLight->SetLightColor(kBeaconCyan);
	BeaconLight->SetIntensity(8.f);
	BeaconLight->SetAttenuationRadius(600.f);
	BeaconLight->SetCastShadows(false);

	// 缺省展示体：基础柱（换装 ini 生效后由 Fab 方碑顶替）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BasicMat = BasicMatFinder.Succeeded() ? BasicMatFinder.Object : nullptr;
	if (CylinderFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 2.1f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // 家族哲学：纯距离判定
	if (BasicMat)
	{
		BodyMesh->SetMaterial(0, BasicMat);
	}
}

void AEmergencyBeacon::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;

	// 换装（TreasureChest 同款）：ini 生效即 Fab 方碑顶替几何柱，原生材质说话
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
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Beacon] 换装加载失败：%s——回退几何柱"), *MeshOverride.ToSoftObjectPath().ToString());
		}
	}
	else if (BasicMat)
	{
		// 几何柱兜底染色（青）
		if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), kBeaconCyan);
		}
	}

	// 探地落位（宝箱同款）：出生点 Z 是摆点常数，504m 起伏地形下必须贴地——ByObjectType（怪胶囊不挡线）
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(BeaconDrop), /*bTraceComplex*/ false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y,
			FloorHit.ImpactPoint.Z + MeshOverrideScale * 26.f));
	}
	// 09-25 浮空案根因：Tick 的浮动叠加基于 RestLocation（出生点快照）——探地挪正后不刷新它，
	// 下一帧就被浮动画回出生高度（用户实测"信标浮在空中完全交互不到"）
	RestLocation = GetActorLocation();
}

void AEmergencyBeacon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 待机：浮动自转（拾取物家族通用语言——"可以捡"的形体语言）
	BobPhase += DeltaSeconds * 2.f;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * 8.f));

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	if (FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= CollectRadius)
	{
		Collect();
	}
}

void AEmergencyBeacon::Collect()
{
	// 解锁玩家 C 键召回（能力授予走角色——技能冷却/按键归角色自持，信标只管"发技能"）
	AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(
		UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (PlayerChar)
	{
		const bool bAlreadyUnlocked = PlayerChar->IsRecallUnlocked();
		PlayerChar->UnlockRecall();
		DrawDebugString(GetWorld(), PlayerChar->GetActorLocation() + FVector(0.f, 0.f, 200.f),
			bAlreadyUnlocked ? *FString::Printf(TEXT("召回冷却 %.0f 秒"), PlayerChar->GetRecallCooldownTotal()) : TEXT("获得紧急呼叫！按 C 召回 AI 队友"), PlayerChar, FColor::Cyan, 2.5f, false, 1.2f);
	}

	// 青色光柱谢幕 + 拾取闪光（传送仪式感的预告）
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Recall);   // [SFX] 信标拾取——复用紧急呼叫槽（技能授予与后续召回同声同源）
	DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + FVector(0.f, 0.f, 3000.f),
		FColor::Cyan, false, 0.8f, 0, 12.f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), 80.f, 12, FColor::Cyan, false, 0.6f, 0, 2.f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Beacon] 拾取！C 键召回技能已解锁"));
	Destroy();
}
