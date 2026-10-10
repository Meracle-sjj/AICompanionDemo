/**
 * [INPUT]: 依赖 EnemyTarget.h 的组件契约、HealthComponent 的 OnHealthChanged/OnDeath、AIModule 的 AISense_Sight（视觉源注册）、Kismet 的 GetPlayerPawn（猎手行为的目标查询）
 * [OUTPUT]: 对外提供 AEnemyTarget 的实现——装配球体/血量/感知源三件套 + HuntPlayer 逼近与攻击节拍（ApplyDamage 携带 Instigator=自己，受击广播即队友的伤害通道信号）+ SnapToGround 持续贴地（水平直移免 sweep，Z 每帧跟地；按 ObjectType 只认 WorldStatic 地面——通道查询把同伴/角色当"地面"抬球瞬移的"球消失"根因修复，球体 ObjectType 相应摘成 WorldDynamic），颜色随血量渐变
 * [POS]: AI/ 的敌对方实现；被 GameMode 代码生成（D6 范式），M3 起主动威胁玩家——队友评分的"威胁分"由此产生
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "EnemyTarget.h"

#include "AICompanionDemo.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	// 满血（敌对红）与濒死（暗灰）的两端色：血量百分比线性插值——观感即信息，无需 UI 血条
	const FLinearColor kColorHealthy(0.78f, 0.18f, 0.12f);
	const FLinearColor kColorWounded(0.35f, 0.32f, 0.30f);
	const FLinearColor kColorDead(0.12f, 0.12f, 0.12f);

	// 贴地几何：球体半径（基础 Sphere 50 × Scale 2）与悬浮间隙——间隙给地形起伏留容差，视觉不可辨
	constexpr float kBodyRadius = 100.f;
	constexpr float kGroundHover = 2.f;
}

void AEnemyTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HuntPlayer(DeltaSeconds);
}

void AEnemyTarget::HuntPlayer(float DeltaSeconds)
{
	// 自身已死或玩家已倒下：猎手停手（死亡是绝对状态，不追尸不挨打）
	if (IsDead())
	{
		return;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	// 玩家血量组件经 FindComponentByClass 解耦获取：敌人不依赖玩家具体类（跨阵营只认 HealthComponent 契约）
	UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	if (!PlayerHealth || PlayerHealth->IsDead())
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();
	const float DistCm = ToPlayer.Size2D();
	if (DistCm > PlayerEngageRange)
	{
		// 水平逼近 + 每帧贴地校准（不寻路：开阔灰盒够用，KISS）。
		// sweep 关闭：贴地球与地面零相交，水平 sweep 只会撞上坡面并被整帧拒动——永久卡死的根因
		// （2026-09-06"球不动"：+2cm 静态余量治不了动态坡面）；灰盒开阔无墙，穿墙风险可忽略，
		// 未来加入迷宫式布局再回头补水平滑行（YAGNI）
		FVector NewLocation = GetActorLocation()
			+ FVector(ToPlayer.X, ToPlayer.Y, 0.f).GetSafeNormal() * (MoveSpeed * DeltaSeconds);
		SnapToGround(NewLocation);
		SetActorLocation(NewLocation, /*bSweep*/ false);
		PlayerAttackCooldown = 0.f;
		return;
	}

	// 贴脸攻击节拍：冷却累计到间隔才出手（伤害仍走全项目唯一入口 HealthComponent；
	// Instigator=自己——玩家血量组件的受击广播会把它递到队友的威胁候选集）
	PlayerAttackCooldown += DeltaSeconds;
	if (PlayerAttackCooldown >= PlayerAttackInterval)
	{
		PlayerAttackCooldown = 0.f;
		PlayerHealth->ApplyDamage(AttackDamageToPlayer, /*Instigator*/ this);
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] %s 攻击玩家，伤害 %.0f，玩家余血 %.0f"),
			*GetNameSafe(this), AttackDamageToPlayer, PlayerHealth->GetCurrentHealth());
	}
}

bool AEnemyTarget::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}

bool AEnemyTarget::IsEngagingPlayer() const
{
	if (IsDead())
	{
		return false;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return false;
	}

	// 无状态即时判定（KISS）：贴脸范围内且玩家还活着即"咬人中"——与 HuntPlayer 的攻击分支同一几何条件
	const UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	return PlayerHealth && !PlayerHealth->IsDead()
		&& FVector(GetActorLocation() - PlayerPawn->GetActorLocation()).Size2D() <= PlayerEngageRange;
}

AEnemyTarget::AEnemyTarget()
{
	// Tick 驱动猎手行为（逼近 + 攻击节拍）——数量级 ≤10 只，每帧直移开销可忽略
	PrimaryActorTick.bCanEverTick = true;

	// 根=球体网格（/Engine 基础形状，半径 50cm，放大到 1m 直径）：碰撞与观感一体，任务书"简单碰撞、颜色表现"的对应物
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetRelativeScale3D(FVector(2.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereMeshFinder.Object);
	}

	// ObjectType 摘成 WorldDynamic：贴地 trace（SnapToGround）按 ObjectType 只认 WorldStatic 地面——
	// 默认 BlockAll 预设的 ObjectType 是 WorldStatic，敌人会互相当"地面"把彼此抬上头顶瞬移
	// （2026-09-06 用户实测"球突然消失"根因）；Visibility 响应矩阵不变，队友感知照常看得见
	BodyMesh->SetCollisionObjectType(ECC_WorldDynamic);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));

	// 视觉刺激源：让队友的 AIPerception（Sight）能"看见"——感知注册在敌人体内完成，队友侧零配置
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
}

void AEnemyTarget::SnapToGround(FVector& InOutLocation) const
{
	// 向下按 ObjectType 找地面（只认 WorldStatic=地形/静态几何）：起点抬高 200 容上坡，射程 500 容高台落差——
	// 直移体无重力，Z 必须自管。按通道查会命中路径上的其他敌人/角色的胶囊，球被"抬"到它们头顶瞬移跳变
	// （2026-09-06"球突然消失"根因）——WorldStatic 过滤 + 球体 ObjectType=WorldDynamic 双保险根治
	FHitResult GroundHit;
	const FVector TraceStart = InOutLocation + FVector(0.f, 0.f, 200.f);
	if (GetWorld()->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceStart - FVector(0.f, 0.f, 500.f),
		FCollisionObjectQueryParams(ECC_WorldStatic)))
	{
		InOutLocation.Z = GroundHit.ImpactPoint.Z + kBodyRadius + kGroundHover;
	}
	// 未命中（悬于平台边缘外）：保持 Z 水平飞——无重力直移体的自洽行为，无需告警
}

void AEnemyTarget::BeginPlay()
{
	Super::BeginPlay();

	// 落地放置（2026-09-05 用户实测"敌人悬浮半空"）：直移体无重力，spawn 抬高的 Z 余量不会自然落定——
	// 首帧先贴地（此后 HuntPlayer 每帧持续跟地），观感与"敌人贴地追击"的真实感对齐
	FVector SpawnLocation = GetActorLocation();
	SnapToGround(SpawnLocation);
	SetActorLocation(SpawnLocation);

	// MID 在运行时从当前材质克隆：编辑器里换网格也不破链路
	if (UStaticMesh* Mesh = BodyMesh->GetStaticMesh())
	{
		if (Mesh->GetMaterial(0))
		{
			BodyMaterial = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
		}
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue("BaseColor", kColorHealthy);
	}

	HealthComponent->OnHealthChanged.AddDynamic(this, &AEnemyTarget::HandleHealthChanged);
	HealthComponent->OnDeath.AddDynamic(this, &AEnemyTarget::HandleDeath);

	// 诊断日志：入场即报号（感知不到敌人时先查此行是否存在）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] Target spawned: %s at %s"), *GetNameSafe(this), *GetActorLocation().ToCompactString());
}

void AEnemyTarget::HandleHealthChanged(UHealthComponent* HealthComp, float NewHealth, float Delta)
{
	if (!BodyMaterial || !HealthComp)
	{
		return;
	}

	// 血量百分比插值：满血红 → 濒死灰（观感即信息，替代 UI 血条——与项目"调试表现"叙事一致）
	const float HealthPct = FMath::Clamp(NewHealth / FMath::Max(HealthComp->GetMaxHealth(), 1.f), 0.f, 1.f);
	BodyMaterial->SetVectorParameterValue("BaseColor", FLinearColor::LerpUsingHSV(kColorWounded, kColorHealthy, HealthPct));
}

void AEnemyTarget::HandleDeath(UHealthComponent* HealthComp)
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue("BaseColor", kColorDead);
	}

	// 停留 0.8s 供视觉确认后自毁（退出目标集合）；此间 bDead 已挡住重复伤害（HealthComponent 契约）
	SetLifeSpan(0.8f);
}
