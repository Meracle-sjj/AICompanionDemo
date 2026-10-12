// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 Health/HealthComponent（玩家血量——敌人攻击与评分器"玩家安全"维度的数据基础）、Enhanced Input（移动/视角/跳跃）、SpringArm/Camera（第三人称相机装配）
 * [OUTPUT]: 对外提供 AAICompanionDemoCharacter 玩家抽象基类（UCLASS(abstract)，BP 子类现役）：相机装配 + Health 挂载 + 玩家侧能力全集——C1 闪烁（Shift 传送语义）、C 键紧急呼叫（信标拾取解锁）、持钥状态（锁箱开锁资格）、Lv.6 自动射击（**被动强化**：自动锁定最近活敌节拍开火，玩家只管走位）、**回响脉冲 2.0（10-07 蔓延领域重做：HandleHealApplied 订阅 OnHealApplied 治疗意图——辉耀结界(bHealNovaCountWhenFull)持有者满血治疗也计数；FireHealNova 启动 2s 蔓延态，绿球自玩家脚下展开至 7m、过程每0.1s对当时球内敌人1伤（10DPS不变）、卡帧按历史半径补拍，短命绿飘字/命中闪光；终点爆25伤+双层爆闪+冲击环+瞬时绿光+蠕虫撞地同款Explosion音——TickHealNova/NovaDamageRing/FinishHealNova状态机局部实现在AICompanionDemoCharacterHealNova.cpp）**、治疗反馈光效（绿飘字+脉冲圈+屏幕绿闪，0.6s 节流）与濒死恢复台词（<30% 挂旗回升穿 70% 喊话）、受击反馈 HandleHurt（镜头震动自持 + DamageFlash 脉冲）、闪现轰击融合回调、HUD 技能栏读数口（六件只读）
 *  同批契约：默认BossRelicComponent、主攻/真实受击/成功闪烁事件；闪烁5s、召回20s，首次信标与成功召回各自必须2D语音；战斗实现拆至CharacterCombat。
 *  猎印命中按ICombatant叠层，覆盖EnemyGrunt和BossBase家族；冰/电残留仍按EnemyGrunt契约处理。寒霜参数与实际施加者分开：本体普通弹路才授传播源，本体Nova伤害/附冰也归本体但不授源。ApplyElementPayload不做反向融化，回传本次主伤害语义给Health。
 *  Boss远景/震屏实现在CharacterWurmCamera：捕获实际弹簧臂基线，任意活Boss期间平滑1.6倍，虫0.55s强震/巨像较小短震；保留碰撞/控制旋转，暂停清偏移、死亡/Ending/EndPlay复原。
 * [POS]: 模块根的玩家载体，与 AI/CompanionCharacter（队友）平行、无继承关系；GameMode 生成玩家，敌人伤害经其 HealthComponent 进全项目唯一伤害口
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "AICompanionDemoCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UHealthComponent;
class UPointLightComponent;
class AEnemyGrunt;
enum class ECombatDamageKind : uint8;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AAICompanionDemoCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** 玩家血量（M3）：与敌人/队友同款 HealthComponent——"玩家安全"评分维度与敌人攻击的数据基础 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UHealthComponent* Health;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	AAICompanionDemoCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void SetWurmEncounterActive(bool bActive);
	/** 世界活Boss共用远景；Leaving用于死亡/EndPlay同帧排除尚在迭代器内的本体。 */
	void RefreshBossEncounterCamera(const AActor* Leaving = nullptr);
	void PulseWurmCameraShake(float Duration = .55f);
	void PulseBossCameraShake(float Duration, float XYAmplitude, float ZAmplitude);
	/** 终局宿主须在暂停前调用，死亡与EndPlay也复用此清理口。 */
	void ResetWurmEncounterCamera();
	UFUNCTION(BlueprintPure, Category = "Debug|Wurm")
	FString DebugWurmCameraState() const;

protected:
	void InitializeWurmCamera();
	void TickWurmCamera(float DeltaSeconds);
	bool bWurmCameraInitialized = false;
	bool bWurmEncounterActive = false;
	bool bBossEncounterActive = false;
	int32 BossCameraAliveCount = 0;
	float NormalCameraArmLength = 0.f;
	FVector NormalCameraTargetOffset = FVector::ZeroVector;
	float WurmCameraShakeTime = 0.f;
	float WurmCameraShakeDuration = .55f;
	float BossCameraShakeXY = 0.f;
	float BossCameraShakeZ = 0.f;
	int32 WurmCameraShakePulses = 0;
	UPROPERTY(EditAnywhere, Category = "Camera|Wurm", meta = (ClampMin = "1.0", ClampMax = "2.5"))
	float WurmCameraDistanceScale = 1.6f;
	UPROPERTY(EditAnywhere, Category = "Camera|Wurm", meta = (ClampMin = "0.1"))
	float WurmCameraBlendSpeed = 3.f;
	UPROPERTY(EditAnywhere, Category = "Camera|Wurm", meta = (ClampMin = "0.0", ClampMax = "150.0"))
	float WurmCameraShakeXY = 80.f;
	UPROPERTY(EditAnywhere, Category = "Camera|Wurm", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float WurmCameraShakeZ = 46.f;

	/** 死亡序列（09-21 G2）：OnDeath 订阅入口——禁操作 + 布娃娃倒地 + 经组合根弹死亡屏 */
	virtual void BeginPlay() override;

	/** OnDeath 回调（动态委托签名）：玩家死亡的唯一处理点——此前死亡静默如实验品 */
	UFUNCTION()
	void HandleDeath(UHealthComponent* HealthComp);

	/** OnDamaged 回调（09-22 G4 受击反馈）：镜头震动计时 + 红闪经组合根脉冲 */
	UFUNCTION()
	void HandleHurt(UHealthComponent* HealthComp, AActor* InstigatorActor);

	/** 血量变化订阅（受伤累计警报 + 濒死恢复边沿 + 治疗反馈光效）：Delta>0 治疗流入 / <0 受伤累计 */
	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* HealthComp, float NewHealth, float Delta);

	/** 治疗意图订阅（10-07 回响脉冲重做）：回响累计器改挂此口——辉耀结界持有者满血治疗也计数 */
	UFUNCTION()
	void HandleHealApplied(UHealthComponent* HealthComp, float Attempted, float Actual);

	/** 回响脉冲启动：进入 2s 蔓延态（绿球自玩家脚下缓慢展开），终点引爆结算——爆发改状态机 */
	void FireHealNova();

	/** 蔓延态推进（Tick 调用）：球径扩张 + 过程伤害节拍 + 满径终点爆 */
	void TickHealNova(float DeltaSeconds);

	/** 按指定历史球径结算活敌伤害，返回实际扣血的敌数；FeedbackLifetime=0只结算，卡帧补拍不叠浮字 */
	int32 NovaDamageRing(float Damage, float Radius, float FeedbackLifetime = 0.6f);

	/** 终点爆（蔓延满径触发）：满径伤害 + 双层绿金爆闪 + 贴地冲击环 + 瞬时绿光 + 蠕虫撞地同款Explosion音 */
	void FinishHealNova();

	/** 蔓延态清理（死亡/终点共用）：光组件销毁 + 状态复位 */
	void ClearHealNovaState();

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	// ---------- C1 技能卡：闪烁 ----------

	/** 闪烁解锁旗（UpgradeDirector 抽到技能卡时置 true；卡前常闭零开销） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Blink")
	bool bBlinkUnlocked = false;

	/** 紧急呼叫解锁旗（09-25 拾取信标解锁；C 键召回队友——移速复利流的保险丝） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Blink")
	bool bRecallUnlocked = false;

	/** 玩家射击解锁旗（09-25 等级成长：Lv.6 解锁左键射击——跑位期的自保与补刀） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Blink")
	bool bPlayerShootingUnlocked = false;

	/** 持钥状态（09-25 寻宝：钥匙拾取解锁锁箱开箱资格） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Blink")
	bool bHasTreasureKey = false;

	/** 玩家射击伤害（约为队友基础的一半——自保与补刀定位，火力成长仍归队友） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "1.0"))
	float PlayerShotDamage = 8.f;

	/** 玩家射击间隔（秒） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "0.1"))
	float PlayerShotInterval = 0.5f;

	/** 玩家射击射程（厘米） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "500.0"))
	float PlayerShotRange = 2500.f;

	// ---------- 元素弹头（09-29 元素反应体系，gameplay-proposals 十二节：玩家=状态制造器） ----------

	/** 寒霜弹·本体版：真实弹命中挂本体冰与近距传播源；冰只被队友火触发独立融化。 */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Element")
	bool bFrostShotUnlocked = false;

	/** 感电弹解锁旗：命中刷新5秒感电；队友每半秒配对两目标，每端基础10点或攻击15%，玩家额外枪口散射 */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Element")
	bool bShockShotUnlocked = false;

	/** 猎杀印记解锁旗：命中叠印记（金色 ◆ 可读）——AI 弹命中消耗全部层=处决（15/层） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Element")
	bool bHuntMarkUnlocked = false;

	/** 印记层数上限：基础 3，"印记强化"卡每张 +1（文档 B"印记强化（叠 4 层）"的成长线） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Element", meta = (ClampMin = "1", ClampMax = "6"))
	int32 HuntMarkMaxStacks = 3;

	/** 玩家冰霜减速参数（FrostDeep 深化 1.5s/0.7 → 4s/0.4） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Element", meta = (ClampMin = "0.5"))
	float FrostSlowSeconds = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Upgrade|Element", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float FrostSlowFactor = 0.7f;

	/** 玩家弹道与AI独立：穿透+折射可同持，每个贯穿单位单跳、整发去重；散射仍与两种互斥 */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|PlayerFire")
	bool bPlayerScatter = false;
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|PlayerFire")
	bool bPlayerPierce = false;
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|PlayerFire")
	bool bPlayerPrism = false;
	/** 玩家折射衰减：命中后弹向 8m 内最近敌（70% 伤害，单跳） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|PlayerFire", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PlayerPrismScale = 0.7f;

	/** 冰霜/感电/印记统一挂载；施加者默认本体，本体Nova明确传自身且不授传播资格；冰弹原伤害不被灼烧放大。 */
	float ApplyElementPayload(AActor* VictimActor, float BaseDamage, FColor& InOutShotColor, const FVector& FxLocation,
		ECombatDamageKind* OutDamageKind = nullptr, bool bCanSpreadFrost = false, AActor* ElementInstigator = nullptr);

	/** 召回冷却（秒）：防无脑瞬移的节拍阀 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "5.0"))
	float RecallCooldownSeconds = 20.f;

	/** 融合·闪现轰击解锁旗（闪烁+散射双前置的融合卡） */
	UPROPERTY(VisibleAnywhere, Category = "Upgrade|Blink")
	bool bBlinkBombUnlocked = false;

	/** 闪现距离（厘米）：09-21 三审——3m 在 5m/s 移速下视觉等同半步快跑（用户实测"类似加速而非传送"），
	 *  8m 才读得出"A 消失 B 出现"的传送质变 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "100.0"))
	float DashDistance = 800.f;

	/** 闪现冷却（秒） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Blink", meta = (ClampMin = "0.1"))
	float DashCooldownSeconds = 5.f;

	/** 闪现：朝移动方向（静止时朝相机朝向）sweep 瞬移——被墙挡则瞬到墙边（不穿墙） */
	UFUNCTION(BlueprintCallable, Category = "Upgrade|Blink")
	void PerformDash();

	/** 信标拾取：首次解锁C，重复总/剩余CD减5秒、总CD下限5秒 */
	UFUNCTION(BlueprintCallable, Category = "Abilities|Recall")
	void UnlockRecall();

	/** C 键召回：队友探地传送至玩家 3m 环点（09-25 深度扩展 A） */
	UFUNCTION(BlueprintCallable, Category = "Abilities|Recall")
	void RecallCompanion();

	/** 玩家射击（Lv.6 解锁，**自动强化**——09-26 夜轮用户定调"和 AI 队友一样自动开火，不是让玩家发起攻击指令"）：Target 有效朝目标打，无目标不开火；Tick 节拍自动选最近活敌 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void PlayerFire(AActor* Target = nullptr);

	/** 钥匙授予（KeyPickup 拾取侧调用）：持钥后可开带锁大箱；09-28 起带队友播报语音+HUD 钥槽点亮 */
	void GiveTreasureKey();

	/** 持钥查询（LockedChest 开锁资格判定） */
	UFUNCTION(BlueprintPure, Category = "Upgrade|Blink")
	bool HasTreasureKey() const { return bHasTreasureKey; }

	/** HUD 技能栏读数口（CompanionHud 轮询消费）：解锁态 + 剩余/总冷却——只读，倒计时推进仍在 Tick */
	bool IsBlinkUnlocked() const { return bBlinkUnlocked; }
	bool IsRecallUnlocked() const { return bRecallUnlocked; }
	float GetDashCooldownRemaining() const { return DashCooldownCountdown; }
	float GetDashCooldownTotal() const { return DashCooldownSeconds; }
	float GetRecallCooldownRemaining() const { return RecallCooldownCountdown; }
	float GetRecallCooldownTotal() const { return RecallCooldownSeconds; }

	// ---------- 回响脉冲（09-26 夜轮用户提案：回复也是武器；10-07 蔓延领域重做） ----------

	/** 解锁旗：抽"回响脉冲"卡后，累计治疗满阈值自动爆发 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond")
	bool bHealNovaUnlocked = false;

	/** 辉耀结界协同（10-07 用户裁决"计数不常态生效"）：持有融合卡·辉耀结界后，满血治疗也计入累计——
	 *  挂 OnHealApplied 意图事件后按此旗分流（Attempted/Actual），未持卡维持"真实回血才计数" */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond")
	bool bHealNovaCountWhenFull = false;

	/** 触发阈值（累计治疗量）：每次爆发清零重新累计 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "5.0"))
	float HealNovaThreshold = 40.f;

	/** 终点爆单敌伤害（蔓延过程另计 HealNovaDotDamage——叠加卡"伤害+10"作用于此口） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "1.0"))
	float HealNovaDamage = 25.f;

	/** 蔓延终点半径（厘米，10-07 用户裁决体积×2：350→700） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "100.0"))
	float HealNovaMaxRadius = 700.f;

	/** 蔓延时长（秒）：0→满径的展开速度（10-07 用户裁决"缓慢展开"——原瞬间爆发改 2s 领域） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "0.2"))
	float HealNovaExpandTime = 2.0f;

	/** 蔓延过程伤害（球内敌人每拍）：默认1伤/0.1s，保持旧5伤/0.5s的10DPS */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "1.0"))
	float HealNovaDotDamage = 1.f;

	/** 蔓延过程伤害节拍（秒/拍）：0.1s密集跳伤，补拍用对应历史半径；每拍短命浮字/命中闪光 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "0.1"))
	float HealNovaDotInterval = 0.1f;

protected:

	/** 闪现冷却倒计时（秒） */
	float DashCooldownCountdown = 0.f;

	/** Shift 上帧状态：边沿检测（D9 Q 键同款零资产范式） */
	bool bPrevShiftDown = false;

	/** E 键上帧状态：宝箱交互边沿检测（M3.75 E3，同款范式） */
	bool bPrevEDown = false;

	/** C 键上帧状态：紧急呼叫边沿检测（09-25，同款范式） */
	bool bPrevCDown = false;

	// ---------- 受击反馈（09-22 G4） ----------

	/** 镜头震动剩余时长（秒）：HandleHurt 置 0.3，Tick 内衰减驱动弹簧臂随机偏移 */
	float CameraShakeTime = 0.f;

	/** 召回冷却倒计时（秒）：RecallCompanion 置满值，Tick 内推进 */
	float RecallCooldownCountdown = 0.f;

	/** 玩家射击冷却倒计时（秒）：Lv.6 解锁后 Tick 内按 PlayerShotInterval 节拍 */
	float PlayerShotCooldown = 0.f;

	/** 回响脉冲治疗累计器：HandleHealApplied 累加、爆发清零 */
	float AccumHealSinceNova = 0.f;

	// ---------- 回响脉冲蔓延态（10-07 领域重做） ----------

	/** 蔓延进行中：FireHealNova 置位、满径终点爆/死亡清理复位——活跃期忽略新触发 */
	bool bNovaActive = false;

	/** 终点结算重入闸门：同步伤害/治疗回调期间仍保持活跃，清理完成前拒绝新领域 */
	bool bNovaFinishing = false;

	/** 蔓延已进行秒数：驱动球径插值与终点判定 */
	float NovaElapsed = 0.f;

	/** 过程伤害节拍倒计时（秒）：首拍从完整Interval开始，余数跨帧保留；只推进领域2s内时间 */
	float NovaDotCountdown = 0.f;

	/** 最近一次领域实际命中短脉冲（秒）：只驱动已有跟随光，不生成额外光组件 */
	float NovaHitPulseTime = 0.f;

	/** 蔓延期跟随光（NewObject 瞬态组件，TWeakObjectPtr 防悬空——裸指针埋雷案教训）：强度随球径上升 */
	TWeakObjectPtr<UPointLightComponent> NovaLightRef;

	/** 濒死恢复边沿旗：血量 <30% 置位，回升穿 70% 喊台词并清位（09-27 配音轮） */
	bool bNearDeathFlag = false;

	/** 受伤累计警报（09-28 用户裁决"累计一定时间掉多少血再触发音效"；09-30 复测定调"两秒内掉 20 再报+响后要消停"）：
	 *  2s 窗累计 ≥20 响一次，响后 6s 静默冷却（围殴场景最多 6 秒一响） */
	float RecentDamageAccum = 0.f;
	double DamageWindowStart = -100.0;
	double HurtAlarmReadyTime = 0.0;

	/** 治疗光效节流戳（真实时钟）：持续治疗源 0.5s 一拍，0.6s 内合并为一次可读脉冲 */
	double LastHealFxTime = -10.0;

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** 外部脉冲镜头震动（10-06 蠕虫落点爆接入——G4 震动机制原本只有 HandleHurt 一条触发线；
	 *  爆炸类事件的体感王者：大爆闪+震屏让"爆炸发生了"无可置疑）。取 Max 不叠加防连续爆炸抽搐 */
	void PulseCameraShake(float Duration = 0.3f) { CameraShakeTime = FMath::Max(CameraShakeTime, Duration); }
};

