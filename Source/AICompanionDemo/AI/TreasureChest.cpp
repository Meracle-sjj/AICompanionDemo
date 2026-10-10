/**
 * [INPUT]: TreasureChest.h、原创M_StarterHalo/M_StarterMote的AuraColor/AuraOpacity、WorldStatic实际探地、MaterialBillboard世界半尺寸、Health/导演终局查询；原开箱音效。Niagara接口仅供离线Editor制作桥。
 * [OUTPUT]: 普通箱动态收费；初始箱85cm贴地金环/六个8–12cm稀疏上浮光点/柔和点光，无火焰柱或头顶球。游戏Tick驱动相位与透明度，真实暂停和低流速冻结；领取/玩家死亡/终局/EndPlay显式销毁。免费三选一账本不变。
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
#include "Components/DecalComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	const FLinearColor kStarterAuraTint(3.f, 1.8f, .22f, 1.f);
	constexpr int32 kStarterMoteCount = 6;
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

	// 原创无Time材质：全部呼吸和漂浮由此Actor的游戏Tick驱动，暂停不继续闪动。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HaloMaterialFinder(TEXT("/Game/VFX/StarterChest/M_StarterHalo.M_StarterHalo"));
	if (HaloMaterialFinder.Succeeded()) { StarterHaloBaseMaterial = HaloMaterialFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MoteMaterialFinder(TEXT("/Game/VFX/StarterChest/M_StarterMote.M_StarterMote"));
	if (MoteMaterialFinder.Succeeded()) { StarterMoteBaseMaterial = MoteMaterialFinder.Object; }
	// 仅离线制作DebugConfigureColoredFlameAsset使用；运行宝箱不创建Niagara组件。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameMaterialFinder(TEXT("/Game/VFX/Enrage/MI_EnrageFlame.MI_EnrageFlame"));
	if (FlameMaterialFinder.Succeeded()) { StarterFlameBaseMaterial = FlameMaterialFinder.Object; }
	StarterLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StarterLight"));
	StarterLight->SetupAttachment(RootComponent);
	StarterLight->SetAbsolute(false, false, true);
	StarterLight->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	StarterLight->SetLightColor(FLinearColor(1.f, 0.78f, 0.15f));
	StarterLight->SetIntensity(650.f);
	StarterLight->SetAttenuationRadius(320.f);
	StarterLight->SetCastShadows(false);
	StarterLight->SetVisibility(false);
}

void ATreasureChest::ConfigureStarterChest()
{
	if (bOpened || bStarterAuraRetired) { return; }
	bStarterChest = true;
	if (HasActorBegunPlay()) { BuildStarterAura(); }
}

bool ATreasureChest::BuildStarterAura()
{
	if (bStarterAuraRetired || bOpened || !bStarterChest || !GetWorld() || !RootComponent) { return false; }
	if (IsValid(StarterHalo) && StarterMotes.Num() == kStarterMoteCount) { return true; }
	if (!StarterHaloBaseMaterial || !StarterMoteBaseMaterial)
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[StarterChest] 原创光晕材质未加载，初始补给交互仍可用"));
		return false;
	}
	FLinearColor Color;
	float Opacity = 0.f;
	for (UMaterialInterface* Material : { StarterHaloBaseMaterial.Get(), StarterMoteBaseMaterial.Get() })
	{
		if (!Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AuraColor")), Color)
			|| !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AuraOpacity")), Opacity))
		{
			UE_LOG(LogAICompanionDemo, Error, TEXT("[StarterChest] 光晕材质缺AuraColor/AuraOpacity，拒绝错误外观"));
			return false;
		}
	}

	FHitResult FloorHit;
	FCollisionObjectQueryParams FloorObjects;
	FloorObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams FloorQuery;
	FloorQuery.AddIgnoredActor(this);
	const FVector Probe = GetActorLocation() + FVector(0.f, 0.f, 8000.f);
	if (!GetWorld()->LineTraceSingleByObjectType(FloorHit, Probe, Probe - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorQuery))
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[StarterChest] 未探到实际地面，拒绝悬空光环"));
		return false;
	}
	StarterGroundLocation = FloorHit.ImpactPoint;
	StarterGroundNormal = FloorHit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	StarterAuraAge = 0.f;
	StarterHaloMaterial = UMaterialInstanceDynamic::Create(StarterHaloBaseMaterial, this);
	if (!StarterHaloMaterial) { return false; }
	StarterHaloMaterial->SetVectorParameterValue(TEXT("AuraColor"), kStarterAuraTint);
	StarterHalo = NewObject<UDecalComponent>(this);
	StarterHalo->ComponentTags.Add(TEXT("StarterChestHalo"));
	AddInstanceComponent(StarterHalo);
	StarterHalo->SetupAttachment(RootComponent);
	StarterHalo->SetAbsolute(true, true, true);
	// 局部X投射轴朝地面；等价平地Pitch=-90，局部Y/Z半尺寸85cm，投射半深24cm。
	StarterHalo->DecalSize = FVector(24.f, 85.f, 85.f);
	StarterHalo->SetDecalMaterial(StarterHaloMaterial);
	StarterHalo->SetFadeScreenSize(.001f);
	StarterHalo->SetSortOrder(2);
	StarterHalo->SetWorldLocationAndRotation(StarterGroundLocation + StarterGroundNormal * 2.f,
		FRotationMatrix::MakeFromX(-StarterGroundNormal).ToQuat());
	StarterHalo->RegisterComponent();

	for (int32 Index = 0; Index < kStarterMoteCount; ++Index)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(StarterMoteBaseMaterial, this);
		if (!Material) { ClearStarterAura(); return false; }
		Material->SetVectorParameterValue(TEXT("AuraColor"), kStarterAuraTint);
		UMaterialBillboardComponent* Mote = NewObject<UMaterialBillboardComponent>(this);
		Mote->ComponentTags.Add(TEXT("StarterChestMote"));
		AddInstanceComponent(Mote);
		Mote->SetupAttachment(RootComponent);
		Mote->SetAbsolute(true, true, true);
		Mote->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mote->SetGenerateOverlapEvents(false);
		Mote->SetCanEverAffectNavigation(false);
		Mote->SetCastShadow(false);
		// UE5.6Renderer使用±BaseSize顶点，false为世界厘米：全幅8–12cm，非屏幕大球。
		const float HalfSize = 4.f + static_cast<float>(Index % 3);
		Mote->AddElement(Material, nullptr, false, HalfSize, HalfSize, nullptr);
		Mote->RegisterComponent();
		StarterMotes.Add(Mote);
		StarterMoteMaterials.Add(Material);
	}
	StarterLight->SetVisibility(true);
	TickStarterAura(0.f);
	UE_LOG(LogAICompanionDemo, Log, TEXT("[StarterChest] 稀有光晕已创建：85cm贴地金环/%d稀疏光点，无Niagara火焰"), StarterMotes.Num());
	return true;
}

void ATreasureChest::TickStarterAura(float DeltaSeconds)
{
	StarterAuraAge += FMath::Max(DeltaSeconds, 0.f);
	// 约4.2秒一轮柔和呼吸；无材质Time、定时器或独立粒子模拟，暂停冻结整个表现。
	const float Pulse = .5f + .5f * FMath::Sin(StarterAuraAge * 1.5f);
	StarterHaloOpacity = .56f + .18f * Pulse;
	if (IsValid(StarterHaloMaterial)) { StarterHaloMaterial->SetScalarParameterValue(TEXT("AuraOpacity"), StarterHaloOpacity); }
	if (StarterLight) { StarterLight->SetIntensity(520.f + 180.f * Pulse); }
	const FMatrix GroundFrame = FRotationMatrix::MakeFromZ(StarterGroundNormal);
	const FVector GroundX = GroundFrame.GetUnitAxis(EAxis::X);
	const FVector GroundY = GroundFrame.GetUnitAxis(EAxis::Y);
	for (int32 Index = 0; Index < StarterMotes.Num(); ++Index)
	{
		if (!IsValid(StarterMotes[Index]) || !StarterMoteMaterials.IsValidIndex(Index) || !IsValid(StarterMoteMaterials[Index])) { continue; }
		const float Period = 4.6f + static_cast<float>(Index % 3) * .65f;
		const float Cycle = FMath::Frac(StarterAuraAge / Period + static_cast<float>(Index) / kStarterMoteCount);
		const float Angle = static_cast<float>(Index) * (2.f * PI / kStarterMoteCount)
			+ .15f * FMath::Sin(StarterAuraAge * .42f + Index * 1.7f);
		const float Radius = 57.f + 6.f * FMath::Sin(StarterAuraAge * .6f + Index * 2.1f);
		const float Height = 25.f + Cycle * (80.f + static_cast<float>(Index % 2) * 12.f);
		StarterMotes[Index]->SetWorldLocation(StarterGroundLocation
			+ (GroundX * FMath::Cos(Angle) + GroundY * FMath::Sin(Angle)) * Radius
			+ StarterGroundNormal * Height);
		const float Fade = FMath::Square(FMath::Sin(Cycle * PI));
		StarterMoteMaterials[Index]->SetScalarParameterValue(TEXT("AuraOpacity"), Fade * (.55f + .12f * Pulse));
	}
}

bool ATreasureChest::DebugConfigureColoredFlameAsset(UNiagaraSystem* System)
{
#if WITH_EDITOR
	// 仅离线制作固定专用资产；PIE/Runtime与第三方源路径均不可写。
	if (!System || System->GetPathName() != TEXT("/Game/VFX/Enrage/NS_ColoredFireMid.NS_ColoredFireMid")
		|| (GetWorld() && GetWorld()->IsGameWorld()) || !StarterFlameBaseMaterial) { return false; }
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		const FVersionedNiagaraEmitterData* Data = Instance.GetEmitterData();
		if (!Instance.Emitter || !Instance.Emitter->IsIn(System) || !Data) { return false; }
		for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			if (!Renderer || !Renderer->IsIn(System)) { return false; }
	}
	System->Modify();
	const FNiagaraVariable MaterialParameter(FNiagaraTypeDefinition::GetUMaterialDef(), TEXT("User.Flame_Material"));
	System->GetExposedParameters().AddParameter(MaterialParameter);
	System->GetExposedParameters().SetUObject(StarterFlameBaseMaterial, MaterialParameter);
	int32 Bound = 0;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		for (UNiagaraRendererProperties* Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
		{
			UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
			if (!Sprite || !Sprite->Material || !Sprite->Material->GetPathName().Contains(TEXT("MI_FireMid_Loop_01_Flame"))) { continue; }
			Sprite->Modify();
			Sprite->MaterialUserParamBinding.Parameter = MaterialParameter;
			Sprite->Material = StarterFlameBaseMaterial;
			Sprite->MICMaterial = nullptr;
			++Bound;
		}
	}
	if (Bound != 1) { return false; }
	System->RequestCompile(false);
	System->WaitForCompilationComplete();
	System->CacheFromCompiledData();
	UE_LOG(LogAICompanionDemo, Log, TEXT("[ColoredFlameAsset] 专用系统已绑定%d火焰Renderer，等待工具保存"), Bound);
	return true;
#else
	return false;
#endif
}

void ATreasureChest::ClearStarterAura()
{
	if (IsValid(StarterHalo)) { StarterHalo->DestroyComponent(); }
	StarterHalo = nullptr;
	StarterHaloMaterial = nullptr;
	for (UMaterialBillboardComponent* Mote : StarterMotes)
	{
		if (IsValid(Mote)) { Mote->DestroyComponent(); }
	}
	StarterMotes.Reset();
	StarterMoteMaterials.Reset();
	StarterHaloOpacity = 0.f;
	bStarterAuraRetired = true;
	if (StarterLight) { StarterLight->SetVisibility(false); }
}

void ATreasureChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearStarterAura();
	Super::EndPlay(EndPlayReason);
}

FString ATreasureChest::DebugChestState() const
{
#if WITH_EDITOR
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	const AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr;
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bNearby = Hero && FVector::DistSquared(Hero->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractRadius);
	int32 LiveMotes = 0;
	FString MoteStates;
	for (int32 Index = 0; Index < StarterMotes.Num(); ++Index)
	{
		const UMaterialBillboardComponent* Mote = StarterMotes[Index];
		if (!IsValid(Mote)) { continue; }
		++LiveMotes;
		float Opacity = 0.f;
		if (StarterMoteMaterials.IsValidIndex(Index) && IsValid(StarterMoteMaterials[Index]))
			StarterMoteMaterials[Index]->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AuraOpacity")), Opacity);
		const FVector Position = Mote->GetComponentLocation();
		if (!MoteStates.IsEmpty()) { MoteStates += TEXT(","); }
		MoteStates += FString::Printf(TEXT("{\"position\":[%.3f,%.3f,%.3f],\"opacity\":%.5f}"), Position.X, Position.Y, Position.Z, Opacity);
	}
	FLinearColor Tint = FLinearColor::Black;
	if (StarterHaloMaterial) { StarterHaloMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AuraColor")), Tint); }
	const bool bPaused = GetWorld() && (GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f);
	return FString::Printf(TEXT("{\"starter\":%s,\"opened\":%s,\"price\":%d,\"gold\":%d,\"paidChestCount\":%d,\"glow\":%s,\"nearby\":%s,\"interactRadius\":%.3f,\"halo\":%s,\"liveMotes\":%d,\"phase\":%.5f,\"haloOpacity\":%.5f,\"paused\":%s,\"retired\":%s,\"groundLocation\":[%.3f,%.3f,%.3f],\"groundNormal\":[%.4f,%.4f,%.4f],\"auraTint\":[%.3f,%.3f,%.3f,%.3f],\"motes\":[%s]}"),
		bStarterChest ? TEXT("true") : TEXT("false"), bOpened ? TEXT("true") : TEXT("false"),
		bStarterChest ? 0 : (Director ? Director->GetNextChestCost() : OpenCost), Director ? Director->GetGold() : -1,
		Director ? Director->ChestOpenCount : -1, StarterLight && StarterLight->IsVisible() ? TEXT("true") : TEXT("false"),
		bNearby ? TEXT("true") : TEXT("false"), InteractRadius, IsValid(StarterHalo) && StarterHalo->IsVisible() ? TEXT("true") : TEXT("false"),
		LiveMotes, StarterAuraAge, StarterHaloOpacity, bPaused ? TEXT("true") : TEXT("false"), bStarterAuraRetired ? TEXT("true") : TEXT("false"),
		StarterGroundLocation.X, StarterGroundLocation.Y, StarterGroundLocation.Z, StarterGroundNormal.X, StarterGroundNormal.Y, StarterGroundNormal.Z,
		Tint.R, Tint.G, Tint.B, Tint.A, *MoteStates);
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
	if (bStarterChest && !bStarterAuraRetired) { BuildStarterAura(); }
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
			ClearStarterAura();
			return;
		}
		const bool bFreeze = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
		if (bFreeze) { return; }
		if (!bStarterAuraRetired && IsValid(StarterHalo)) { TickStarterAura(DeltaSeconds); }
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
	ClearStarterAura();
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
