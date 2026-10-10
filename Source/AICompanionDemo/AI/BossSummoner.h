/**
 * [INPUT]: 依赖 AI/BossBase 的基类契约（近战框架/死亡上报/血条/死亡表现槽/死亡保险丝）、AI/EnemyGrunt（召唤杂兵 spawn，VoidRusher 变体）、HealthComponent 的 SetInvulnerable/ClearInvulnerable（护盾免伤通道）与 OnDamaged（受击表现）、Kismet 的 GetPlayerPawn
 * [OUTPUT]: 对外提供 ABossSummoner——Boss1「召唤师」（09-30 三 Boss 终局，用户口述机制定稿）：
 *   护盾期免伤（SetInvulnerable早退）+三波召唤：首波4虚空裔，后两波各4虚空裔+1紫色双攻速妖术师；全灭破盾→眩晕可伤（Stun_Loop）；
 *   眩晕期按血条度量：本体血量跌破下一档（MaxHealth×(1-轮次/3)）→ 重新召唤+护盾回升；三轮走完即死；
 *   常态0.6s/1600cm/s单发弹幕，眩晕1.2s/800仍发；24活弹硬上限与死亡/EndPlay所有权清理由BossSummonerBarrage负责；实际枪口三维瞄准、地形阻挡与35cm可见核心同步判定
 * [POS]: AI/ Boss 家族成员一（波 3）：清杂规则型 Boss——他召你清，清杂就是玩法本体
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "AI/BossBase.h"
#include "BossSummoner.generated.h"

class AEnemyGrunt;
class UAnimationAsset;

UCLASS()
class AICOMPANIONDEMO_API ABossSummoner : public ABossBase
{
	GENERATED_BODY()

public:
	ABossSummoner();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UFUNCTION(BlueprintPure, Category = "Debug|Boss")
	FString DebugSummonerCombatState() const;

protected:
	/** 召唤一波杂兵（首波4虚空裔，后两波各4虚空裔+1快速紫妖术；自身周围4m） */
	void SummonMinions();
	void TickOrbBarrage(float DeltaSeconds, const APawn* PlayerPawn);
	void ClearSummonerCombat();
	virtual void HandleDeath(UHealthComponent* HealthComp) override;

	/** 护盾期免伤（09-30"0 血站桩"案根治）：SetInvulnerable 无敌早退通道——伤害在钳制与广播前全额早退，破盾 ClearInvulnerable（旧"回血抵消"灰巫术退役：会污染死亡判定） */

	/** 受击表现（09-30 用户点名"可伤要有受伤动作"）：眩晕期被打播 Hitreact_Front，0.5s 节流播完回基础动画 */
	UFUNCTION()
	void HandleHitReact(UHealthComponent* HealthComp, AActor* InstigatorActor);

	/** 基础动画回切：护盾期=Idle 待机 / 破盾眩晕期=Stun_Loop 循环——"能打了"的形体语言 */
	void PlayBaseAnim();

	/** 瞬态动画（Cast/Hitreact 播完回基础动画；死亡后自愈不回切） */
	void PlayTransientAnim(UAnimationAsset* Anim);

	/** 召唤杂兵存活轮询（全灭=破盾） */
	TArray<TWeakObjectPtr<AEnemyGrunt>> Minions;

	/** 轮次 0-2：护盾→破盾眩晕→再召唤 */
	int32 CycleIndex = 0;
	bool bShieldUp = true;

	/** 单发弹幕：保留当前频率/弹速，眩晕均减半；旧实例的三发字段不再控制发射数。 */
	float OrbCooldown = .6f;
	TArray<TWeakObjectPtr<class ABossOrb>> ActiveOrbs;
	int32 OrbVolleysFired = 0;
	int32 OrbsSpawned = 0;
	UPROPERTY(EditAnywhere, Category = "Boss|Summoner", meta = (ClampMin = "0.15"))
	float OrbInterval = .6f;
	UPROPERTY(EditAnywhere, Category = "Boss|Summoner", meta = (ClampMin = "100.0"))
	float OrbSpeed = 1600.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Summoner", meta = (ClampMin = "3", ClampMax = "48"))
	int32 MaxActiveOrbs = 24;
	UPROPERTY(EditAnywhere, Category = "Boss|Summoner", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float StunnedFireRateScale = .5f;

	/** 护盾视觉罩脉冲节拍（0.25s 循环重画紫色细线球） */
	float ShieldPulseSeconds = 0.f;

	/** Sevarog 自带动画（09-30 换装：绕开非 Manny 骨骼重定向红线，单节点直播）：
	 *  Idle 待机 / Cast 施法 / Stun_Loop 破盾眩晕循环 / Hitreact_Front 受击 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> CastAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> StunLoopAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> HitReactAnim;

	/** 瞬态动画回切定时器（Cast/Hitreact 共用单柄——互顶时 ClearTimer 防旧回切截断新动画） */
	FTimerHandle TransientAnimTimer;
	float HitReactCooldown = 0.f;

	/** 受击动作单次伤害门槛（09-30 用户裁决"删常态受击动作，留阈值入口"）：默认 99999=禁用——
	 *  无暴击体系前单发伤害远达不到；暴击落地后调低即成"大伤才踉跄"的分级表现 */
	UPROPERTY(EditAnywhere, Category = "Boss|Summoner", meta = (ClampMin = "0.0"))
	float HitReactDamageThreshold = 99999.f;
};
