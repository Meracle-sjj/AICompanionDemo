/**
 * [INPUT]: 依赖 XpPickup.h 的组件契约、Engine 的 StaticMesh/MaterialInstanceDynamic（/Engine 基础球体与 "Color" 参数染色——EnemyTarget 血量染色同款管线）、DrawDebugString（拾取 +N 飘字，Development 包保留）、Kismet 的 GetPlayerPawn/GetGameMode、本模块 TemporaryPickup（磁吸风暴征集调用 ForceMagnetToPlayer）、本模块 Audio/SFXLibrary（Pickup_Xp 事件音效）
 * [OUTPUT]: 对外提供 AXpPickup 的实现——磁吸三段式（圈外浮动自转待机 → 圈内渐加速飞向玩家 → 接触收集：GameMode→WaveDirector 入账 + 玩家头顶 +N 飘字 + 自毁）；碰撞全关（纯距离判定，不污染队友 LOS/感知 trace——球体时代教训）；30s 无人拾取自毁（防堆积，VS 同款规则）；E5 增补强制磁吸态（磁吸风暴期间无视半径恒速直飞）
 * [POS]: AI/ 的成长循环入口实现；掉落方 EnemyGrunt 只管生成与喂值，收集上报的组合根在 GameMode——生成与入账职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "XpPickup.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Audio/SFXLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 晶体青蓝色（/Engine BasicShapeMaterial 的真参数名 "BaseColor"——球体血量染色实证过的管线，勿与 Manny 的 "Paint Tint" 混淆）
	const FLinearColor kXpColor(0.15f, 0.85f, 1.0f);

	// 待机表现参数：浮动幅度/频率与自转速率——"这是可拾取物"的通用语言
	constexpr float kBobAmplitude = 10.f;
	constexpr float kBobFrequency = 3.f;
	constexpr float kSpinRate = 120.f;
}

AXpPickup::AXpPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// 根=小球体（/Engine 基础球 50cm × Scale 0.5 = 25cm 晶体）：退役球体血统的零资产复用
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetRelativeScale3D(FVector(0.5f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (SphereMeshFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereMeshFinder.Object);
	}
	// 显式挂 BasicShapeMaterial（含 "Color" 参数）——球体资产槽 0 自带的是无参 DefaultMaterial，
	// 直接 CreateAndSetMID 染色=静默失败（蛋糕案同根，09-12 晶体灰球案定罪）
	if (BasicMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, BasicMatFinder.Object);
	}

	// 碰撞全关：纯距离判定——任何碰撞响应都可能干扰队友 LOS/感知 trace（"球突然消失"案的教训外推）
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 30s 无人拾取自毁：防晶体堆积拖性能/污染视野（VS 同款规则）
	SetLifeSpan(30.f);
}

void AXpPickup::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;

	// 换装即用 Fab 原生材质（2026-09-18 蛋糕白案根治：构造期钉死的 BasicShapeMaterial override
	// 会盖住换入模型的自带材质——ClearOverrideMaterials 归还材质槽，跳过染色，真模型自己说话）
	if (ApplyMeshOverride())
	{
		return;
	}

	// 未换装：/Engine 几何 + 染色（运行时 MID 克隆再染色，参数真名 "Color"——09-12 实证）
	if (const UStaticMesh* Mesh = BodyMesh->GetStaticMesh())
	{
		if (Mesh->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), kXpColor);
			}
		}
	}
}

void AXpPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();
	const float DistCm = ToPlayer.Size2D();

	// 磁吸三段式之一：接触即收集（磁吸速度保留残值——连续拾取越捡越顺手的隐性手感）
	if (DistCm <= CollectRadius)
	{
		Collect();
		return;
	}

	// E5 磁吸风暴：强制磁吸态无视半径恒速直飞（25000cm/s ≈ 2s 横跨 ±250m 全图——提案"2s 内全部飞向玩家"；
	// 3D 直飞允许穿模，风暴要的是清场爽感不是物理真实）
	if (bForceMagnet)
	{
		SetActorLocation(GetActorLocation() + ToPlayer.GetSafeNormal() * 25000.f * DeltaSeconds);
		return;
	}

	// 之二：圈内渐加速飞向玩家（速度上限 1200——比玩家跑速快，够得着就逃不掉）
	if (DistCm <= MagnetRadius)
	{
		MagnetSpeed = FMath::Min(MagnetSpeed + 900.f * DeltaSeconds, 1200.f);
		SetActorLocation(GetActorLocation() + ToPlayer.GetSafeNormal() * MagnetSpeed * DeltaSeconds);
		return;
	}

	// 之三：圈外待机——浮动自转（叠加在 RestLocation 上，不逐帧漂移）
	MagnetSpeed = 0.f;
	BobPhase += DeltaSeconds * kBobFrequency;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * kBobAmplitude));
	BodyMesh->AddWorldRotation(FRotator(0.f, kSpinRate * DeltaSeconds, 0.f));
}

bool AXpPickup::ApplyMeshOverride()
{
	if (!MeshOverride.ToSoftObjectPath().IsValid())
	{
		return false;
	}
	if (UStaticMesh* Mesh = MeshOverride.LoadSynchronous())
	{
		BodyMesh->SetStaticMesh(Mesh);
		BodyMesh->SetRelativeScale3D(FVector(MeshOverrideScale));
		// 归还材质槽：构造期显式挂的 BasicShapeMaterial override 会盖住 Fab 自带材质（蛋糕白案根因）
		// SetMaterial(nullptr) = 恢复 mesh 资产自带材质
		for (int32 Slot = 0; Slot < BodyMesh->GetNumMaterials(); ++Slot)
		{
			BodyMesh->SetMaterial(Slot, nullptr);
		}
		return true;
	}
	return false;
}

void AXpPickup::ForceMagnetToPlayer(float Duration)
{
	bForceMagnet = true;
	// 到时自解（重复触发刷新定时器，ApplySlow 同范式——不叠加错乱）
	GetWorldTimerManager().SetTimer(ForceMagnetTimerHandle, this, &AXpPickup::EndForceMagnet, Duration, /*bLoop*/ false);
}

void AXpPickup::EndForceMagnet()
{
	bForceMagnet = false;
}

void AXpPickup::Collect()
{
	// 经组合根上报：GameMode 是唯一知道"导演是谁"的地方（掉落方与收集方互不认识，职责分离）
	if (const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		if (AWaveDirector* Director = GameMode->GetWaveDirector())
		{
			Director->AddXp(XpValue);
		}
	}

	// 拾取反馈：+N 飘在玩家头顶（DrawDebugString 跟随 pawn，Development 保留——与评分板同命 D5）
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 140.f), FString::Printf(TEXT("+%d"), XpValue),
			PlayerPawn, FColor::Cyan, 0.8f, /*bDrawShadow*/ false, /*FontScale*/ 1.f);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Xp] 拾取晶体 +%d"), XpValue);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Xp);   // [SFX] 经验收集

	Destroy();
}
