/**
 * [INPUT]: Engine的ACharacter/CharacterMovementComponent/WidgetComponent（保留定位锚点）/死亡动画、Health唯一伤害口、GameMode Boss击杀上报通道
 * [OUTPUT]: 对外提供 ABossBase——Boss 基类骨架（波 1，09-30 用户立项"三 Boss 终局"）：
 *   大体型载体+HealthComponent+屏幕HUD血条身份只读口+ 近战占位行为（贴脸节拍 ApplyDamage+直指追击）；
 *   BossIndex 标识三 Boss 身份（3min 召唤师/6min 巨像/9min 蠕虫的占位实现波 2-4 各自填充特色行为）；
 *   死亡经 HandleDeath 上报 WaveDirector 计数+死亡表现（DeathPresentationAnim 优先，ragdoll 兜底——站尸 3s 观感案根治）
 *   共享猎杀印记状态与世界数字徽记：ICombatant玩家叠层/队友消费，1–2金黄、3+红；无敌不叠，死亡隐藏但保留同击消费顺序
 *   BeginPlay/死亡/EndPlay按世界活Boss刷新玩家共享1.6倍远景，多家族宿主不会提前恢复。
 *   真实子类入场与幂等死亡向统一语音队列发专属事件；队列拥有字幕与播放时序。
 * [POS]: AI/ 的 Boss 家族基类（波 1 骨架）：召唤师/裂地巨像/沙狱蠕虫三 Boss 的公共载体——特色行为（召唤/地刺/钻地）由子类或波次填充
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AI/Combatant.h"
#include "BossBase.generated.h"

class UHealthComponent;
class UWidgetComponent;
class UAnimationAsset;
class UHuntMarkVisual;

UCLASS()
class AICOMPANIONDEMO_API ABossBase : public ACharacter, public ICombatant
{
	GENERATED_BODY()

public:
	ABossBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---------- ICombatant：队友评分器的识别契约（Boss 可被选中/集火） ----------
	virtual bool IsDead() const override;
	virtual bool IsEngagingPlayer() const override { return true; }   // Boss 恒为威胁源（仇恨拉满——压迫力语义）
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	virtual void AddHuntMark(int32 MaxStacks) override;
	virtual int32 GetHuntMarks() const override { return MarkStacks; }
	virtual int32 ConsumeHuntMarks() override;
	UFUNCTION(BlueprintPure, Category = "Debug|HuntMark")
	FString DebugHuntMarkState() const;

	/** 身份注入（BossAltar 激活时调用）：Boss 名（血条显示）与序号（1-3，战绩用） */
	void SetBossIdentity(const FString& InName, int32 InIndex);
	int32 GetBossIndex() const { return BossIndex; }
	const FString& GetBossName() const { return BossName; }

protected:
	/** 死亡回调（动态委托签名）：UFUNCTION 必须显式——AddDynamic 运行时 FindUFunction，
	 *  缺宏=绑定静默失败=OnDeath 广播无人接收（十九轮波 1 埋雷，0 血站桩全案终审真凶：
	 *  血条不隐藏/动画不播/SetLifeSpan 不设/保险丝引爆后依旧无人响应） */
	UFUNCTION()
	virtual void HandleDeath(UHealthComponent* HealthComp);

	/** 死亡表现动画（子类换装时注入，如 Sevarog 的 Death_front）：空则 HandleDeath 走 ragdoll 兜底 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> DeathPresentationAnim;

	/** 死亡表现虚函数（HandleDeath 的表现段抽口）：默认=死亡动画/ragdoll 兜底；
	 *  蠕虫覆写演"节链失引坠落"——程序化节链无骨骼，ragdoll 不适用 */
	virtual void PerformDeathPresentation();

	/** 高空探地（+80m 向下探 240m，v15 全地形覆盖——BossAltar/钥匙落位同款范式）：
	 *  Boss 家族地形锚定公用。BossColossus 另有私有副本（四十四轮，机制冻结未收编）——
	 *  家族第四次用到探地时统一收编，届时删副本 */
	void ResolveGroundZ(FVector& Loc);

	/** 原头顶血条组件保留为子类/猎印定位锚点；不再装载widget或可见血条，显示归CompanionHud。 */
	UPROPERTY(VisibleAnywhere, Category = "Boss")
	TObjectPtr<class UWidgetComponent> HealthBarComp;

	UPROPERTY(VisibleAnywhere, Category = "Boss")
	TObjectPtr<class UHealthComponent> HealthComponent;

	/** 三Boss继承同一猎印显示；不参与碰撞、导航或伤害。 */
	UPROPERTY(VisibleAnywhere, Category = "Boss|HuntMark")
	TObjectPtr<UHuntMarkVisual> HuntMarkVisual;
	int32 MarkStacks = 0;

	/** 近战伤害/间隔/距离（占位行为参数——波 2-4 特色攻击轮换替换） */
	UPROPERTY(EditAnywhere, Category = "Boss|Combat", meta = (ClampMin = "1.0"))
	float MeleeDamage = 18.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Combat", meta = (ClampMin = "0.5"))
	float MeleeInterval = 2.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Combat", meta = (ClampMin = "100.0"))
	float MeleeRange = 220.f;
	float MeleeCooldown = 0.f;

	/** 站桩闸（子类行为期置位：召唤师读条/蠕虫钻地等——挂起基类追击与近战） */
	UPROPERTY(EditAnywhere, Category = "Boss|Combat")
	bool bHoldPosition = false;

	FString BossName = TEXT("BOSS");
	int32 BossIndex = 1;
	bool bDeathHandled = false;
};
