/**
 * [INPUT]: BossAltar契约、玩家Health/终局状态、BossBase生成、独立Unlit/Additive柔边红柱材质与Engine Cylinder。
 * [OUTPUT]: 红导航细柱触发后按三游戏秒平滑扩大至110cm半径覆盖祭坛；暂停/奖励冻结、死亡/终局清柱，末刻唯一生成，无文字/数字/界面框。
 * [POS]: AI/ 的 Boss 终局引导件实现：祭坛出现本身即是事件（红柱+光），玩家走近=选择应战时机
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "BossAltar.h"

#include "AI/BossBase.h"
#include "AICompanionDemoGameMode.h"
#include "Health/HealthComponent.h"
#include "AICompanionDemo.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FLinearColor kBossAltarRed(0.9f, 0.12f, 0.08f);
	constexpr float SummonDelay = 3.f;
	constexpr float TriggerDistance = 500.f;
	constexpr float ColumnHeight = 3000.f;
	constexpr float ColumnIdleRadius = 8.f;
	constexpr float ColumnFullRadius = 110.f;
	constexpr float CylinderRadius = 50.f;
	constexpr float CylinderHeight = 100.f;
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
	SummonColumn = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SummonColumn"));
	SummonColumn->SetupAttachment(RootComponent);
	// 柱尺寸不继承祭坛BodyMesh的1.5/1.2缩放；位置仍跟随祭坛，始终竖直向上。
	SummonColumn->SetAbsolute(false, true, true);
	SummonColumn->SetWorldRotation(FRotator::ZeroRotator);
	SummonColumn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SummonColumn->SetCanEverAffectNavigation(false);
	SummonColumn->SetCastShadow(false);
	SummonColumn->SetVisibility(false);
	if (CylinderFinder.Succeeded()) SummonColumn->SetStaticMesh(CylinderFinder.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColumnMatFinder(TEXT("/Game/VFX/BossAltar/M_SummonColumn.M_SummonColumn"));
	if (ColumnMatFinder.Succeeded()) SummonColumnMaterial = ColumnMatFinder.Object;
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
	if (SummonColumnMaterial)
	{
		SummonColumnMID = UMaterialInstanceDynamic::Create(SummonColumnMaterial, this);
		SummonColumn->SetMaterial(0, SummonColumnMID);
		UpdateSummonColumn();
	}
	else UE_LOG(LogAICompanionDemo, Error, TEXT("[BossAltar] 缺独立M_SummonColumn，红柱显示不可用"));
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

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const UHealthComponent* Health = IsValid(PlayerPawn) ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	const AAICompanionDemoGameMode* GM = Cast<AAICompanionDemoGameMode>(UGameplayStatics::GetGameMode(this));
	if (!Health || Health->IsDead() || (GM && GM->IsEndingShown()))
	{
		ClearCountdown();
		return;
	}
	if (GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	if (bCountingDown)
	{
		CountdownRemaining = FMath::Max(0.f, CountdownRemaining - DeltaSeconds);
		if (CountdownRemaining <= 0.f) ActivateBoss();
		else UpdateSummonColumn();
	}
	else if (!bSummonCommitted && FVector::Dist2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= TriggerDistance)
	{
		StartCountdown();
	}
}

void ABossAltar::StartCountdown()
{
	if (bCountingDown || bSummonCommitted) return;
	bCountingDown = true;
	CountdownRemaining = SummonDelay;
	UpdateSummonColumn();
	UE_LOG(LogAICompanionDemo, Log, TEXT("[BossAltar] %s 预备召唤，三秒后降世"), *BossName);
}

void ABossAltar::ClearCountdown()
{
	if (SummonColumn) SummonColumn->SetVisibility(false);
	bCountingDown = false;
	CountdownRemaining = 0.f;
}

void ABossAltar::UpdateSummonColumn()
{
	if (!SummonColumn || !SummonColumnMID || bSummonCommitted) return;
	const float Phase = bCountingDown ? FMath::Clamp(1.f - CountdownRemaining / SummonDelay, 0.f, 1.f) : 0.f;
	const float SmoothPhase = Phase * Phase * (3.f - 2.f * Phase);
	const float Radius = FMath::Lerp(ColumnIdleRadius, ColumnFullRadius, SmoothPhase);
	SummonColumn->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, ColumnHeight * .5f - 5.f));
	SummonColumn->SetWorldScale3D(FVector(Radius / CylinderRadius, Radius / CylinderRadius, ColumnHeight / CylinderHeight));
	SummonColumnMID->SetScalarParameterValue(TEXT("ColumnHeight"), ColumnHeight);
	SummonColumnMID->SetScalarParameterValue(TEXT("ColumnOpacity"), FMath::Lerp(.30f, .72f, SmoothPhase));
	SummonColumn->SetVisibility(true);
}

void ABossAltar::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCountdown();
	Super::EndPlay(EndPlayReason);
}

void ABossAltar::ActivateBoss()
{
	if (bSummonCommitted) return;
	bSummonCommitted = true;
	ClearCountdown();
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

	Destroy();
}

FString ABossAltar::DebugAltarState() const
{
	const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("boss_name"), BossName);
	State->SetNumberField(TEXT("boss_index"), BossIndex);
	State->SetBoolField(TEXT("counting_down"), bCountingDown);
	State->SetBoolField(TEXT("summon_committed"), bSummonCommitted);
	State->SetNumberField(TEXT("remaining"), CountdownRemaining);
	State->SetNumberField(TEXT("phase"), bCountingDown ? FMath::Clamp(1.f - CountdownRemaining / SummonDelay, 0.f, 1.f) : 0.f);
	State->SetNumberField(TEXT("digit"), bCountingDown ? FMath::CeilToInt(CountdownRemaining) : 0);
	State->SetBoolField(TEXT("widget_in_viewport"), false);
	State->SetBoolField(TEXT("column_visible"), SummonColumn && SummonColumn->IsVisible());
	State->SetStringField(TEXT("column_material"), GetPathNameSafe(SummonColumnMID));
	State->SetNumberField(TEXT("column_radius"), SummonColumn ? SummonColumn->GetComponentScale().X * CylinderRadius : 0.f);
	State->SetNumberField(TEXT("column_height"), SummonColumn ? SummonColumn->GetComponentScale().Z * CylinderHeight : 0.f);
	State->SetNumberField(TEXT("column_opacity"), SummonColumnMID ? SummonColumnMID->K2_GetScalarParameterValue(TEXT("ColumnOpacity")) : 0.f);
	State->SetNumberField(TEXT("column_collision"), SummonColumn ? static_cast<int32>(SummonColumn->GetCollisionEnabled()) : -1);
	State->SetBoolField(TEXT("column_affects_navigation"), SummonColumn && SummonColumn->CanEverAffectNavigation());
	FString Json;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
	return Json;
}
