/**
 * [INPUT]: 依赖 TreasureChest.h 的组件契约、Engine 的 StaticMesh/MaterialInstanceDynamic（/Engine 基础立方体 + BasicShapeMaterial 染色，参数真名 "Color"——09-12 实证）、DrawDebugHelpers（提示与爆闪）、Kismet 的 GetPlayerPawn/GetGameMode（玩家距离与组合根）、本模块 Audio/SFXLibrary（ChestOpen 事件音效）
 * [OUTPUT]: 普通箱动态收费；初始箱自有金色闪光/MID与免费提示，TryGrantStarterUpgrade免费三选一不计收费箱。E交互/已开门控/开箱反馈共用，原Fab材质与资源不改。
 * [POS]: AI/ 的道具交互代表件实现；交互输入在玩家 Character（E 键边沿），开箱账本在 WaveDirector，本类只管"门面与状态"——三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "TreasureChest.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Audio/SFXLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 箱体宝蓝 / 盖条亮蓝白 / 已开灰（BasicShapeMaterial 真参数名 "Color"）——
	// v4 改色（用户裁决 2026-09-13：不占柠檬蛋糕的黄色，宝箱换蓝系）；与经验晶体的浅青蓝靠色深+尺寸双重区分
	const FLinearColor kChestBlue(0.25f, 0.45f, 1.0f);
	const FLinearColor kLidCyanWhite(0.6f, 0.85f, 1.0f);
	const FLinearColor kOpenedGray(0.28f, 0.28f, 0.3f);
}

ATreasureChest::ATreasureChest()
{
	PrimaryActorTick.bCanEverTick = true;

	// 本体：90×60×50cm 矮箱（/Engine 基础立方体 100³ × Scale），金色
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetRelativeScale3D(FVector(0.9f, 0.6f, 0.5f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeMeshFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMeshFinder.Object);
	}
	if (BasicMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, BasicMatFinder.Object);
	}
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // 纯距离判定（拾取物同族哲学）

	// 盖条：顶部横向压条（96×64×8cm，z=+28）——"这是个箱子"的剪影语言
	LidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LidMesh"));
	LidMesh->SetupAttachment(RootComponent);
	LidMesh->SetRelativeScale3D(FVector(0.96f, 0.66f, 0.1f));
	LidMesh->SetRelativeLocation(FVector(0.f, 0.f, 28.f));
	if (CubeMeshFinder.Succeeded())
	{
		LidMesh->SetStaticMesh(CubeMeshFinder.Object);
	}
	if (BasicMatFinder.Succeeded())
	{
		LidMesh->SetMaterial(0, BasicMatFinder.Object);
	}
	LidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 免费箱独立表现组件，BasicShapeMaterial/MID归本Actor；原Fab箱材质不覆写。
	StarterGlowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StarterGlowMesh"));
	StarterGlowMesh->SetupAttachment(RootComponent);
	StarterGlowMesh->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	StarterGlowMesh->SetRelativeScale3D(FVector(0.16f));
	StarterGlowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded()) { StarterGlowMesh->SetStaticMesh(SphereFinder.Object); }
	if (BasicMatFinder.Succeeded()) { StarterGlowMesh->SetMaterial(0, BasicMatFinder.Object); }
	StarterGlowMesh->SetVisibility(false);
	StarterLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StarterLight"));
	StarterLight->SetupAttachment(RootComponent);
	StarterLight->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	StarterLight->SetLightColor(FLinearColor(1.f, 0.78f, 0.15f));
	StarterLight->SetIntensity(1000.f);
	StarterLight->SetAttenuationRadius(550.f);
	StarterLight->SetCastShadows(false);
	StarterLight->SetVisibility(false);
}

void ATreasureChest::ConfigureStarterChest()
{
	if (bOpened) { return; }
	bStarterChest = true;
	StarterGlowMesh->SetVisibility(true);
	StarterLight->SetVisibility(true);
	StarterGlowMaterial = StarterGlowMesh->CreateAndSetMaterialInstanceDynamic(0);
	if (StarterGlowMaterial) { StarterGlowMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.78f, 0.15f)); }
}

FString ATreasureChest::DebugChestState() const
{
#if WITH_EDITOR
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bNearby = Hero && FVector::DistSquared(Hero->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractRadius);
	return FString::Printf(TEXT("{\"starter\":%s,\"opened\":%s,\"price\":%d,\"gold\":%d,\"paidChestCount\":%d,\"glow\":%s,\"nearby\":%s,\"interactRadius\":%.3f}"),
		bStarterChest ? TEXT("true") : TEXT("false"), bOpened ? TEXT("true") : TEXT("false"),
		bStarterChest ? 0 : (Director ? Director->GetNextChestCost() : OpenCost), Director ? Director->GetGold() : -1,
		Director ? Director->ChestOpenCount : -1, StarterGlowMesh && StarterGlowMesh->IsVisible() ? TEXT("true") : TEXT("false"),
		bNearby ? TEXT("true") : TEXT("false"), InteractRadius);
#else
	return TEXT("{}");
#endif
}

bool ATreasureChest::DebugTryOpen()
{
#if WITH_EDITOR
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Hero || FVector::DistSquared(Hero->GetActorLocation(), GetActorLocation()) > FMath::Square(InteractRadius)) { return false; }
	return TryOpen();
#else
	return false;
#endif
}

bool ATreasureChest::ApplyMeshOverride()
{
	if (!MeshOverride.ToSoftObjectPath().IsValid())
	{
		return false;
	}
	if (UStaticMesh* Mesh = MeshOverride.LoadSynchronous())
	{
		BodyMesh->SetStaticMesh(Mesh);
		BodyMesh->SetRelativeScale3D(FVector(MeshOverrideScale));
		for (int32 Slot = 0; Slot < BodyMesh->GetNumMaterials(); ++Slot)
		{
			BodyMesh->SetMaterial(Slot, nullptr);
		}
		// 盖条隐藏：Fab 宝箱自带完整剪影，压条同屏即穿帮（开箱"盖抬起"表现随换装退役）
		LidMesh->SetVisibility(false);
		return true;
	}
	return false;
}

void ATreasureChest::BeginPlay()
{
	Super::BeginPlay();

	// 换装用 Fab 原生材质（跳过箱体染色），但**探地落位永远执行**——
	// 09-20 浮箱案：换装提前 return 跳过了下方探地段，箱子悬在 GameMode 固定 spawn 高度
	const bool bSkinned = ApplyMeshOverride();

	if (!bSkinned)
	{
		// 运行时 MID 克隆再染色（参数真名 "Color"）
		if (const UStaticMesh* Mesh = BodyMesh->GetStaticMesh())
		{
			if (Mesh->GetMaterial(0))
			{
				if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
				{
					Mid->SetVectorParameterValue(TEXT("Color"), kChestBlue);
				}
			}
		}
		if (const UStaticMesh* LidMeshAsset = LidMesh->GetStaticMesh())
		{
			if (LidMeshAsset->GetMaterial(0))
			{
				if (UMaterialInstanceDynamic* Mid = LidMesh->CreateAndSetMaterialInstanceDynamic(0))
				{
					Mid->SetVectorParameterValue(TEXT("Color"), kLidCyanWhite);
				}
			}
		}
	}

	// 起伏地形适配（Landscape 落地，2026-09-13；v15 加深）：固定 spawn 高度在地形起伏下会埋/悬——
	// 向下探地落位（ObjectType 只认 WorldStatic，Landscape 是导航与探地的一等公民）。
	// v15：地形域扩到 ±21m（504m 图），探程 12m 不够——上探 15m / 下探 50m 全域覆盖
	FHitResult FloorHit;
	FCollisionObjectQueryParams FloorParams;
	FloorParams.AddObjectTypesToQuery(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorParams))
	{
		// 箱体半高落位：换装后按 Fab 箱实际高度（72cm 高 × 1.0 缩放 → 半高 36）
		const float HalfHeight = bSkinned ? 36.f : 25.f;
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y,
			FloorHit.ImpactPoint.Z + HalfHeight));
	}
}

void ATreasureChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bOpened)
	{
		return;
	}
	if (bStarterChest)
	{
		StarterGlowPhase += DeltaSeconds * 3.f;
		const float Pulse = 0.75f + 0.25f * FMath::Sin(StarterGlowPhase);
		StarterLight->SetIntensity(1000.f * Pulse);
		if (StarterGlowMaterial) { StarterGlowMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(Pulse, Pulse * 0.78f, 0.15f)); }
	}

	// 接近提示：头顶浮"按 E 开箱（N 金）"——交互邀请的可视语言
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	if (FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= InteractRadius)
	{
		// 09-25 动态价（用户定调"开得越多越贵"对冲金币常规化）：显示与扣费同源——导演的下一口价格
		const AAICompanionDemoGameMode* GameModeHint = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
		const AWaveDirector* DirectorHint = GameModeHint ? GameModeHint->GetWaveDirector() : nullptr;
		const int32 Price = bStarterChest ? 0 : (DirectorHint ? DirectorHint->GetNextChestCost() : OpenCost);
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 60.f),
			bStarterChest ? TEXT("按 E 领取初始补给（免费 · 仅一次）") : FString::Printf(TEXT("按 E 开箱（%d 金）"), Price),
			this, FColor::Yellow, 0.f, /*bDrawShadow*/ false, /*FontScale*/ 1.1f);
	}
}

bool ATreasureChest::TryOpen()
{
	if (bOpened)
	{
		return false;
	}

	// 组合根上报：扣费与三选一暂停都在 WaveDirector（账本唯一真源）
	const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
	AWaveDirector* Director = GameMode ? GameMode->GetWaveDirector() : nullptr;
	if (!Director)
	{
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	// 09-25 动态价：全局递增（首箱 3 金，每开一口 +2）——金币常规化后的经济对冲（MegaBonk 参考曲线，社区共识"越开越贵"）
	const int32 Price = bStarterChest ? 0 : Director->GetNextChestCost();
	const bool bGranted = bStarterChest ? Director->TryGrantStarterUpgrade() : Director->TrySpendGoldForUpgrade(Price);
	if (!bGranted)
	{
		if (bStarterChest) { return false; }
		// 钱不够：头顶飘差额——"攒钱再来"的经济张力
		if (PlayerPawn)
		{
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 160.f),
				FString::Printf(TEXT("金币不足（还差 %d）"), Price - Director->GetGold()),
				PlayerPawn, FColor::Silver, 1.0f, false, 1.f);
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Chest] 开箱失败：金币不足（%d/%d）"), Director->GetGold(), Price);
		return false;
	}

	bOpened = true;
	StarterGlowMesh->SetVisibility(false);
	StarterLight->SetVisibility(false);
	PlayOpenFx();
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::ChestOpen);   // [SFX] 开箱成功（扣费后才响——钱不够不出声）
	// 09-25 用户裁决：开箱后宝箱直接消失——原地留一具"开过的箱子"让玩家迷惑能不能再开；
	// 延迟 0.9s 销毁让爆闪（0.5s）演完再走
	SetLifeSpan(0.9f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Chest] %s开箱！消耗 %d 金，进入三选一（0.9s 后消失）"), bStarterChest ? TEXT("初始免费箱") : TEXT("普通箱"), Price);
	return true;
}

void ATreasureChest::PlayOpenFx()
{
	// 状态语言：箱体染灰 + 盖条抬起（"用掉了"）+ 三帧金色爆闪
	if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Mid->SetVectorParameterValue(TEXT("Color"), kOpenedGray);
	}
	LidMesh->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	LidMesh->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));

	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 40.f), 45.f, 12, FColor::Yellow, false, 0.5f, 0, 2.f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 40.f), 70.f, 12, FColor::Yellow, false, 0.35f, 0, 1.f);
}
