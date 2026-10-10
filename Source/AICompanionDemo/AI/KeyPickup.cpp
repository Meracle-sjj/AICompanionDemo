/**
 * [INPUT]: 依赖 KeyPickup.h 的组件契约、Engine 的 StaticMesh/PointLight/MaterialInstanceDynamic、DrawDebugHelpers（拾取闪光与教学飘字）、Kismet 的 GetPlayerPawn、本模块 AICompanionDemoCharacter 的 GiveTreasureKey
 * [OUTPUT]: 对外提供 AKeyPickup 的实现——待机浮动自转（拾取物家族语言）→ 玩家入圈 Collect：持钥状态 + "寻找带锁的宝箱"教学 + 金色闪光 + 自毁；**钥匙放大（09-29 用户裁决"删黑柱扩样式"：信标柱染色失效渲染黑柱=Cylinder 槽 0 无参 DefaultMaterial 定罪案的复发，柱体删除；几何球 0.25→0.5、换装缩放默认 3→5、点光 14/1400 保留）**
 * [POS]: AI/ 拾取物家族实现第五系：寻宝玩法的钩子端——与 ALockedChest 构成"钥匙-锁箱"两点一线的探索回路
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "KeyPickup.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Vfx/VfxLibrary.h"

namespace
{
	// 钥匙金（宝箱金同族——"金钥匙开金宝箱"的暗示语言）
	const FLinearColor kKeyGold(1.f, 0.85f, 0.3f);
}

AKeyPickup::AKeyPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(RootComponent);
	KeyLight->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	KeyLight->SetLightColor(kKeyGold);
	KeyLight->SetIntensity(1200.f);           // 09-30 二刀：40cd 在室外日照=萤火虫（用户实测无光斑）——上千坎德拉才穿日光
	KeyLight->SetAttenuationRadius(2500.f);   // 1400→2500（25m 光域）
	KeyLight->SetCastShadows(false);

	// 环绕卫星×3（09-30 特效升级：程序化 Mesh 粒子——真实网格有光影有遮挡，替代 Debug 线条的手搓感）
	for (int32 i = 0; i < 3; ++i)
	{
		Orbiters[i] = CreateDefaultSubobject<USphereComponent>(*FString::Printf(TEXT("Orbiter%d"), i));
		Orbiters[i]->SetupAttachment(RootComponent);
		Orbiters[i]->InitSphereRadius(16.f);
		Orbiters[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMatFinder(TEXT("/Game/Materials/M_KeyGlow.M_KeyGlow"));
	BasicMat = BasicMatFinder.Succeeded() ? BasicMatFinder.Object : nullptr;
	if (SphereFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(2.0f));   // 09-30 终局：1.0→2.0（200cm 金球，与信标方碑同级体量——对照实验：信标看得到钥匙看不到，差异就是体量）
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (int32 i = 0; i < 3; ++i)
	{
		if (BasicMat)
		{
			Orbiters[i]->SetMaterial(0, BasicMat);   // 哑光金球——KeyLight 1200cd 照亮即"发光"（物理光照路线）
		}
	}
	// 材质优先级（09-30 发光案）：自发光 M_KeyGlow（Emissive 不受日光影响+Bloom 泛光晕）
	// → BasicShapeMaterial 哑光兜底（零资产可用：发光材质资产缺失时仍可玩）
	if (GlowMatFinder.Succeeded())
	{
		BodyMesh->SetMaterial(0, GlowMatFinder.Object);
	}
	else if (BasicMat)
	{
		BodyMesh->SetMaterial(0, BasicMat);
	}
}

void AKeyPickup::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;

	// 换装链退役（09-30 用户实测"突兀的小钥匙"定罪：ini MeshOverride 指向锈钥匙时 BeginPlay 覆写
	// 金球为 39cm 小模型且 SetMaterial(nullptr) 清掉 Glow——终局金球方案被换装链打架）。金球+泛光为正装，
	// ini 的 MeshOverride 行保留但已置空——未来换装重启此链时需同步重审视觉包
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
	}
	else if (BasicMat)
	{
		if (UMaterialInstanceDynamic* Mid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), kKeyGold);
		}
	}

	// 发光材质参数化补染（09-30）：M_KeyGlow 的 "KeyColor" 参数覆写为钥匙金——
	// Emissive 路径下球自发光，配合引擎 Bloom=真光晕；CreateAndSetMID 仅在已挂 Glow 材质时生效
	if (BodyMesh->GetMaterial(0) && BodyMesh->GetMaterial(0)->GetName().StartsWith(TEXT("M_KeyGlow")))
	{
		if (UMaterialInstanceDynamic* GlowMid = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			GlowMid->SetVectorParameterValue(TEXT("KeyColor"), kKeyGold);
		}
	}

	// 探地落位（信标同款）
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(KeyDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y,
			FloorHit.ImpactPoint.Z + 110.f));   // 09-30：固定抬升 110cm（球半径 100+浮动余量）——scale 乘式在大球下会埋地
	}
	RestLocation = GetActorLocation();   // 浮动叠加基准在探地之后落定（信标浮空案教训）

	// 呼吸脉动基准（换装覆写后的当前缩放——Tick 乘算不覆盖换装结果）
	BaseScale = BodyMesh->GetRelativeScale3D().X;

	// 09-30 用户实测"钥匙旁带紫色火不对"——Magic 样条特效退役（紫色调与钥匙语义不符）；
	// 钥匙视觉回归干净组合：锈钥匙+卫星环+呼吸+光柱+1200cd 点光
}

void AKeyPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	BobPhase += DeltaSeconds * 2.f;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * 7.f));

	// 展示自转（拾取物家族惯例——锈钥匙模型绕轴旋转，剪影可读性最大化）
	BodyMesh->AddRelativeRotation(FRotator(0.f, DeltaSeconds * 70.f, 0.f));

	// 呼吸脉动（09-30 特效升级：±8% 正弦缩放——"活物感"的最低成本来源）
	BodyMesh->SetRelativeScale3D(FVector(FMath::Lerp(BaseScale * 0.92f, BaseScale * 1.08f,
		(FMath::Sin(BobPhase * 1.5f) + 1.f) * 0.5f)));

	// 环绕卫星公转（120° 均分三球，水平环绕半径 160cm + 正弦微升降——"法球环绕"的仪式感）
	OrbitalPhase += DeltaSeconds * 1.6f;
	for (int32 i = 0; i < 3; ++i)
	{
		const float Angle = OrbitalPhase + i * (2.f * PI / 3.f);
		Orbiters[i]->SetRelativeLocation(FVector(
			FMath::Cos(Angle) * 160.f,
			FMath::Sin(Angle) * 160.f,
			20.f + FMath::Sin(OrbitalPhase * 2.f + i) * 25.f));
	}

	// 常驻金色光柱（09-30 用户实测"突兀的小钥匙很难看到"）：DrawDebugLine 每帧重画短寿命=零资产常驻
	// 光柱（信标谢幕线同款范式），25m 外可见；KeyLight 已加到 1200cd 穿日光
	DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + FVector(0.f, 0.f, 2500.f),
		FColor::FromHex(TEXT("FFD700")), false, 0.1f, 0, 10.f);

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

void AKeyPickup::Collect()
{
	AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(
		UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (PlayerChar)
	{
		PlayerChar->GiveTreasureKey();
		DrawDebugString(GetWorld(), PlayerChar->GetActorLocation() + FVector(0.f, 0.f, 200.f),
			TEXT("拾取宝箱钥匙！寻找带锁的大箱"), PlayerChar, FColor::Yellow, 2.5f, false, 1.2f);
	}

	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 50.f), 60.f, 12, FColor::Yellow, false, 0.5f, 0, 2.f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Key] 钥匙拾取——锁箱已解锁资格"));
	if (MagicVfx)
	{
		MagicVfx->Destroy();   // 光效随钥匙同生共死
	}
	Destroy();
}
