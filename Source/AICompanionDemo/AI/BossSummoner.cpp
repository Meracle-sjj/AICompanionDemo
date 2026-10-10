/**
 * [INPUT]: 依赖 BossSummoner.h 的状态机契约、AI/EnemyGrunt（召唤杂兵 spawn，VoidRusher 变体）、HealthComponent 的 SetInvulnerable/ClearInvulnerable（护盾免伤通道）与 OnDamaged（受击表现）与血量读数、Kismet 的 GetPlayerPawn
 * [OUTPUT]: 对外提供 ABossSummoner 的实现——BeginPlay 覆写体量（血 600/慢速 160）+ 开局护盾+首召；Tick 状态机：
 *  护盾期（免伤/紫罩/清杂破盾）与眩晕可伤/血档再召，三波为4→5→5，后两波各1双攻速紫Warlock；
 *  受击播 Hitreact（节流）+ 死亡播 Death_front（基类表现槽）；三轮血档与基类死亡上报；每种召唤单位均先经导演共享虫战限额准入。两态弹幕由BossSummonerBarrage推进，玩家死亡停主状态推进，死亡/EndPlay清活弹/孤儿杂兵/动画Timer。
 * [POS]: AI/ Boss 家族成员一实现：清杂规则型——护盾=无敌早退通道（回血抵消灰巫术退役，死亡判定污染根除）；**受击阈值 HitReactDamageThreshold=99999 常态禁用（09-30 用户裁决"删常态受击动作"，暴击落地后调低启用）**
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "BossSummoner.h"

#include "AI/BossOrb.h"
#include "AI/EnemyGrunt.h"
#include "AI/WaveDirector.h"
#include "AICompanionDemoGameMode.h"
#include "AICompanionDemo.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

ABossSummoner::ABossSummoner()
{
	BossName = TEXT("召唤师");
	BossIndex = 1;
	MeleeDamage = 12.f;
	MeleeInterval = 2.f;
	MeleeRange = 200.f;
	bHoldPosition = true; // 护盾与眩晕均站桩；弹幕独立推进，不借基类近战计时。
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = 160.f;   // 召唤师几乎不走——压迫全靠杂兵

		// 召唤师换装（09-30 模型调研定案：Paragon Sevarog——亡灵收割者站桩施法者，全套动画自带）：
		// 单节点模式播自带动画——绕开非 Manny 骨骼的 ABP 重定向（静默参考姿势案红线）。
		// 09-30 表现层大轮：Stun_Loop=破盾可伤的形体语言 / Hitreact=受击动作 / Death_front=死亡表现（用户点名"死亡要看得见"）
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> SevarogMeshFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog.Sevarog"));
		if (SevarogMeshFinder.Succeeded())
		{
			GetMesh()->SetSkeletalMeshAsset(SevarogMeshFinder.Object);
			GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		}
		static ConstructorHelpers::FObjectFinder<UAnimationAsset> SevarogIdleFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Idle.Idle"));
		if (SevarogIdleFinder.Succeeded())
		{
			IdleAnim = SevarogIdleFinder.Object;
			GetMesh()->SetAnimation(IdleAnim);   // SingleNode 默认动画
		}
		static ConstructorHelpers::FObjectFinder<UAnimationAsset> SevarogCastFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Cast.Cast"));
		if (SevarogCastFinder.Succeeded())
		{
			CastAnim = SevarogCastFinder.Object;
		}
		static ConstructorHelpers::FObjectFinder<UAnimationAsset> SevarogStunFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Stun_Loop.Stun_Loop"));
		if (SevarogStunFinder.Succeeded())
		{
			StunLoopAnim = SevarogStunFinder.Object;
		}
		static ConstructorHelpers::FObjectFinder<UAnimationAsset> SevarogHitFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Hitreact_Front.Hitreact_Front"));
		if (SevarogHitFinder.Succeeded())
		{
			HitReactAnim = SevarogHitFinder.Object;
		}
		static ConstructorHelpers::FObjectFinder<UAnimationAsset> SevarogDeathFinder(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Death_front.Death_front"));
		if (SevarogDeathFinder.Succeeded())
		{
			DeathPresentationAnim = SevarogDeathFinder.Object;   // 基类表现槽：HandleDeath 播死亡动画（ragdoll 兜底退役）
		}
	}
}

void ABossSummoner::BeginPlay()
{
	Super::BeginPlay();

	// 体量覆写（召唤师血 600——09-30 用户实测"打不动"调优：三轮各 200，眩晕期掉 200 即推进；破盾横幅已补状态可见）
	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(600.f, /*bFillCurrent*/ true);
		HealthComponent->OnDamaged.AddDynamic(this, &ABossSummoner::HandleHitReact);
	}

	SummonMinions();
}

void ABossSummoner::HandleHitReact(UHealthComponent* /*HealthComp*/, AActor* /*InstigatorActor*/)
{
	// 受击表现（09-30 用户点名"能被打要有受伤动作"）：眩晕期被打播 Hitreact，节流 0.5s 防弹幕式抽搐；
	// 护盾期不打动画（免伤观感——掉了血才演受伤就穿帮了）。
	// 09-30 阈值入口（用户裁决"删常态受击动作"）：单次伤害低于阈值一律不播（默认 99999=禁用）
	if (bShieldUp || !HitReactAnim || !GetMesh() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}
	if (HealthComponent && HealthComponent->GetLastDamageTaken() < HitReactDamageThreshold)
	{
		return;
	}
	if (HitReactCooldown > 0.f)
	{
		return;
	}
	HitReactCooldown = 0.5f;
	PlayTransientAnim(HitReactAnim);
}

void ABossSummoner::PlayBaseAnim()
{
	if (!GetMesh() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}
	if (bShieldUp)
	{
		if (IdleAnim)
		{
			GetMesh()->PlayAnimation(IdleAnim, true);
		}
	}
	else if (StunLoopAnim)
	{
		// 破盾眩晕循环——"现在能打了"的形体语言（09-30 用户点名"可伤状态要有提示"）
		GetMesh()->PlayAnimation(StunLoopAnim, true);
	}
}

void ABossSummoner::PlayTransientAnim(UAnimationAsset* Anim)
{
	if (!Anim || !GetMesh())
	{
		return;
	}
	GetMesh()->Stop();
	GetMesh()->PlayAnimation(Anim, false);
	const float PlayLength = Anim->GetPlayLength();
	GetWorldTimerManager().ClearTimer(TransientAnimTimer);   // 瞬态互顶：旧回切作废（Cast 播一半挨打→Hitreact 接管）
	TWeakObjectPtr<ABossSummoner> WeakThis(this);
	GetWorldTimerManager().SetTimer(TransientAnimTimer, [WeakThis]()
	{
		if (WeakThis.IsValid()) WeakThis->PlayBaseAnim(); // 死亡/EndPlay不残留裸this回调。
	}, PlayLength, false);
}

void ABossSummoner::SummonMinions()
{
	bShieldUp = true;

	// 护盾免伤迁入无敌早退通道（09-30"0 血站桩"案根治）：旧"事后回血抵消"是灰巫术——
	// ApplyDamage 的死亡判定读成员血量，广播期间 Heal 同步拉回，致死事实被静默吃掉（配合
	// 高 DPS 跨血档竞态=0 血永生站桩）。SetInvulnerable 让伤害在钳制与广播之前全额早退，
	// 连 OnDamaged 都不广播（受击动画/仇恨通道护盾期天然静默——语义更纯）
	if (HealthComponent)
	{
		HealthComponent->SetInvulnerable(9999.f);
	}

	// 召唤施法动画（Sevarog 自带 Cast——召唤仪式感；播完回 Idle，EnemyGrunt 挥拳同款范式）
	PlayTransientAnim(CastAnim);

	if (UWorld* World = GetWorld())
	{
		Minions.Reset();
		OrbCooldown = FMath::Min(OrbCooldown, FMath::Max(.15f, OrbInterval));
		int32 SpawnedCount = 0;
		const int32 MinionCount = CycleIndex > 0 ? 5 : 4;
		for (int32 i = 0; i < MinionCount; ++i)
		{
			const bool bFastWarlock = CycleIndex > 0 && i == 0;
			const EGruntVariant Variant = bFastWarlock ? EGruntVariant::Warlock : EGruntVariant::VoidRusher;
			const AAICompanionDemoGameMode* GM = World->GetAuthGameMode<AAICompanionDemoGameMode>();
			if (AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr)
				if (!Director->CanSpawnMob(Variant)) continue;
			const float Angle = (static_cast<float>(i) / MinionCount) * 2.f * PI + FMath::FRandRange(0.f, 0.5f);
			const FVector Loc = GetActorLocation() + FVector(FMath::Cos(Angle) * 400.f, FMath::Sin(Angle) * 400.f, 200.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (AEnemyGrunt* Minion = World->SpawnActor<AEnemyGrunt>(Loc, FRotator::ZeroRotator, Params))
			{
				// 虚空裔变体（数量与质量双加强：420 速/75 血/6 伤——比野生猎犬硬且痛但稍慢可风筝；
				// 紫染/体型/零掉落全在变体内聚，手工 MID 染色与缩放退役）
				if (bFastWarlock) Minion->ApplySummonedWarlock();
				else Minion->ApplyVariant(EGruntVariant::VoidRusher);
				Minion->SetOwner(this);
				Minions.Add(Minion);
				++SpawnedCount;
			}
			else
			{
				UE_LOG(LogAICompanionDemo, Error, TEXT("[Boss] 杂兵 spawn 失败 @ %s——失败必须可见"), *Loc.ToCompactString());
			}
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 召唤师第 %d/3 波：成功 %d/%d，快速紫妖术配额%d（清杂破盾，虫战共享准入）"), CycleIndex + 1, SpawnedCount, MinionCount, CycleIndex > 0 ? 1 : 0);
	}
}

void ABossSummoner::Tick(float DeltaSeconds)
{
	bHoldPosition = true;
	Super::Tick(DeltaSeconds);

	HitReactCooldown = FMath::Max(HitReactCooldown - DeltaSeconds, 0.f);

	if (!HealthComponent || HealthComponent->IsDead())
	{
		return;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const UHealthComponent* PlayerHealth = PlayerPawn ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!PlayerHealth || PlayerHealth->IsDead()) return; // 玩家死亡窗不能继续破盾/血档召唤。
	TickOrbBarrage(DeltaSeconds, PlayerPawn); // 两状态共用，护盾段return不会截断眩晕弹幕。

	if (bShieldUp)
	{
		bHoldPosition = true;   // 护盾期站桩（压迫全靠杂兵）

		// 护盾视觉罩（紫细线球 0.25s 短命循环重画——"他有无敌盾"一眼可读；破盾后消失）
		ShieldPulseSeconds -= DeltaSeconds;
		if (ShieldPulseSeconds <= 0.f)
		{
			ShieldPulseSeconds = 0.25f;
			DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 90.f), 130.f, 16,
				FColor::FromHex(TEXT("B266FF")), false, 0.26f, 0, 2.f);
		}

		// 杂兵全灭轮询：清杂破盾（眩晕可伤开始）——破盾瞬间切 Stun_Loop（可伤的形体语言）
		bool bAnyAlive = false;
		for (const TWeakObjectPtr<AEnemyGrunt>& M : Minions)
		{
			if (M.IsValid() && !M->IsDead())
			{
				bAnyAlive = true;
				break;
			}
		}
		if (!bAnyAlive)
		{
			bShieldUp = false;
			bHoldPosition = true;
			OrbCooldown /= FMath::Clamp(StunnedFireRateScale, .1f, 1.f);
			if (HealthComponent)
			{
				HealthComponent->ClearInvulnerable();   // 破盾=解除无敌（新通道的解除端）
			}
			PlayBaseAnim();
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(95, 5.f, FColor::FromHex(TEXT("9BD7FF")),
					TEXT("护盾破碎！召唤师陷入眩晕——全力输出！"));
			}
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 召唤师护盾破碎——眩晕可伤（血档 %.0f）"),
				HealthComponent->GetMaxHealth() * static_cast<float>(2 - CycleIndex) / 3.f);
		}
		return;   // 护盾期站桩（不追不近战——压迫全靠杂兵）
	}

	// 眩晕期：按血条度量——本体血量跌破下一档（每轮 1/3）即重新召唤+回盾
	const float NextThreshold = HealthComponent->GetMaxHealth() * static_cast<float>(2 - CycleIndex) / 3.f;
	if (HealthComponent->GetCurrentHealth() <= NextThreshold)
	{
		++CycleIndex;
		if (CycleIndex >= 3)
		{
			return;   // 三轮走完：不再召唤（剩余血量打完即死——基类死亡上报）
		}
		SummonMinions();
	}

}
