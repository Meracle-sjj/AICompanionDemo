/**
 * [INPUT]: 依赖 TemporaryPickup.h 的组件契约、Engine 的 StaticMesh/MaterialInstanceDynamic（/Engine 基础几何 + "Color" 参数染色）、DrawDebugString（拾取飘字）、EngineUtils（TActorIterator 效果征集）、Kismet 的 GetPlayerPawn、本模块 XpPickup/CompanionAIController/CompanionCharacter/HealthComponent（三效果征集目标+台词出口）、本模块 Audio/SFXLibrary（Pickup_Rush 事件音效）
 * [OUTPUT]: 磁吸三段式与三效果分派；狂暴飘字读取Controller真实剩余/紫炎，语音保持原槽和起播同源记录；碰撞全关，45s无人拾取自毁。
 * [POS]: AI/ 的暂态刺激节拍实现；掉落方 EnemyGrunt 只管生成与随机定型，效果各自直达作用面——生成与效果职责分离（XpPickup 同款哲学）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "TemporaryPickup.h"

#include "AI/CompanionAIController.h"
#include "AI/CompanionCharacter.h"
#include "AI/XpPickup.h"
#include "AICompanionDemo.h"
#include "Audio/SFXLibrary.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Events/RunEventLog.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 三系配色（避开在场四色：经验青蓝/金币金/蛋糕黄白/宝箱宝蓝——形状+颜色双区分）
	const FLinearColor kMagnetStormColor(1.0f, 0.35f, 0.05f);   // 橙红方块：风暴补给箱
	const FLinearColor kFrenzyColor(0.70f, 0.30f, 1.0f);        // 紫色晶柱：狂怒（与狂怒弹道同色系）
	const FLinearColor kAegisColor(0.95f, 0.93f, 0.78f);        // 白金大球：圣盾

	// 待机表现参数：与 XpPickup 同族语言，幅度/速率略高（稀有物的存在感）；
	// 前缀 kRush——unity build 多 cpp 同 chunk，匿名 namespace 同名常量会 C2086（GoldPickup/HealthPickup 案）
	constexpr float kRushBobAmplitude = 12.f;
	constexpr float kRushBobFrequency = 3.f;
	constexpr float kRushSpinRate = 160.f;
}

ATemporaryPickup::ATemporaryPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// 根=圆柱占位：首帧 Tick 按 RushType 换形状染色（RushType 由掉落方 spawn 后赋值，构造期不可知）
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetRelativeScale3D(FVector(0.3f));

	// 三系形状 + 带参材质全部构造期 Finder 常驻（ApplyRushVisual 运行时只做选用与染色）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	MeshCube = CubeMeshFinder.Object;
	MeshCylinder = CylMeshFinder.Object;
	MeshSphere = SphereMeshFinder.Object;
	BasicMat = BasicMatFinder.Object;
	if (CylMeshFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylMeshFinder.Object);
	}
	// 显式挂 BasicShapeMaterial（含 "Color" 参数）——基础形状资产槽 0 自带无参默认材质，
	// 直接 CreateAndSetMID 染色=静默失败（09-12 晶体灰球案同根，拾取物家族统一教训）
	if (BasicMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, BasicMatFinder.Object);
	}

	// 碰撞全关：纯距离判定——任何碰撞响应都可能干扰队友 LOS/感知 trace（拾取物家族铁律）
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 光晕：颜色在 ApplyRushVisual 按 RushType 定（远距离可辨——用户 09-18"不明显"反馈）
	RushLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RushLight"));
	RushLight->SetupAttachment(RootComponent);
	RushLight->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	RushLight->SetIntensity(8.f);
	RushLight->SetAttenuationRadius(350.f);
	RushLight->SetCastShadows(false);   // 小光源不值得阴影成本

	// 45s 无人拾取自毁：战场不堆积（XpPickup 同款规则）
	SetLifeSpan(45.f);
}

void ATemporaryPickup::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;
}

void ATemporaryPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 外观首帧定型：RushType 在 spawn 之后才被掉落方赋值（时序见 h），只能在此一次性应用
	if (!bVisualApplied)
	{
		ApplyRushVisual();
		bVisualApplied = true;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();

	// 磁吸三段式之一：接触即触发效果并销毁
	if (ToPlayer.Size2D() <= CollectRadius)
	{
		Collect();
		return;
	}

	// 之二：圈内渐加速飞向玩家（速度上限 1200 与 XpPickup 对齐——同族手感）
	if (ToPlayer.Size2D() <= MagnetRadius)
	{
		MagnetSpeed = FMath::Min(MagnetSpeed + 900.f * DeltaSeconds, 1200.f);
		SetActorLocation(GetActorLocation() + ToPlayer.GetSafeNormal() * MagnetSpeed * DeltaSeconds);
		return;
	}

	// 之三：圈外待机——浮动自转（叠加在 RestLocation 上，不逐帧漂移）
	MagnetSpeed = 0.f;
	BobPhase += DeltaSeconds * kRushBobFrequency;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * kRushBobAmplitude));
	BodyMesh->AddWorldRotation(FRotator(0.f, kRushSpinRate * DeltaSeconds, 0.f));
}

void ATemporaryPickup::ApplyRushVisual()
{
	// Fab 换装优先（09-25）：按系查 Config——素材到位即原生材质说话，几何拼装退役；
	// 换装路径信任素材贴图（蛋糕案：缺贴图素材才需兜底染色，选素材时须带 Textures）
	const TSoftObjectPtr<UStaticMesh>* OverridePtr = nullptr;
	float OverrideScale = 1.f;
	switch (RushType)
	{
	case ERushType::MagnetStorm: OverridePtr = &MagnetMeshOverride; OverrideScale = MagnetMeshScale; break;
	case ERushType::Frenzy:      OverridePtr = &FrenzyMeshOverride; OverrideScale = FrenzyMeshScale;  break;
	case ERushType::Aegis:       OverridePtr = &AegisMeshOverride;  OverrideScale = AegisMeshScale;   break;
	}
	if (OverridePtr && OverridePtr->ToSoftObjectPath().IsValid())
	{
		if (UStaticMesh* Skinned = OverridePtr->LoadSynchronous())
		{
			BodyMesh->SetStaticMesh(Skinned);
			BodyMesh->SetRelativeScale3D(FVector(OverrideScale));
			for (int32 Slot = 0; Slot < BodyMesh->GetNumMaterials(); ++Slot)
			{
				BodyMesh->SetMaterial(Slot, nullptr);   // 留 mesh 原生材质（带贴图 Fab 素材）
			}
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Rush][Skin] 换装 %s（%d 槽 ×%.4f）"),
				*Skinned->GetName(), BodyMesh->GetNumMaterials(), OverrideScale);
			return;
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Rush][Skin] 换装加载失败：%s——回退几何拼装"),
			*OverridePtr->ToSoftObjectPath().ToString());
	}

	UStaticMesh* Mesh = MeshCylinder;
	FLinearColor Color = kFrenzyColor;
	switch (RushType)
	{
	case ERushType::MagnetStorm:
		Mesh = MeshCube;      // 橙红方块：补给箱血统
		Color = kMagnetStormColor;
		BodyMesh->SetRelativeScale3D(FVector(0.3f));    // 30cm 方块（09-18 用户反馈"偏小不明显"翻倍）
		break;
	case ERushType::Frenzy:
		Mesh = MeshCylinder;  // 紫色晶柱
		Color = kFrenzyColor;
		BodyMesh->SetRelativeScale3D(FVector(0.3f));
		break;
	case ERushType::Aegis:
	default:
		Mesh = MeshSphere;    // 白金大球：圣盾（保命物的仪式感）
		Color = kAegisColor;
		BodyMesh->SetRelativeScale3D(FVector(0.6f));    // 30cm 球
		break;
	}
	// 光晕同色：远距离可辨（用户 09-18 反馈"不是很明显"的正面响应）
	if (RushLight)
	{
		RushLight->SetLightColor(Color);
	}

	if (Mesh)
	{
		BodyMesh->SetStaticMesh(Mesh);
	}
	// 换 mesh 会重置材质槽为 mesh 自带无参材质——必须重挂 BasicShapeMaterial 再 MID 染色（晶体案教训）
	if (BasicMat)
	{
		BodyMesh->SetMaterial(0, BasicMat);
	}
	if (const UStaticMesh* CurrentMesh = BodyMesh->GetStaticMesh())
	{
		if (CurrentMesh->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	}
}

void ATemporaryPickup::Collect()
{
	// 拾取语音依次入世界队列，起播时才显示该录音对应的字幕。

	switch (RushType)
	{
	case ERushType::MagnetStorm:
		ApplyMagnetStorm();
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Magnet);   // [SFX] 磁吸风暴独立音（09-27 用户点名）
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_Magnet);    // [Voice] 药剂台词（09-29 TTS 同源）
		break;
	case ERushType::Frenzy:
		ApplyFrenzy();
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Rush);     // [SFX] 狂怒/圣盾共用槽
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_Frenzy);    // [Voice] 药剂台词
		break;
	case ERushType::Aegis:
	default:
		ApplyAegis();
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Rush);
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_Aegis);     // [Voice] 药剂台词
		break;
	}

	Destroy();
}

void ATemporaryPickup::ApplyMagnetStorm()
{
	// 全图征集：拾取物互不相识，广播式触发（磁吸风暴语义 = 战场清空的"唰"）
	int32 Count = 0;
	for (TActorIterator<AXpPickup> It(GetWorld()); It; ++It)
	{
		It->ForceMagnetToPlayer(2.f);
		++Count;
	}

	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Rush] 磁吸风暴：全图 %d 枚经验晶体强制飞向玩家"), Count);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 190.f), TEXT("磁吸风暴！"),
			PlayerPawn, FColor::Orange, 1.5f, /*bDrawShadow*/ false, /*FontScale*/ 1.4f);
	}
}

void ATemporaryPickup::ApplyFrenzy()
{
	float Remaining = 0.f;
	bool bPurple = false;
	for (TActorIterator<ACompanionAIController> It(GetWorld()); It; ++It)
	{
		It->ActivateFrenzy(8.f);
		Remaining = FMath::Max(Remaining, It->GetFrenzyRemaining());
		bPurple |= It->IsPurpleFrenzyActive();
	}

	if (Remaining <= 0.f) return;
	const FString Label = bPurple ? FString::Printf(TEXT("紫炎狂暴 %.0fs！"), Remaining)
		: FString::Printf(TEXT("狂怒攻速 %.0fs！"), Remaining);
	UE_LOG(LogAICompanionDemo, Log, TEXT("[Rush] %s 开火间隔×0.5 damageMultiplier=%.1f"), *Label, bPurple ? 2.f : 1.f);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 190.f), Label,
			PlayerPawn, FColor::Purple, 1.5f, false, 1.4f);
	}
}

void ATemporaryPickup::ApplyAegis()
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		if (UHealthComponent* Health = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			Health->SetInvulnerable(5.f);
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 190.f), TEXT("圣盾无敌 5s！"),
				PlayerPawn, FColor::White, 1.5f, false, 1.4f);
		}
		else
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Rush] 圣盾落空：玩家无 HealthComponent"));
		}
	}
}
