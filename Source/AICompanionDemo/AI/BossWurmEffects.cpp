/**
 * [INPUT]: BossWurm落点/伤害/探地契约、Character震屏、Health伤害、Niagara与Vfx/SFX、专用M_Wurm_Scorch局部UV材质、PointLight及DrawDebugHelpers
 * [OUTPUT]: 保持破土/入地判定与时序、0.55s强震/单次BossImpact；爆闪硬寿命、不规则软边炭黑焦痕无伤/无光，6s/末2s按ScorchOpacity渐淡/上限4，死亡与EndPlay显式销毁。
 * [POS]: AI/蠕虫落点爆与范围展示实现；状态/轨迹/球代理归BossWurm，岩甲外观归WurmBodyVisual，拆分遵守单文件800行上限
 *  出土与入地两口均提交爆炸语义和受击前落点，Health只按实际扣血发一次漫画反馈。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "BossWurm.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Components/PointLightComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Vfx/VfxLibrary.h"

namespace
{
	void RetireGroundBurst(TWeakObjectPtr<UNiagaraComponent>& WeakComponent)
	{
		if (UNiagaraComponent* Component = WeakComponent.Get())
		{
			// DeactivateImmediate同步OnSystemComplete，AutoDestroy可能已令弱引用失效。
			Component->DeactivateImmediate();
			if (IsValid(Component)) Component->DestroyComponent();
		}
		WeakComponent.Reset();
	}
}

void ABossWurm::TrackGroundBurst(UNiagaraComponent* Component, float Life)
{
	if (!IsValid(Component)) return;
	while (GroundBursts.Num() >= 8)
	{
		RetireGroundBurst(GroundBursts[0].Component);
		GroundBursts.RemoveAt(0);
	}
	Component->ComponentTags.Add(TEXT("WurmGroundBurst"));
	GroundBursts.Add({Component, FMath::Max(.01f, Life)});
}

void ABossWurm::SpawnGroundScorch(const FVector& Center)
{
	if (!ScorchMaterial || !GetWorld() || IsDead()) return;
	while (Scorches.Num() >= 4)
	{
		if (Scorches[0].Decal.IsValid()) Scorches[0].Decal->DestroyComponent();
		Scorches.RemoveAt(0);
	}
	UDecalComponent* Decal = NewObject<UDecalComponent>(this);
	Decal->ComponentTags.Add(TEXT("WurmScorch"));
	Decal->SetDecalMaterial(ScorchMaterial);
	Decal->DecalSize = FVector(600.f, EruptRadius * .75f, EruptRadius * .75f);
	Decal->SetFadeScreenSize(.0001f);
	Decal->RegisterComponent();
	Decal->SetWorldLocation(Center + FVector(0.f, 0.f, 30.f));
	Decal->SetWorldRotation(FRotator(-90.f, 0.f, FMath::FRandRange(0.f, 360.f)));
	if (UMaterialInstanceDynamic* Mid = Decal->CreateDynamicMaterialInstance())
	{
		Mid->SetScalarParameterValue(TEXT("ScorchOpacity"), .9f);
	}
	// 只由Age驱动材质与硬回收；不使用素材的Niagara高度遮罩或贴花Owner销毁定时器。
	Scorches.Add({Decal, 0.f});
}

void ABossWurm::TickGroundEffects(float DeltaSeconds)
{
	for (int32 Index = GroundBursts.Num() - 1; Index >= 0; --Index)
	{
		FWurmTimedBurst& Burst = GroundBursts[Index];
		Burst.Life -= DeltaSeconds;
		if (!Burst.Component.IsValid() || Burst.Life <= 0.f)
		{
			RetireGroundBurst(Burst.Component);
			GroundBursts.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = Scorches.Num() - 1; Index >= 0; --Index)
	{
		FWurmScorch& Scorch = Scorches[Index];
		Scorch.Age += DeltaSeconds;
		if (!Scorch.Decal.IsValid() || Scorch.Age >= 6.f)
		{
			if (Scorch.Decal.IsValid()) Scorch.Decal->DestroyComponent();
			Scorches.RemoveAtSwap(Index); continue;
		}
		// 专用材质Opacity直接接ScorchOpacity，单一游戏时钟避免双重淡出。
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Scorch.Decal->GetDecalMaterial()))
			Mid->SetScalarParameterValue(TEXT("ScorchOpacity"), .9f * FMath::Clamp((6.f - Scorch.Age) / 2.f, 0.f, 1.f));
	}
}

void ABossWurm::ClearGroundEffects()
{
	for (FWurmTimedBurst& Burst : GroundBursts) RetireGroundBurst(Burst.Component);
	GroundBursts.Reset();
	for (FWurmScorch& Scorch : Scorches) if (Scorch.Decal.IsValid()) Scorch.Decal->DestroyComponent();
	Scorches.Reset();
}

void ABossWurm::EruptBurst(const FVector& Center)
{
	// 落点范围爆（入地偷袭与破土瞬爆共用结算——伤害/特效/音效/土屑/震屏/爆光一套）
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		if (FVector::Dist2D(PlayerPawn->GetActorLocation(), Center) <= EruptRadius)
		{
			if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
			{
				PlayerHealth->ApplyCombatDamage(EruptDamage, this, {ECombatDamageKind::Explosion, PlayerPawn->GetActorLocation() + FVector(0, 0, 160)});
			}
		}
		// 独立短震不会污染受击时钟，暂停/死亡/Ending可单独取消。
		if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(PlayerPawn))
		{
			Hero->PulseWurmCameraShake(0.55f);
		}
	}
	if (IsDead() || IsActorBeingDestroyed()) return; // 范围伤害可同栈反伤致死，清理后不再补生特效。
	// 特效=球形爆炸空间主视觉：Large 级爆闪（scale 2.0+抬高 120cm——十一批加强：
	// 贴地矮爆被玩家模型/相机下缘吃掉，抬升+放大让爆炸从地里"立"起来）
	UNiagaraComponent* BurstNS = nullptr;
	if (UNiagaraSystem* NS = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Large_001_01.NS_Sub_EXP_Large_001_01")))
	{
		BurstNS = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), NS,
			Center + FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator, FVector(2.0f), true, true);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 落点爆结算：NS=%s"),
		BurstNS ? TEXT("OK") : TEXT("NULL（资产加载失败）"));
	TrackGroundBurst(BurstNS, 1.2f);
	SpawnGroundScorch(Center);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::BossImpact);

	// 瞬时爆光（十二批：光照是爆炸的最强存在感——4000cd 橙光 0.35s 照亮扎地点全场，
	// 比 NS 粒子更不可错过；TWeakObjectPtr 判活销毁——函数级裸指针埋雷案教训）
	if (UPointLightComponent* FlashLight = NewObject<UPointLightComponent>(this))
	{
		FlashLight->SetIntensity(4000.f);
		FlashLight->SetLightColor(FLinearColor(FColor::FromHex(TEXT("FF7A26"))));
		FlashLight->SetAttenuationRadius(2500.f);
		FlashLight->RegisterComponent();
		FlashLight->SetWorldLocation(Center + FVector(0.f, 0.f, 150.f));
		TWeakObjectPtr<UPointLightComponent> WeakLight(FlashLight);
		FTimerHandle FlashHandle;
		GetWorldTimerManager().SetTimer(FlashHandle, [WeakLight]()
		{
			if (WeakLight.IsValid())
			{
				WeakLight->DestroyComponent();
			}
		}, 0.35f, false);
	}

	// 冲击环（破土爆收束层——承接红圈预告的爆炸波及面）
	DrawBurstRing(Center, 0.6f);

	// 土屑迸射（向上锥形抛物线——破土/入地的物质感）
	for (int32 i = 0; i < 12; ++i)
	{
		const FVector Up = FMath::VRandCone(FVector::UpVector, 1.0f);
		SpawnDebris(Center + FVector(0.f, 0.f, 40.f),
			Up.GetSafeNormal() * FMath::FRandRange(600.f, 1200.f), 0.9f, 30.f);
	}
}

void ABossWurm::DiveImpactBurst(const FVector& Center)
{
	// 入地爆专属视觉（十三批解耦）：球形爆炸空间=爆炸子弹溅射同款双层线框球
	//（ApplyHitChain 橙大球+黄小球同语言——用户点名"爆炸子弹那种球形爆炸空间"；
	// 此前四轮 NS_Sub_EXP_Large 组件 spawn OK 但从未被用户确认可见，彻底弃用）
	const FVector BlastCenter = Center + FVector(0.f, 0.f, 80.f);
	DrawDebugSphere(GetWorld(), BlastCenter, EruptRadius, 12, FColor::Orange, false, 0.4f, 0, 3.f);
	DrawDebugSphere(GetWorld(), BlastCenter, EruptRadius * 0.55f, 12, FColor::Yellow, false, 0.3f, 0, 2.f);
	// 火光层锦上添花（Small 爆闪走 VfxLibrary 统一入口——有则叠加无则静默，不碍事）

	// 震屏+伤害（与破土爆同参 600/25——威胁一致，差异只在预告与视觉语言）
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		if (FVector::Dist2D(PlayerPawn->GetActorLocation(), Center) <= EruptRadius)
		{
			if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
			{
				PlayerHealth->ApplyCombatDamage(EruptDamage, this, {ECombatDamageKind::Explosion, PlayerPawn->GetActorLocation() + FVector(0, 0, 160)});
			}
		}
		if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(PlayerPawn))
		{
			Hero->PulseWurmCameraShake(0.55f);
		}
	}

	if (IsDead() || IsActorBeingDestroyed()) return;
	UNiagaraComponent* DiveBurst = nullptr;
	UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Explosion, BlastCenter, .7f, 2.f, &DiveBurst);
	TrackGroundBurst(DiveBurst, .7f);
	SpawnGroundScorch(Center);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::BossImpact);
	// 土屑迸射（入地的物质感）
	for (int32 i = 0; i < 12; ++i)
	{
		const FVector Up = FMath::VRandCone(FVector::UpVector, 1.0f);
		SpawnDebris(Center + FVector(0.f, 0.f, 40.f),
			Up.GetSafeNormal() * FMath::FRandRange(600.f, 1200.f), 0.9f, 30.f);
	}
}

void ABossWurm::DrawTelegraphRing()
{
	// 破土预告红圈（十批用户点名恢复："红圈让玩家知道接下来要面对沙虫"——巨像建立的贴地
	// telegraph 语义，语言一致性优先；贴地离散环四十五轮范式：24 点各探地+8cm 抬升闭环）
	constexpr int32 NumPts = 24;
	FVector Prev(ForceInit);
	for (int32 i = 0; i <= NumPts; ++i)
	{
		const float Ang = static_cast<float>(i) / NumPts * 2.f * PI;
		FVector Pt(EruptPoint.X + FMath::Cos(Ang) * EruptRadius,
			EruptPoint.Y + FMath::Sin(Ang) * EruptRadius, EruptPoint.Z);
		ResolveGroundZ(Pt);
		Pt.Z += 8.f;
		if (i > 0)
		{
			DrawDebugLine(GetWorld(), Prev, Pt, FColor::Red, false, 0.06f, 0, 10.f);
		}
		Prev = Pt;
	}
}

void ABossWurm::DrawBurstRing(const FVector& Center, float Life)
{
	// 冲击环（非 telegraph）：橙色快闪粗线=爆炸波及面的瞬时表达——与红色预告圈在色彩（红=将要发生/
	// 橙=已经发生）与节奏（1s 倒计时/0.6s 快闪）上双双分家；贴地离散环四十五轮范式（弦矢高 5.1<10 ✓）
	constexpr int32 NumPts = 24;
	FVector Prev(ForceInit);
	for (int32 i = 0; i <= NumPts; ++i)
	{
		const float Ang = static_cast<float>(i) / NumPts * 2.f * PI;
		FVector Pt(Center.X + FMath::Cos(Ang) * EruptRadius,
			Center.Y + FMath::Sin(Ang) * EruptRadius, Center.Z);
		ResolveGroundZ(Pt);
		Pt.Z += 10.f;
		if (i > 0)
		{
			DrawDebugLine(GetWorld(), Prev, Pt, FColor::FromHex(TEXT("FF7A26")), false, Life, 0, 16.f);
		}
		Prev = Pt;
	}
}

