/**
 * [INPUT]: GoldPickup.h、StaticMesh材质染色、GameMode/WaveDirector金币账本、Audio收集音、UI/CombatFeedbackSubsystem正式金币显示
 * [OUTPUT]: 磁吸三段式、真实金币入账，仅钱包实际正增量显示头顶金币矢量图标与大数字，收集自毁/45s无人拾取自毁；碰撞全关
 * [POS]: AI/ 的宝箱经济货币层实现；掉落方 EnemyGrunt 只管生成与喂值，收集上报的组合根在 GameMode——生成与入账职责分离（XpPickup 同款）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "GoldPickup.h"

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
#include "UI/CombatFeedbackSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 金币金色（BasicShapeMaterial 真参数名 "Color"——09-12 实证；勿与 Manny 的 "Paint Tint" 混淆）
	const FLinearColor kGoldColor(1.0f, 0.78f, 0.1f);

	constexpr float kGoldBobAmplitude = 9.f;
	constexpr float kGoldBobFrequency = 3.4f;
	constexpr float kGoldSpinRate = 160.f;
}

AGoldPickup::AGoldPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// 根=小球体（/Engine 基础球 50cm × Scale 0.35 ≈ 18cm 金币，比经验晶体略小以示层级）
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetRelativeScale3D(FVector(0.35f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (SphereMeshFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereMeshFinder.Object);
	}
	// 显式挂 BasicShapeMaterial（含 "Color" 参数）——资产槽 0 自带无参 DefaultMaterial，直接染色=静默失败（晶体灰球案同根）
	if (BasicMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, BasicMatFinder.Object);
	}

	// 碰撞全关：纯距离判定（同族哲学——任何碰撞响应都可能干扰队友 LOS/感知 trace）
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 45s 无人拾取自毁（钱比经验值钱，活得久一点）
	SetLifeSpan(45.f);
}

void AGoldPickup::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;

	// 换装即用 Fab 原生材质（蛋糕白案根治，同 XpPickup）
	if (ApplyMeshOverride())
	{
		return;
	}

	// 运行时 MID 克隆再染色（参数真名 "Color"）
	if (const UStaticMesh* Mesh = BodyMesh->GetStaticMesh())
	{
		if (Mesh->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), kGoldColor);
			}
		}
	}
}

void AGoldPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();
	const float DistCm = ToPlayer.Size2D();

	// 磁吸三段式之一：接触即收集
	if (DistCm <= CollectRadius)
	{
		Collect();
		return;
	}

	// 之二：圈内渐加速飞向玩家
	if (DistCm <= MagnetRadius)
	{
		MagnetSpeed = FMath::Min(MagnetSpeed + 900.f * DeltaSeconds, 1200.f);
		SetActorLocation(GetActorLocation() + ToPlayer.GetSafeNormal() * MagnetSpeed * DeltaSeconds);
		return;
	}

	// 之三：圈外待机——浮动自转
	MagnetSpeed = 0.f;
	BobPhase += DeltaSeconds * kGoldBobFrequency;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * kGoldBobAmplitude));
	BodyMesh->AddWorldRotation(FRotator(0.f, kGoldSpinRate * DeltaSeconds, 0.f));
}

bool AGoldPickup::ApplyMeshOverride()
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
		return true;
	}
	return false;
}

void AGoldPickup::Collect()
{
	// 经组合根上报：GameMode 是唯一知道"导演是谁"的地方（职责分离与 XpPickup 同款）
	int32 ActualGoldGained = 0;
	if (const AAICompanionDemoGameMode* GameMode = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		if (AWaveDirector* Director = GameMode->GetWaveDirector())
		{
			const int32 GoldBefore = Director->GetGold();
			Director->AddGold(GoldValue);
			ActualGoldGained = FMath::Max(Director->GetGold() - GoldBefore, 0);
		}
	}

	// 账本拒绝入账时不伪报收益；正式数字生命周期归世界反馈层。
	if (ActualGoldGained > 0)
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			UCombatFeedbackSubsystem::ShowGold(GetWorld(), PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 170.f), ActualGoldGained);
		}
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Gold] 拾取金币实际入账 +%d"), ActualGoldGained);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Gold);   // [SFX] 金币收集

	Destroy();
}
