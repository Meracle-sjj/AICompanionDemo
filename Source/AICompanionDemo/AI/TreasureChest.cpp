/**
 * [INPUT]: TreasureChest.h、FireMid及现役MI_EnrageFlame的仅R火焰遮罩/EnrageTint、Niagara安全深复制、Health/导演终局查询；Engine BasicShapeMaterial参数Color及原开箱音效。
 * [OUTPUT]: 普通箱动态收费；初始箱自有低位金焰/MID与免费提示，原Fab/源Niagara不改。领取/玩家死亡/终局/EndPlay显式销毁、暂停冻结；TryGrantStarterUpgrade免费三选一不计收费箱。
 * [POS]: AI/ 的道具交互代表件实现；交互输入在玩家 Character（E 键边沿），开箱账本在 WaveDirector，本类只管"门面与状态"——三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
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
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 箱体宝蓝 / 盖条亮蓝白 / 已开灰（BasicShapeMaterial 真参数名 "Color"）——
	// v4 改色（用户裁决 2026-09-13：不占柠檬蛋糕的黄色，宝箱换蓝系）；与经验晶体的浅青蓝靠色深+尺寸双重区分
	const FLinearColor kChestBlue(0.25f, 0.45f, 1.0f);
	const FLinearColor kLidCyanWhite(0.6f, 0.85f, 1.0f);
	const FLinearColor kOpenedGray(0.28f, 0.28f, 0.3f);
	const FLinearColor kStarterFlameTint(6.f, 3.1f, 0.08f, 1.f);
}

ATreasureChest::ATreasureChest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

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

	// 复用合法火焰与项目已验证的纯遮罩材质；染金只改自有运行时MID/系统副本。
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FireFinder(TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireMid_Loop_001.NS_Sub_FireMid_Loop_001"));
	if (FireFinder.Succeeded()) { StarterSourceSystem = FireFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameMaterialFinder(TEXT("/Game/VFX/Enrage/MI_EnrageFlame.MI_EnrageFlame"));
	if (FlameMaterialFinder.Succeeded()) { StarterFlameBaseMaterial = FlameMaterialFinder.Object; }
	StarterLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StarterLight"));
	StarterLight->SetupAttachment(RootComponent);
	StarterLight->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
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
	StarterLight->SetVisibility(true);
	if (StarterFlames.IsEmpty()) { BuildStarterFlames(); }
}

bool ATreasureChest::BuildStarterFlames()
{
	if (!StarterSourceSystem || !StarterFlameBaseMaterial || !GetWorld() || !RootComponent) { return false; }
	FLinearColor ExistingTint;
	if (!StarterFlameBaseMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EnrageTint")), ExistingTint))
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[StarterChest] 火焰材质缺EnrageTint，拒绝使用橙火冒充金焰"));
		return false;
	}
	StarterFlameMaterial = UMaterialInstanceDynamic::Create(StarterFlameBaseMaterial, this);
	StarterFlameMaterial->SetVectorParameterValue(TEXT("EnrageTint"), kStarterFlameTint);
	StarterFlameSystem = DuplicateObject<UNiagaraSystem>(StarterSourceSystem, this);
	if (!StarterFlameSystem) { ClearStarterFlames(); return false; }
	StarterFlameSystem->ClearFlags(RF_Public | RF_Standalone);
	StarterFlameSystem->SetFlags(RF_Transient);
	// 首先验证完整副本归属，不能修改外部共享的素材包Emitter/Renderer。
	for (FNiagaraEmitterHandle& Handle : StarterFlameSystem->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		FVersionedNiagaraEmitterData* Data = Instance.GetEmitterData();
		if (!Instance.Emitter || !Instance.Emitter->IsIn(StarterFlameSystem) || !Data) { ClearStarterFlames(); return false; }
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			if (!Renderer || !Renderer->IsIn(StarterFlameSystem)) { ClearStarterFlames(); return false; }
	}
	int32 Replaced = 0;
	for (FNiagaraEmitterHandle& Handle : StarterFlameSystem->GetEmitterHandles())
	{
		for (UNiagaraRendererProperties* Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
		{
			UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
			if (!Sprite || !Sprite->Material || !Sprite->Material->GetPathName().Contains(TEXT("MI_FireMid_Loop_01_Flame"))) { continue; }
			Sprite->MaterialUserParamBinding.Parameter.SetName(NAME_None);
			Sprite->Material = StarterFlameMaterial;
#if WITH_EDITORONLY_DATA
			Sprite->MICMaterial = nullptr;
#endif
			++Replaced;
		}
	}
	if (!Replaced) { ClearStarterFlames(); return false; }
	StarterFlameSystem->CacheFromCompiledData();
	const FNiagaraParameterStore& Parameters = StarterFlameSystem->GetExposedParameters();
	const FVector Positions[] = { FVector(-28.f, -12.f, -24.f), FVector(28.f, 12.f, -24.f) };
	for (const FVector& Position : Positions)
	{
		UNiagaraComponent* Flame = NewObject<UNiagaraComponent>(this);
		Flame->ComponentTags.Add(TEXT("StarterChestFlame"));
		Flame->SetAutoActivate(false);
		Flame->SetAutoDestroy(false);
		Flame->SetAsset(StarterFlameSystem);
		Flame->SetupAttachment(RootComponent);
		Flame->SetAbsolute(false, false, true);
		Flame->SetRelativeLocation(Position);
		Flame->SetRelativeScale3D(FVector(.42f));
		Flame->SetCanEverAffectNavigation(false);
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		const FName ColorName(TEXT("User.Flame_Color"));
		if (Parameters.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ColorName)) != INDEX_NONE)
			Flame->SetVariableLinearColor(ColorName, FLinearColor(1.f, .65f, .015f, 1.f));
		const FName QuietParameters[] = { FName(TEXT("User.Spark_Rate")), FName(TEXT("User.Smoke Intensity")) };
		for (const FName& Name : QuietParameters)
			if (Parameters.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), Name)) != INDEX_NONE)
				Flame->SetVariableFloat(Name, 0.f);
		Flame->RegisterComponent();
		StarterFlames.Add(Flame);
		Flame->Activate(true);
	}
	UE_LOG(LogAICompanionDemo, Log, TEXT("[StarterChest] 自有金焰已创建：%d组件/%d材质替换，源资产未修改"), StarterFlames.Num(), Replaced);
	return StarterFlames.Num() == 2;
}

void ATreasureChest::ClearStarterFlames()
{
	for (UNiagaraComponent* Flame : StarterFlames)
	{
		if (!IsValid(Flame)) { continue; }
		Flame->DeactivateImmediate();
		if (IsValid(Flame)) { Flame->DestroyComponent(); }
	}
	StarterFlames.Reset();
	StarterFlameSystem = nullptr;
	StarterFlameMaterial = nullptr;
	if (StarterLight) { StarterLight->SetVisibility(false); }
}

void ATreasureChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearStarterFlames();
	Super::EndPlay(EndPlayReason);
}

FString ATreasureChest::DebugChestState() const
{
#if WITH_EDITOR
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bNearby = Hero && FVector::DistSquared(Hero->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractRadius);
	int32 Live = 0, Active = 0, Paused = 0;
	for (const UNiagaraComponent* Flame : StarterFlames)
	{
		if (!IsValid(Flame)) { continue; }
		++Live;
		if (Flame->IsActive()) { ++Active; }
		if (Flame->IsPaused()) { ++Paused; }
	}
	FLinearColor Tint = FLinearColor::Black;
	if (StarterFlameMaterial) { StarterFlameMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EnrageTint")), Tint); }
	return FString::Printf(TEXT("{\"starter\":%s,\"opened\":%s,\"price\":%d,\"gold\":%d,\"paidChestCount\":%d,\"glow\":%s,\"nearby\":%s,\"interactRadius\":%.3f,\"liveFlames\":%d,\"activeFlames\":%d,\"pausedFlames\":%d,\"flameTint\":[%.3f,%.3f,%.3f,%.3f]}"),
		bStarterChest ? TEXT("true") : TEXT("false"), bOpened ? TEXT("true") : TEXT("false"),
		bStarterChest ? 0 : (Director ? Director->GetNextChestCost() : OpenCost), Director ? Director->GetGold() : -1,
		Director ? Director->ChestOpenCount : -1, StarterLight && StarterLight->IsVisible() ? TEXT("true") : TEXT("false"),
		bNearby ? TEXT("true") : TEXT("false"), InteractRadius, Live, Active, Paused, Tint.R, Tint.G, Tint.B, Tint.A);
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
		const AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
		const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
		const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
		const UHealthComponent* Health = Hero ? Hero->FindComponentByClass<UHealthComponent>() : nullptr;
		if ((Health && Health->IsDead()) || (Director && Director->IsRunTerminated()))
		{
			ClearStarterFlames();
			return;
		}
		const bool bFreeze = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
		for (UNiagaraComponent* Flame : StarterFlames)
			if (IsValid(Flame)) { Flame->SetPaused(bFreeze); }
		if (bFreeze) { return; }
		StarterGlowPhase += DeltaSeconds * 3.f;
		const float Pulse = 0.75f + 0.25f * FMath::Sin(StarterGlowPhase);
		StarterLight->SetIntensity(1000.f * Pulse);
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
	ClearStarterFlames();
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
