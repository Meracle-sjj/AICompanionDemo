/**
 * [INPUT]: 依赖 BossOrb.h 的组件契约、Engine 的 StaticMeshComponent（挂点）/PointLightComponent（火光）、NiagaraFunctionLibrary 的 SpawnSystemAttached（火焰弹体）、Vfx/VfxLibrary 的 Fire 命中特效、HealthComponent 的玩家伤害口、Kismet 的 GetPlayerPawn
 * [OUTPUT]: 可见35cm橙核/外围火焰随三维直线飞行；连续球扫玩家实际胶囊与BlockPawn实体，最近接触结算且地形同距优先；6s/宿主/玩家死亡销毁，反伤同栈退场不造火浪，友方保持独立原路径。
 * [POS]: AI/ Boss弹体；显式物理查询避免高处误伤、高速穿透与Visibility忽略Pawn，Niagara不承担伤害几何。
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "BossOrb.h"
#include "AI/BossBase.h"
#include "AICompanionDemo.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Vfx/VfxLibrary.h"

ABossOrb::ABossOrb()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;

	// 隐藏根挂载火焰，额外可见橙核对应敌方实际命中体积；友方保留旧火焰外观。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereFinder.Object);
	}
	BodyMesh->SetRelativeScale3D(FVector(0.8f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 伤害使用显式连续球扫。
	BodyMesh->SetVisibility(false);
	HitCore = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HitCore"));
	HitCore->SetupAttachment(BodyMesh);
	if (SphereFinder.Succeeded()) HitCore->SetStaticMesh(SphereFinder.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CoreMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CoreMaterial.Succeeded()) HitCore->SetMaterial(0, CoreMaterial.Object);
	HitCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HitCore->SetCastShadow(false);
	HitCore->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FireFinder(TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireMid_Loop_001.NS_Sub_FireMid_Loop_001"));
	if (FireFinder.Succeeded())
	{
		FireTemplate = FireFinder.Object;
	}

	// 火光跟随（KeyLight 同款哲学：日照沙丘上无光源=隐身）——橙红火光，火焰弹 60m 外可读。
	// 09-30 复测三批补亮（用户"阴燃感、识别度低"）：Small→Mid 火焰本体更饱满 + 光强 1500→3000cd
	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
	PointLight->SetupAttachment(BodyMesh);
	PointLight->SetLightColor(FLinearColor(1.f, 0.45f, 0.15f));
	PointLight->SetIntensity(3000.f);
	PointLight->SetAttenuationRadius(900.f);

	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
}

void ABossOrb::BeginPlay()
{
	Super::BeginPlay();

	// 火焰挂载（跟随弹体飞行，随 actor 销毁）——Mid 档火焰 1.4 倍体量（本体已大，微调即饱满）
	if (FireTemplate)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(FireTemplate, BodyMesh, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			/*bAutoDestroy*/ false, /*bAutoActivate*/ true, ENCPoolMethod::None);
		GetRootComponent()->SetWorldScale3D(FVector(1.4f));
	}
}

void ABossOrb::Launch(const FVector& Direction, float InDamage, float Speed)
{
	Velocity = Direction.GetSafeNormal() * FMath::Max(0.f, Speed);
	Damage = InDamage;
	// Engine Sphere直径100cm；根节点固定单位缩放，可见核心与查询半径同一真源。
	BodyMesh->SetWorldScale3D(FVector::OneVector);
	HitCore->SetRelativeScale3D(FVector(HitRadius / 50.f));
	HitCore->SetVisibility(true);
	if (UMaterialInstanceDynamic* CoreMid = HitCore->CreateAndSetMaterialInstanceDynamic(0))
		CoreMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, .22f, .025f));
	TArray<UNiagaraComponent*> Flames;
	GetComponents<UNiagaraComponent>(Flames);
	for (UNiagaraComponent* Flame : Flames) Flame->SetRelativeScale3D(FVector(.7f));
}

void ABossOrb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFriendly) { TickFriendly(DeltaSeconds); return; }
	if (!GetWorld() || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	if (const ABossBase* BossOwner = Cast<ABossBase>(GetOwner()))
		if (BossOwner->IsDead() || BossOwner->IsActorBeingDestroyed()) { Destroy(); return; }
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	UHealthComponent* PlayerHealth = PlayerPawn ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (PlayerHealth && PlayerHealth->IsDead()) { Destroy(); return; }

	const FVector Start = GetActorLocation();
	const FVector End = Start + Velocity * DeltaSeconds;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(HitRadius);
	FHitResult PlayerHit, SceneHit;
	const ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn);
	UCapsuleComponent* Capsule = PlayerCharacter ? PlayerCharacter->GetCapsuleComponent() : nullptr;
	// 直接扫实际胶囊：Pawn默认忽略Visibility，普通Visibility追踪会漏掉玩家。
	const bool bPlayerHit = Capsule && PlayerHealth && Capsule->SweepComponent(PlayerHit, Start, End, FQuat::Identity, Sphere);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SummonerOrb), false, this);
	Params.AddIgnoredActor(GetOwner());
	if (PlayerPawn) Params.AddIgnoredActor(PlayerPawn);
	Params.bFindInitialOverlaps = true;
	FCollisionResponseParams SceneResponse(ECR_Ignore);
	SceneResponse.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
	SceneResponse.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
	SceneResponse.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Block);
	const bool bSceneHit = GetWorld()->SweepSingleByChannel(SceneHit, Start, End, FQuat::Identity, ECC_Pawn, Sphere, Params, SceneResponse);
	// 地形/实体必须实际BlockPawn；其它Pawn和触发区不吃弹。同距地形优先，含出生即穿入实体。
	if (bSceneHit && (!bPlayerHit || SceneHit.Time <= PlayerHit.Time + KINDA_SMALL_NUMBER))
	{
		SetActorLocation(SceneHit.Location);
		UE_LOG(LogAICompanionDemo, Log, TEXT("[BossOrb] collision=Scene orb=%s target=%s center=%s"), *GetPathName(), *GetPathNameSafe(SceneHit.GetActor()), *SceneHit.Location.ToString());
		Destroy(); return;
	}
	if (bPlayerHit)
	{
		SetActorLocation(PlayerHit.Location);
		UE_LOG(LogAICompanionDemo, Log, TEXT("[BossOrb] collision=Player orb=%s center=%s hpBefore=%.3f"), *GetPathName(), *PlayerHit.Location.ToString(), PlayerHealth->GetCurrentHealth());
		// 玩家岩甲反伤可在ApplyDamage同栈杀宿主并Destroy当前Orb，之后不能继续造火浪。
		TWeakObjectPtr<ABossOrb> WeakThis(this);
		TWeakObjectPtr<UHealthComponent> WeakPlayerHealth(PlayerHealth);
		TWeakObjectPtr<ABossBase> WeakBossOwner(Cast<ABossBase>(GetOwner()));
		const bool bHadBossOwner = WeakBossOwner.IsValid();
		PlayerHealth->ApplyDamage(Damage, this);
		if (!WeakThis.IsValid() || IsActorBeingDestroyed() || !WeakPlayerHealth.IsValid() || WeakPlayerHealth->IsDead()
			|| (bHadBossOwner && (!WeakBossOwner.IsValid() || WeakBossOwner->IsDead() || WeakBossOwner->IsActorBeingDestroyed())))
		{
			if (WeakThis.IsValid() && !IsActorBeingDestroyed()) Destroy();
			return;
		}
		UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Fire, GetActorLocation(), 0.5f, 1.f);
		Destroy();
		return;
	}
	SetActorLocation(End);

	// 超时自毁（6s；实际距离随Launch速度，弹幕仍有宿主24件上限）
	LifeSeconds -= DeltaSeconds;
	if (LifeSeconds <= 0.f)
	{
		Destroy();
	}
}

FString ABossOrb::DebugOrbCombatState() const
{
#if WITH_EDITOR
	const FVector Core = HitCore->GetComponentLocation();
	return FString::Printf(TEXT("{\"path\":\"%s\",\"speed\":%.3f,\"damage\":%.3f,\"life\":%.4f,\"friendly\":%s,\"owner\":\"%s\",\"vx\":%.4f,\"vy\":%.4f,\"vz\":%.4f,\"radius\":%.3f,\"coreVisible\":%s,\"coreX\":%.4f,\"coreY\":%.4f,\"coreZ\":%.4f}"),
		*GetPathName(), Velocity.Size(), Damage, LifeSeconds, bFriendly ? TEXT("true") : TEXT("false"), *GetPathNameSafe(GetOwner()),
		Velocity.X, Velocity.Y, Velocity.Z, HitRadius, HitCore->IsVisible() ? TEXT("true") : TEXT("false"), Core.X, Core.Y, Core.Z);
#else
	return TEXT("{}");
#endif
}

void ABossOrb::DebugLaunchOrb(const FVector& Direction, float InDamage, float Speed)
{
#if WITH_EDITOR
	if (!bFriendly && GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		Launch(Direction, InDamage, Speed);
		LifeSeconds = 6.f;
	}
#endif
}
