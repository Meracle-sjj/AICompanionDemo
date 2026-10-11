/**
 * [INPUT]: 依赖 Engine 的 UActorComponent 与 Actor 伤害链路（PointDamage/AnyDamage 均可接入）
 * [OUTPUT]: 对外提供 UHealthComponent——通用生命值组件：MaxHealth/CurrentHealth（EditAnywhere）、ApplyDamage(含 Instigator)、Heal(治疗唯一入口，死者无效)、SetMaxHealth(出生定型充满/升级扩容钳制)、SetInvulnerable(E5 圣盾无敌，伤害全额早退定时自恢复)、OnDamaged/OnHealthChanged/OnDeath/**OnHealApplied（10-07 治疗意图事件：满血治疗也广播，Attempted/Actual 双参——"满血也计数"类消费方订阅此口而非血量事件）**动态多播委托；**LastDamageTaken/GetLastDamageTaken（09-30 受击阈值入口数据源：每次 ApplyDamage 钳制后的实际扣血快照——OnDamaged 委托不带伤害量，受击分级表现订阅方读此口）**；挂载方自行决定受击表现与死亡处理
 * [POS]: Health/唯一生命账本；原Blueprint ApplyDamage保留泛用语义，原生ApplyCombatDamage接业务类别/落点/猎印层数，实际扣血后一次向既有UI层提交反馈，现役队友未挂载Health。
 *  Q模式被动在实现的唯一入口结算：护卫增强玩家治疗，歼灭增强实际队友来源伤害，默认均10%；不在调用点重复增强。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatDamageFeedback.h"
#include "HealthComponent.generated.h"

/** 血量变化事件（含死亡）：UI 血条、受击变色、死亡清理统一从此订阅 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthChanged, UHealthComponent*, HealthComp, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, UHealthComponent*, HealthComp);
/** 受击事件（带凶手）：队友"伤害通道"的信号源——伙伴挨打即知攻击者，补视锥盲区（感知-决策断层修复） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamaged, UHealthComponent*, HealthComp, AActor*, Instigator);
/** 治疗意图事件（10-07 回响脉冲重做）：Attempted=施放的治疗量、Actual=钳制后实际回血量——
 *  满血时 Actual=0 但 Attempted 照传。OnHealthChanged 在满血时静默（无变化的血量事件是噪声），
 *  "满血治疗也要计数"这类消费方必须订阅本事件而非血量事件 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealApplied, UHealthComponent*, HealthComp, float, Attempted, float, Actual);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class AICOMPANIONDEMO_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void BeginPlay() override;

	/** 伤害唯一入口：钳制到 [0, CurrentHealth]，广播 OnDamaged/OnHealthChanged；归零时广播 OnDeath（仅一次）。Instigator = 施伤者（可空） */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ApplyDamage(float Damage, AActor* Instigator = nullptr);

	/** C++语义入口；与Blueprint泛用入口共用原生命结算，反馈只使用本次实际扣血快照。 */
	void ApplyCombatDamage(float Damage, AActor* Instigator, const FCombatDamageContext& Context);

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	/** 最近一次实际扣血量（钳制后正数，09-30 受击阈值入口）：受击表现类订阅方据此做
	 *  "伤害够大才播受击"分级——暴击体系落地后即为此口的原生数据源 */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetLastDamageTaken() const { return LastDamageTaken; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return CurrentHealth <= 0.f; }

	/** 出生/升级期的血量上限配置：bFillCurrent=true 当前血量同步充满（变体出生定型）；false 仅调上限并钳制现值（升级扩容不回血，C1 预留） */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetMaxHealth(float NewMax, bool bFillCurrent = false);

	/** 治疗唯一入口：钳制到 MaxHealth，广播 OnHealthChanged（Delta>0）；对死者无效（蛋糕不救尸，与 ApplyDamage 的 bDead 早退对称） */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float Amount);

	/** E5 圣盾无敌：Duration 秒内 ApplyDamage 全额早退，定时自恢复（EnemyGrunt::ApplySlow 同范式，重复调用刷新） */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetInvulnerable(float Duration);

	/** 主动提前解除无敌（09-30 召唤师破盾：护盾免伤迁入无敌通道后，破盾即清） */
	void ClearInvulnerable();

	/** 死亡链保险丝（幂等）：血量归零而 OnDeath 未走时强制引爆标准死亡链——Boss 侧"0 血站桩"防御纵深 */
	void ConfirmDeath();

	/** 死亡是否已确认（保险丝判据：归零而未确认=竞态漏网） */
	bool IsDeathConfirmed() const { return bDead; }

	/** 圣盾是否生效（表现层可读：染色/状态行） */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsInvulnerable() const { return bInvulnerable; }

	/** 圣盾剩余秒数（09-28 圣盾特效用：快到期闪烁提示）；未激活返回 0 */
	float GetInvulnerableRemaining() const
	{
		return bInvulnerable && GetWorld() ? FMath::Max(GetWorld()->GetTimerManager().GetTimerRemaining(InvulnTimerHandle), 0.f) : 0.f;
	}

	/** 受击广播（带施伤者）：早于 OnHealthChanged——队友的威胁候选注入订阅此通道 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDamaged OnDamaged;

	/** 血量变化（NewHealth/Delta；Delta<0 为受伤） */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChanged OnHealthChanged;

	/** 治疗意图（10-07 回响脉冲重做：满血治疗也广播——Attempted=施放量，Actual=钳制后实际回血量） */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealApplied OnHealApplied;

	/** 死亡广播（仅一次，重复伤害不重复触发） */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeath OnDeath;

private:
	/** 最大生命值：EditAnywhere 让每个挂载方在 Details 面板自定义血量 */
	UPROPERTY(EditAnywhere, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(VisibleInstanceOnly, Category = "Health")
	float CurrentHealth = 0.f;

	/** 最近一次实际扣血量快照（ApplyDamage 钳制后写入；09-30 受击阈值入口的数据源） */
	float LastDamageTaken = 0.f;

	bool bDead = false;

	/** E5 圣盾无敌态：ApplyDamage 早退闸门（真源），SetInvulnerable 定时自恢复 */
	bool bInvulnerable = false;

	/** 圣盾到期定时器（重复触发刷新） */
	FTimerHandle InvulnTimerHandle;

	/** 圣盾到期恢复 */
	void ExpireInvulnerability();
};
