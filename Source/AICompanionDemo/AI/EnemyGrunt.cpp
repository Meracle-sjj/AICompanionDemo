/**
 * [INPUT]: EnemyGrunt组件/追击契约，Health的实际伤害与类别落点接口，角色移动/动画/定时器和死亡掉落；Audio/SFXLibrary普通死亡声与携带来源的妖术师空间短声入口。
 * [OUTPUT]: 对外提供 AEnemyGrunt 的实现——重力行走逼近（**追击全自驱寻路**：RefreshChasePath 每 0.5s FindPathToActorSynchronously 算路存路点串 + 沿"路径下一路点"喂输入——加速度与绕障同源，滑步案（PFC 不写加速度，vel=200/accel=0 探针实证）与双驱动打架案（直冲输入顶掉绕障路径=卡斜坡）双根治；无路径直指玩家兜底 + 失败回退窗）+ 贴脸挥拳节拍（动画与伤害同拍，伤害携带 Instigator=自己——玩家受击广播即队友的伤害通道信号）+ ragdoll 布娃娃死亡（切 Ragdoll 碰撞预设 QueryAndPhysics + 全骨骼模拟——mesh 默认 QueryOnly 只模拟不碰撞即"沉地"案根因）+ ApplyVariant 变体定型（三变体参数集：移速/血量/伤害/间隔/体型/"Paint Tint" 色调微差/经验值/蛋糕掉率，逐分支显式赋值保幂等；Standard=M3.5 基线零漂移）+ 死亡掉落 XpPickup（喂 XpValue）与 HealthPickup 柠檬蛋糕（HealDropChance 概率，蛮兵必掉）与 TemporaryPickup E5 暂态道具（RushDropChance 概率，种类随机定型）+ ICombatant 三方法 + **妖术师蓄力地面预警圈（09-29 视觉区分度案：红线战场与穿透红撞车定罪——MOBA 语言"脚下红圈=要挨打"，圈半径判宽+50，来源线降 1px 辅助；结算猩红+白芯与穿透红分家）**
 *  1.1仅Standard基础步速450→405cm/s，其他变体不变；ApplyVariant同步慢速恢复基线。猎印由UPROPERTY持有；狂暴双股贴身紫红火焰由EnrageVisual管理，召唤杂兵出生读导演；死亡先清火焰并报告普通怪事件。
 * [POS]: AI/ 的敌对方实现（球体 EnemyTarget 的继任者，WaveDirector 的生成目标）；球体时代两大物理补丁（SnapToGround 贴地、免 sweep）在 ACharacter 载体上整体作废——胶囊+重力天生正确，但两道 BP 隐式装配必须显式化（构造函数注释含三案根因）：① bRunPhysicsWithNoController（无 Controller 不模拟，悬浮冻结）；② 胶囊 42×96 + mesh 贴地(-90)/前向转正(-90°)（模板配在 BP 里，裸 C++ 缺失即浮空+恒侧身）；③ 胶囊 Visibility=Block（Pawn 预设默认 Ignore，队友 LOS 线穿透即永不开火）
 *  v1.3.2：妖术师两声进入每世界短声预算；光束、投掷和拳击仅提交伤害语义，实际漫画数字由Health统一生成。
 *  v1.3.3正式：ApplySlow/RecoverFromSlow仅管理移动，寒霜雪花和双来源残留由Elements局部实现。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "EnemyGrunt.h"

#include "AI/GoldPickup.h"
#include "AI/HealthPickup.h"
#include "AI/TemporaryPickup.h"
#include "AI/XpPickup.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "AI/WaveDirector.h"
#include "Landscape.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Audio/SFXLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Vfx/VfxLibrary.h"
#include "Vfx/HuntMarkVisual.h"
#include "Vfx/EnemyEnrageVisual.h"

#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 贴脸挥拳的层高容差（厘米）：|ΔZ| 超过它视为"不同层"——隔着地板/夹层绝不挥拳
	//（隐形敌人案 2026-09-10：环形刷怪落在玩家头顶的结构面上，Size2D 只看水平距离导致穿层围殴）
	constexpr float kMeleeHeightTolerance = 150.f;

	// LOS 探针的胸口抬升：与队友 HasLineOfSight 的眼位抬升同款思路
	constexpr float kChestOffset = 80.f;
}

bool AEnemyGrunt::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}

bool AEnemyGrunt::IsEngagingPlayer() const
{
	if (IsDead())
	{
		return false;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return false;
	}

	// 无状态即时判定（与 HuntPlayer 攻击分支同一几何条件）：贴脸范围内且玩家活着即"咬人中"
	const UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	return PlayerHealth && !PlayerHealth->IsDead()
		&& FVector(GetActorLocation() - PlayerPawn->GetActorLocation()).Size2D() <= PlayerEngageRange;
}

	AEnemyGrunt::AEnemyGrunt()
{
	// Tick 驱动猎手行为（逼近 + 攻击节拍）——数量级 ≤10 只，每帧开销可忽略
	PrimaryActorTick.bCanEverTick = true;

	// 载体人形装配：模板把这三项配在角色 BP 里，裸 C++ 类无 BP 兜底必须显式化——
	// ① 胶囊对齐玩家模板 42×96（敌人与玩家体型一致，互撞观感统一）；
	// ② mesh 原点(脚底)钉到胶囊底：ACharacter 默认相对位置 (0,0,0) 即胶囊中心，缺它整个人浮空半高 88cm（"悬浮"案根因）；
	// ③ Manny 家族资产建模面向 +Y，yaw -90 转正到胶囊 +X 前向，缺它则模型恒侧身（"朝向不随行进"案根因）
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// Pawn 碰撞预设对 Visibility 通道默认 Ignore（BaseEngine.ini 预设表原文 ECR_Ignore）——
	// 队友 HasLineOfSight 走 ECC_Visibility 且要求"命中物恰为目标"，穿透即永报"无视线"不开火（"护卫不攻击"案根因）；显式 Block 恢复可被视线命中
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 外观与动画内联装配（2026-09-07 用户要求"配置零人工"——模板字面约定是 BP 装配，此处取舍为 C++ 引用，
	// 取舍记录在 L2；资产与队友 BP_CompanionCharacter 的已验证组合完全同款，UPROPERTY 口子保留，BP 子类可覆盖）
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(MeshFinder.Object);
	}

	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimClassFinder(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
	if (AnimClassFinder.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimClassFinder.Class);
	}

	static ConstructorHelpers::FObjectFinder<UAnimationAsset> AttackAnimFinder(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));

	// 蓄力汇聚球×8（09-30 特效升级：程序化 Mesh 粒子——真实网格有光影遮挡，替代 Debug 线条手搓感；
	// 仅 Warlock 蓄力期显示，隐藏组件零渲染成本，其余变体永不激活）
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OrbMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (int32 i = 0; i < 8; ++i)
	{
		ChargeOrbs[i] = CreateDefaultSubobject<USphereComponent>(*FString::Printf(TEXT("ChargeOrb%d"), i));
		ChargeOrbs[i]->SetupAttachment(GetCapsuleComponent());
		ChargeOrbs[i]->InitSphereRadius(14.f);
		ChargeOrbs[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ChargeOrbs[i]->SetVisibility(false);
		if (OrbMatFinder.Succeeded())
		{
			ChargeOrbs[i]->SetMaterial(0, OrbMatFinder.Object);
		}
	}
	if (AttackAnimFinder.Succeeded())
	{
		AttackAnimation = AttackAnimFinder.Object;
	}

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	HuntMarkVisual = CreateDefaultSubobject<UHuntMarkVisual>(TEXT("HuntMarkVisual"));
	HuntMarkVisual->SetupAttachment(GetCapsuleComponent());
	HuntMarkVisual->SetRelativeLocation(FVector(0.f, 0.f, 170.f));
	EnrageVisual = CreateDefaultSubobject<UEnemyEnrageVisual>(TEXT("EnrageVisual"));

	// 视觉刺激源：让队友的 AIPerception（Sight）能"看见"——感知注册在敌人体内完成，队友侧零配置
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());

	// 猎手朝向：面朝移动方向（追玩家自然转身）；MaxWalkSpeed 即逼近速度真源（组件属性本就 EditAnywhere 可调，不再另立字段）
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->MaxWalkSpeed = 340.f;   // 09-22 移速重校：与 ApplyVariant 的标准兵基线一致

	// 无主模拟开关：EnemyGrunt 自驱 Tick（不加 AIController），而 CharacterMovement 默认对无 Controller 的
	// Character 直接中止模拟并清零速度（TickComponent 里 bShouldPerformControlledCharMove 三条件全假）——
	// 症状即"悬浮在生成点一动不动"；开此开关后引擎按 DefaultLandMovementMode 正常初始化，重力下落与行走复活
	GetCharacterMovement()->bRunPhysicsWithNoController = true;

	// 掉落物默认裸 C++ 晶体（B5）：置空即关闭掉落
	XpPickupClass = AXpPickup::StaticClass();

	// 回血蛋糕默认裸 C++ 柠檬蛋糕：置空即关闭掉落（概率见 HealDropChance，变体在 ApplyVariant 覆写）
	HealPickupClass = AHealthPickup::StaticClass();

	// 金币默认裸 C++ 类：置空即关闭掉落（宝箱经济的收入端，E3——概率/枚数见 GoldDropChance/GoldBurst，变体覆写）
	GoldPickupClass = AGoldPickup::StaticClass();

	// E5 暂态道具默认裸 C++ 三件套：置空即关闭掉落（概率见 RushDropChance，变体在 ApplyVariant 覆写）
	RushPickupClass = ATemporaryPickup::StaticClass();

	// 追击寻路（高台围殴案 2026-09-10 起，历 D12→D16→D17 三代）：现役为"无控制器 + FindPathToActorSynchronously
	// 同步算路 + 沿路点喂输入"——D12 曾挂 AIController 走 MoveToActor，但其 PFC"直接速度"管线不写加速度
	// （滑步，D16 探针定罪）；D16 的输入常开又与 PFC 双驱动打架（顶掉绕障路径，卡斜坡）。三代归一：
	// 寻路只管算路，移动 100% 走 D7 输入管线（加速度与绕障同源）。
	// bRunPhysicsWithNoController 是无控制器行走的主开关（M3.5 实证）
}

void AEnemyGrunt::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickElementResidue(DeltaSeconds);
	HuntPlayer(DeltaSeconds);

	// 滑行呆移探针（1Hz，仅移动时出声）：速度/加速度/动画模式/实例/控制器五件套一次报齐——
	// 速度有而加速度无 = 物理/传送在移动（ABP 判"静止"即滑步，D7 同族）；实例非 ABP = 动画系统被劫持
	if (bEnableMoveProbe)
	{
		MoveProbeCountdown -= DeltaSeconds;
		if (MoveProbeCountdown <= 0.f)
		{
			MoveProbeCountdown = 1.f;
			const float Speed = GetVelocity().Size();
			if (Speed > 20.f)
			{
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy][Anim] %s vel=%.0f accel=%.0f mode=%d inst=%s ctrl=%s pos=%s"),
					*GetNameSafe(this), Speed,
					GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration().Size() : -1.f,
					static_cast<int32>(GetMesh() ? GetMesh()->GetAnimationMode() : EAnimationMode::AnimationCustomMode),
					*GetNameSafe(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr),
					*GetNameSafe(Controller),
					*GetActorLocation().ToCompactString());
			}
		}
	}
}

// ---------- 元素标记（09-29 元素反应体系） ----------

void AEnemyGrunt::HuntPlayer(float DeltaSeconds)
{
	// 自身已死或玩家已倒下：猎手停手（死亡是绝对状态，不追尸不挨打）
	if (IsDead())
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	// 玩家血量组件经 FindComponentByClass 解耦获取：敌人不依赖玩家具体类（跨阵营只认 HealthComponent 契约）
	UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	if (!PlayerHealth || PlayerHealth->IsDead())
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();
	const float DistCm = ToPlayer.Size2D();

	// ---------- Warlock 蓄力射线（09-25 Boss 变体，用户点名"红色射线预警→实体伤害"） ----------
	if (GruntVariant == EGruntVariant::Warlock)
	{
		BeamCooldown -= DeltaSeconds;

		// 蓄力中：红色细线预警每帧重画 + 站桩不移动——射线在进入蓄力瞬间锁定玩家当时位置，走位即躲
		if (BeamChargeCountdown > 0.f)
		{
			BeamChargeCountdown -= DeltaSeconds;
			// 汇聚球插值（09-30 特效升级：程序化 Mesh 粒子——能量从四周向胸口收敛，蓄力进度可视化）
			UpdateChargeOrbs();
			{
			}
			// 地面预警圈（09-29 视觉改造主件）：MOBA 通用语言"脚下红圈=要挨打"——圈心=锁定落点，
			// 半径=判宽 70+50 余量=120，走出圈即躲；平铺圆（辉耀圈同款画法）
			DrawDebugCircle(GetWorld(), BeamTarget + FVector(0.f, 0.f, -30.f), 120.f, 32,
				FColor::Red, false, 0.12f, 0, 4.f,
				FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
			if (BeamChargeCountdown <= 0.f)
			{
				// 汇聚球谢幕（能量已注入射线）
				for (USphereComponent* Orb : ChargeOrbs)
				{
					if (Orb)
					{
						Orb->SetVisibility(false);
					}
				}
				if (ChargeVfx)
				{
					ChargeVfx->Destroy();
					ChargeVfx = nullptr;
				}
				// 结算：猩红粗束+白芯沿锁定线扫过——与队友穿透红（10px 纯红）形态分家
				DrawDebugLine(GetWorld(), BeamStart, BeamTarget, FColor::FromHex(TEXT("FF3C28")), false, 0.35f, 0, 14.f);
				DrawDebugLine(GetWorld(), BeamStart, BeamTarget, FColor::White, false, 0.35f, 0, 3.f);
				USFXLibrary::PlayWarlockSFX(GetWorld(), EGameSFX::WarlockBeamFire, this, BeamStart);
				const float DistToBeam = FMath::PointDistToSegment(PlayerPawn->GetActorLocation(), BeamStart, BeamTarget);
				if (DistToBeam <= BeamHitWidth && !PlayerHealth->IsDead())
				{
					PlayerHealth->ApplyCombatDamage(BeamDamage, this,
						{ECombatDamageKind::Beam, PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 160.f)});
				}
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] %s 射线结算（玩家距线 %.0fcm）"), *GetNameSafe(this), DistToBeam);
			}
			return;   // 施法站桩：不追击不近战
		}

		// 冷却好 + 中距（4-22m，09-28 近距下限 600→400：贴脸也可起手——妖术师威胁要见得到）：
		// 锁定玩家当前位置进入蓄力（红细线对玩家可见=预警本体）；近了贴脸挥拳、远了继续追
		// 09-28 二审定罪（首修 Landscape 掠过后用户复测"仍然没有任何远程意图"）：LOS 探线走
		// ECC_WorldStatic——Guard 模式队友永远贴着玩家（连线终端就在玩家身上，队友胶囊挡线概率
		// 极高）+ E4 场地 209 件散布障碍同为 WorldStatic Block，窗口内常年被切断=蓄力永不起手。
		// 预警线是瞄准线不是真实性检查——躲避语义在走位端（1.2s 红线预警，走出线段即白给），
		// 射线打在山上也是合理表现。LOS 门槛整段删除，窗口内必起手。
		if (BeamCooldown <= 0.f && DistCm >= 400.f && DistCm <= 2200.f)
		{
			BeamStart = GetActorLocation() + FVector(0.f, 0.f, 140.f * GetActorScale().Z);
			BeamTarget = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 40.f);
			BeamChargeCountdown = BeamChargeSeconds;
			BeamCooldown = BeamCooldownSeconds;
			ChargeStartTime = FPlatformTime::Seconds();
			for (USphereComponent* Orb : ChargeOrbs)
			{
				if (Orb)
				{
					Orb->SetVisibility(true);
				}
			}
			// 蓄力电弧已撤（09-30 用户裁决"妖术师省掉闪电"——视觉只留红圈+结算粗束；ChargeVfx 结算销毁段保留对空指针无害）
			USFXLibrary::PlayWarlockSFX(GetWorld(), EGameSFX::WarlockBeamCharge, this, BeamStart);
		}
		// 冷却中/超程：落到正常追击逻辑（下方的近战/游走段）——妖术师也会逼近
	}

	if (DistCm > PlayerEngageRange)
	{
		// 追击（D7 药方完全体，双根治）：RefreshChasePath 每 0.5s 同步算一次路（NavMesh 绕障/上斜坡/跨层），
		// 移动 100% 由输入管线沿路径下一路点喂出——加速度信号与移动驱动同源：
		// 动画有跑步姿势（加速度非零，滑行呆移案）+ 路径绕障不被直冲输入顶掉（双驱动打架案，卡斜坡）
		RefreshChasePath(PlayerPawn, DeltaSeconds);

		FVector FeedDir;
		if (ChasePathPoints.IsValidIndex(ChasePathIndex))
		{
			// 跳过已抵达路点（2D 120cm，v15 坡地调优：80 在坡地锯齿路径上会绕点打转微卡，120 减少折返）
			while (ChasePathIndex < ChasePathPoints.Num() - 1
				&& FVector(ChasePathPoints[ChasePathIndex] - GetActorLocation()).Size2D() < 120.f)
			{
				++ChasePathIndex;
			}
			const FVector& Target = ChasePathPoints[ChasePathIndex];
			FeedDir = FVector(Target.X - GetActorLocation().X, Target.Y - GetActorLocation().Y, 0.f).GetSafeNormal();
		}
		else
		{
			// 无路径（寻路失败回退窗内/导航缺失）：直指玩家兜底（M3.5 同款直线追，弱但能追）
			FeedDir = FVector(ToPlayer.X, ToPlayer.Y, 0.f).GetSafeNormal();
		}
		AddMovementInput(FeedDir);

		// 追击中冷却照常累计（封顶在攻击阈值）：怒气在跑动路上攒满，贴脸即出手——
		// 此处清零会让第一拳平白迟滞一个 PlayerAttackInterval（"贴脸呆滞"案根因）
		PlayerAttackCooldown = FMath::Min(PlayerAttackCooldown + DeltaSeconds, PlayerAttackInterval);
		return;
	}

	// 出拳双闸门（隐形敌人案根治，2026-09-10）：Size2D 只看水平距离是穿层围殴的根因——
	// ① 层高闸门：|ΔZ| 超容差视为不同层（跳起 ≈±90cm 不受影响，平地行为零漂移）；
	// ② LOS 闸门：胸口连线被几何挡住不挥拳（ObjectType 只认 WorldStatic/WorldDynamic，
	//    Pawn 对象天然豁免——同伴围挤互不挡拳，与 SnapToGround"球消失"案同款 ObjectType 哲学）
	const bool bSameLayer = FMath::Abs(ToPlayer.Z) <= kMeleeHeightTolerance;
	if (!bSameLayer || !HasClearLineToPlayer(PlayerPawn))
	{
		PlayerAttackCooldown = FMath::Min(PlayerAttackCooldown + DeltaSeconds, PlayerAttackInterval);

		// 高处反制（用户 2026-09-13 提案，蛮兵+标准兵远程）。
		// 不可达判定 v3（柱顶案二次定罪：玩家上柱后 2D<160 停止刷新路径，缓存的旧路径永远"可达"）——
		// 弃用缓存路径，改**玩家支撑面实时探针**：玩家脚下地面比自己脚下高 120cm 以上 = 物理爬不上去 = 投掷。
		// 一发 ObjectType 射线直读真值，不依赖 NavMesh 数据与任何缓存
		const float MyFloorZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FHitResult PlayerFloorHit;
		const FVector FloorProbe = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		FCollisionObjectQueryParams FloorParams;
		FloorParams.AddObjectTypesToQuery(ECC_WorldStatic);
		const bool bHasPlayerFloor = GetWorld()->LineTraceSingleByObjectType(PlayerFloorHit, FloorProbe,
			FloorProbe - FVector(0.f, 0.f, 600.f), FloorParams);
		const float PlayerFloorZ = bHasPlayerFloor
			? PlayerFloorHit.ImpactPoint.Z
			: PlayerPawn->GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float FloorDeltaZ = PlayerFloorZ - MyFloorZ;

		RangedAttackCooldown += DeltaSeconds;
		if (bRangedCapable && FloorDeltaZ > RangedEngageDeltaZ && RangedAttackCooldown >= RangedAttackInterval)
		{
			// 投掷视线宽松版：从头顶连线只认 WorldStatic（贴柱时斜线会被柱体挡住，头顶起线+游走绕位自然找窗口）
			const FVector ThrowStart = GetActorLocation() + FVector(0.f, 0.f, 80.f);
			FHitResult ThrowLOS;
			FCollisionQueryParams ThrowParams(SCENE_QUERY_STAT(EnemyThrowLOS), false, this);
			const bool bThrowLOS = !GetWorld()->LineTraceSingleByChannel(ThrowLOS, ThrowStart,
				PlayerPawn->GetActorLocation(), ECC_WorldStatic, ThrowParams);
			if (bThrowLOS)
			{
				RangedAttackCooldown = 0.f;
				SetActorRotation(FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation());

				// 投掷弹道视觉：橙色掷线 + 落点标记——来源可辨识（近战无弹道/寒霜蓝/投掷橙）
				DrawDebugLine(GetWorld(), ThrowStart, PlayerPawn->GetActorLocation(), FColor::Orange, false, 0.4f, 0, 3.f);
				DrawDebugSphere(GetWorld(), PlayerPawn->GetActorLocation(), 16.f, 10, FColor::Orange, false, 0.3f, 0, 2.f);

				const bool bHit = FMath::FRand() < RangedHitChance;
				if (bHit)
				{
					PlayerHealth->ApplyCombatDamage(RangedDamageToPlayer, this,
						{ECombatDamageKind::Projectile, PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 160.f)});
				}
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] %s 高处投掷（支撑面差 %.0f，命中 %s），玩家余血 %.0f"),
					*GetNameSafe(this), FloorDeltaZ, bHit ? TEXT("是") : TEXT("否"), PlayerHealth->GetCurrentHealth());
			}
			return;
		}

		// 够不着又不具备远程的近战敌（猎犬/层差不足的标准兵）：切向游走包围——
		// 活的包围姿态取代直挺挺推墙呆立（用户复测四 2026-09-13"只能呆呆看着"的业余感治理）
		if (!bSameLayer)
		{
			const FVector Tangent = FVector(-ToPlayer.Y, ToPlayer.X, 0.f).GetSafeNormal() * CircleDirection;
			AddMovementInput(Tangent, 0.6f);
		}
		return;
	}

	// 贴脸挥拳节拍：动画与伤害同拍出发（动画纯表现，伤害走全项目唯一入口 HealthComponent；
	// Instigator=自己——玩家血量组件的受击广播会把它递到队友的威胁候选集）
	PlayerAttackCooldown += DeltaSeconds;
	if (PlayerAttackCooldown >= PlayerAttackInterval)
	{
		PlayerAttackCooldown = 0.f;

		// 面向玩家挥拳（停步后朝向保留行进方向，须显式校正）
		SetActorRotation(FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation());

		if (AttackAnimation && GetMesh())
		{
			GetMesh()->PlayAnimation(AttackAnimation, /*bLooping*/ false);
			// 动画播完回 ABP：时长由资产自报（GetPlayLength），不依赖易变的结束回调 API
			const UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(AttackAnimation);
			const float AnimLength = Seq ? Seq->GetPlayLength() : 1.f;
			GetWorldTimerManager().SetTimer(AttackAnimTimerHandle, this, &AEnemyGrunt::RestoreLocomotionAnim, AnimLength, /*bLoop*/ false);
		}

		PlayerHealth->ApplyCombatDamage(AttackDamageToPlayer, this,
			{ECombatDamageKind::Melee, PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 160.f)});
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] %s 挥拳攻击玩家，伤害 %.0f，玩家余血 %.0f（敌 Z %.0f / 玩家 Z %.0f）"),
			*GetNameSafe(this), AttackDamageToPlayer, PlayerHealth->GetCurrentHealth(),
			GetActorLocation().Z, PlayerPawn->GetActorLocation().Z);
	}
}

void AEnemyGrunt::RefreshChasePath(APawn* PlayerPawn, const float DeltaSeconds)
{
	// 回退窗（墙顶卡死案 2026-09-11）：上次寻路失败，窗内不重算（直指玩家兜底），窗满再试
	if (NavFallbackGrace > 0.f)
	{
		NavFallbackGrace -= DeltaSeconds;
		return;
	}

	// 节流算路：0.5s 重算一次完整路径（玩家在动，路径跟随刷新）；≤8 敌小图的同步算路开销可忽略
	PathRefreshCountdown -= DeltaSeconds;
	if (PathRefreshCountdown > 0.f)
	{
		return;
	}
	PathRefreshCountdown = PathRefreshInterval;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		NavFallbackGrace = NavFallbackWindow;
		return;
	}

	UNavigationPath* NavPath = NavSys->FindPathToActorSynchronously(this, GetActorLocation(), PlayerPawn);
	const FNavPathSharedPtr PathShared = NavPath ? NavPath->GetPath() : nullptr;
	if (PathShared.IsValid() && PathShared->GetPathPoints().Num() > 0)
	{
		ChasePathPoints.Reset();
		for (const FNavPathPoint& Pt : PathShared->GetPathPoints())
		{
			ChasePathPoints.Add(FVector(Pt.Location));
		}
		ChasePathIndex = 0;
	}
	else
	{
		ChasePathPoints.Reset();
		NavFallbackGrace = NavFallbackWindow;
	}
}

bool AEnemyGrunt::HasClearLineToPlayer(const APawn* PlayerPawn) const
{
	// 胸口对胸口的 ObjectType 探针：只认 WorldStatic/WorldDynamic 几何——Pawn 对象不在查询集里，
	// 同伴围挤互不挡拳、玩家胶囊不遮挡；命中任何几何 = 中间隔着地板/墙，绝不隔空挥拳
	FHitResult Hit;
	const FVector Start = GetActorLocation() + FVector(0.f, 0.f, kChestOffset);
	const FVector End = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, kChestOffset);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, ObjectParams);
}

void AEnemyGrunt::RestoreLocomotionAnim()
{
	// 双保险：死亡时定时器已在 HandleDeath 清除，此处再挡一道（防御清空与到点的时序竞争）
	if (IsDead())
	{
		return;
	}

	if (GetMesh())
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	}
}

void AEnemyGrunt::HandleDeath(UHealthComponent* HealthComp)
{
	SetEnraged(false);
	ConsumeFrostResidue(true); ConsumeFrostResidue(false); // 死亡立即清两来源及传播资格，不等下一帧。
	// 召唤师直接Spawn的杂兵也必须上报；蠕虫以Health弱引用去重导演已有的OnDeath信号。
	if (AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>())
		if (AWaveDirector* Director = GM->GetWaveDirector()) Director->NotifyMobDeath(this);
	// 只隐藏视觉；处决在主伤害之后消费层数，死亡不能抢先改写MarkStacks。
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(0); }
	// 攻击回切定时器若还挂着，会到点把骨骼从物理切回 ABP（打断布娃娃）——先清
	GetWorldTimerManager().ClearTimer(AttackAnimTimerHandle);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::EnemyDie);   // [SFX] 敌人死亡

	// ragdoll 布娃娃死亡：MM_Death_* 六动画全是"受击踉跄跪蹲"同风格（用户实测六连同一观感，2026-09-07），弃动画上物理——
	// PA_Mannequin 物理体自然倒地，受击方向动量与地形自适应，观感即真实。
	// 碰撞预设必须切 Ragdoll（QueryAndPhysics）：mesh 默认 CharacterMesh 预设是 QueryOnly，
	// 只开 SimulatePhysics 不换预设 = 物理模拟但零碰撞响应，尸体直沉地底（"沉地"案根因）
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetAllBodiesSimulatePhysics(true);

	// 尸体停留 3s 供视觉确认后自毁（退出目标集合）
	SetLifeSpan(3.f);

	// M3.75 B5 掉落：经验晶体入世界（VS 成长循环入口）——生成方只管喂 XpValue，
	// 收集上报的组合根在 GameMode→WaveDirector（掉落方与节奏中枢互不认识，职责分离）
	if (XpPickupClass && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (AXpPickup* Pickup = GetWorld()->SpawnActor<AXpPickup>(XpPickupClass, GetActorLocation(), FRotator::ZeroRotator, Params))
		{
			Pickup->XpValue = XpValue;
		}
	}

	// M3.75 回血掉落：概率掉柠檬蛋糕（蛮兵必掉——重怪即补给点，走位冒险绕路去捡的张力线）
	if (HealPickupClass && GetWorld() && FMath::FRand() < HealDropChance)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		GetWorld()->SpawnActor<AHealthPickup>(HealPickupClass, GetActorLocation(), FRotator::ZeroRotator, Params);
	}

	// E3 宝箱经济收入端：概率掉金币（蛮兵必掉 3 枚=小金库）——拾取入钱包，开宝箱消费
	if (GoldPickupClass && GetWorld() && FMath::FRand() < GoldDropChance)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		for (int32 i = 0; i < GoldBurst; ++i)
		{
			// 枚间散开小偏移，落地成一小撮而非叠一坨
			const FVector Offset = FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 0.f);
			if (AGoldPickup* Gold = GetWorld()->SpawnActor<AGoldPickup>(GoldPickupClass, GetActorLocation() + Offset, FRotator::ZeroRotator, Params))
			{
				Gold->GoldValue = 1;
			}
		}
	}

	// E5 暂态道具：稀有掉落 rush 三件套（磁吸风暴/狂怒攻速/圣盾无敌——种类随机定型，精英掉宝节拍）
	if (RushPickupClass && GetWorld() && FMath::FRand() < RushDropChance)
	{
		FActorSpawnParameters RushParams;
		RushParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (ATemporaryPickup* Rush = GetWorld()->SpawnActor<ATemporaryPickup>(RushPickupClass, GetActorLocation() + FVector(0.f, 0.f, 40.f), FRotator::ZeroRotator, RushParams))
		{
			Rush->RushType = static_cast<ERushType>(FMath::RandRange(0, 2));
		}
	}
}

void AEnemyGrunt::ApplySlow(const float Duration, const float SpeedFactor)
{
	UCharacterMovementComponent* Cmc = GetCharacterMovement();
	if (!Cmc || IsDead())
	{
		return;
	}

	// 移动控制与状态显示分开；不让寒霜或地刺覆盖敌人变体身体色。
	Cmc->MaxWalkSpeed = BaseWalkSpeed * SpeedFactor;

	// 到点还原基线（重复命中刷新定时器，不会叠加错乱）
	GetWorldTimerManager().SetTimer(FrostRecoverTimerHandle, this,
		&AEnemyGrunt::RecoverFromSlow, Duration, /*bLoop*/ false);
}

void AEnemyGrunt::RecoverFromSlow()
{
	if (IsDead())
	{
		return;
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->MaxWalkSpeed = BaseWalkSpeed;
	}
}

void AEnemyGrunt::ApplyVariant(const EGruntVariant InVariant)
{
	GruntVariant = InVariant;

	// 变体参数集（提案 B3：数据差异≠类型爆炸）——评分器的戏剧配方：
	// 猎犬快而脆（先贴脸咬人，仇恨覆盖把它顶上首选）；蛮兵慢而硬（先集火蛮兵还是先清贴脸猎犬，
	// 评分板全程有戏）。色调在近黑基底上做红/蓝偏移：远看可辨，近看同族
	// 09-22 移速重校（用户实测"被聚成一坨慢慢蹭，无压迫感"）：玩家 500 的四成太散步——
	// 1.1真实定型值：标准405/猎犬480/蛮兵300/妖术师200/虚空裔420；后期成长由导演另乘。
	float MoveSpeed = 405.f;   // 1.1首测反馈：仅Standard基础速度450降低10%，其他分支明确覆写。
	float MaxHp = 45.f;   // 09-28 用户反馈"血太厚打不死、被追挫败感强"：标准兵 100→45（队友 10 伤/0.8s≈4 发倒地）——让首波即升 1-2 级拿到强化
	float Damage = 5.f;
	float AttackInterval = 1.5f;
	float BodyScale = 1.f;
	FVector Tint(0.03f, 0.03f, 0.035f);

	// 蛋糕掉率随分支显式覆写（ApplyVariant 可重复调用，每分支显式赋值保证幂等）：
	// 用户复测三定调"爆率太高几乎不需回血"——全线下调（蛮兵 1.0→0.5 / 标准 0.15→0.08 / 猎犬 0.05→0.03）
	// 金币掉率同批覆写（E3）：蛮兵必掉 3 枚=小金库——宝箱经济的收入梯度
	// 高处反制（E3 同轮）：仅蛮兵具备投掷能力——可达性触发的部分反制，全面远程会失去近战张力
	switch (GruntVariant)
	{
	case EGruntVariant::Rusher:
		MoveSpeed = 480.f; MaxHp = 30.f; Damage = 3.f; AttackInterval = 1.2f; BodyScale = 0.9f;   // 09-28 降血同批：60→30（3 发倒地，猎犬定位"快而脆"回归本义）
		Tint = FVector(0.09f, 0.02f, 0.02f);
		XpValue = 1;
		HealDropChance = 0.02f;   // 09-25 用户反馈"蛋糕偏高、其他偏低"：0.03→0.02
		GoldDropChance = 1.f;     // 09-25 金币常规化：0.4→1.0
		GoldBurst = 1;
		RushDropChance = 0.045f;  // 暂态三件套上调：0.02→0.045
		bRangedCapable = false;
		break;
	case EGruntVariant::Brute:
		MoveSpeed = 300.f; MaxHp = 140.f; Damage = 12.f; AttackInterval = 2.f; BodyScale = 1.25f;   // 09-28 降血同批：300→140（约 9 秒，硬茬质感保留但不再"永远打不死"）
		Tint = FVector(0.02f, 0.03f, 0.09f);
		XpValue = 5;
		HealDropChance = 0.3f;    // 09-25 蛋糕降：0.5→0.3
		GoldDropChance = 1.f;
		GoldBurst = 3;
		RushDropChance = 0.25f;   // 暂态上调：0.2→0.25
		bRangedCapable = true;
		break;
	case EGruntVariant::VoidRusher:
		// 虚空裔召唤物（09-30 用户裁决"召唤小怪加强数量与质量"）：比野生猎犬（480/30/3）更硬更痛
		// 但稍慢可风筝——清杂是召唤师战的核心博弈，杂兵太脆则护盾形同虚设；巫术造物不掉战利品
		// （金币/蛋糕/暂态全零——12 只召唤物若照猎犬掉金币，经济链直接通胀）
		MoveSpeed = 420.f; MaxHp = 75.f; Damage = 6.f; AttackInterval = 1.f; BodyScale = 1.05f;
		Tint = FVector(0.62f, 0.3f, 1.f);   // 虚空紫——与普通猎犬暗红一眼分家
		XpValue = 2;
		HealDropChance = 0.f;
		GoldDropChance = 0.f;
		GoldBurst = 0;
		RushDropChance = 0.f;
		bRangedCapable = false;
		break;
	case EGruntVariant::Warlock:
		// 妖术师（09-25 Boss 变体，用户点名"缓慢大怪+红色射线预警→实体伤害"）：
		// 缓慢巨物站桩施法，中距锁定玩家放蓄力射线（1.2s 红细线预警→粗束结算，走位可躲）
		MoveSpeed = 200.f; MaxHp = 450.f; Damage = 10.f; AttackInterval = 2.5f; BodyScale = 1.6f;   // 09-30 用户实测"七八个怪堆着杀不死"——900 血 Boss 肉度过恶心，砍半（威胁靠射线不靠血肉）
		Tint = FVector(0.06f, 0.0f, 0.09f);   // 暗紫染——与蛮兵暗蓝/猎犬暗红再分家
		XpValue = 20;
		HealDropChance = 0.f;
		GoldDropChance = 1.f;
		GoldBurst = 5;
		RushDropChance = 0.5f;
		bRangedCapable = false;   // 不用投掷反制——它有自己的蓄力射线
		break;
	case EGruntVariant::Standard:
	default:
		XpValue = 2;
		HealDropChance = 0.04f;  // 09-25 蛋糕降：0.08→0.04
		GoldDropChance = 1.f;    // 09-25 金币常规化（用户定调"金币和经验是常规掉落"）：0.4→1.0，对冲宝箱价格递增
		GoldBurst = 1;
		RushDropChance = 0.045f; // 暂态三件套上调：0.02→0.045
		bRangedCapable = true;   // 标准兵也具备投掷（用户复测四 2026-09-13：散布盒上的灰区呆立治理——猎犬保持纯近战保留张力）
		break;
	}

	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->MaxWalkSpeed = MoveSpeed;
	}
	if (HealthComponent)
	{
		// 充满语义：出生即满变体血量（组件 BeginPlay 已把 Current 钉在旧 Max 上，须同步）
		HealthComponent->SetMaxHealth(MaxHp, /*bFillCurrent*/ true);
	}
	AttackDamageToPlayer = Damage;
	PlayerAttackInterval = AttackInterval;
	SetActorScale3D(FVector(BodyScale));

	// 记录基线（寒霜减速恢复的还原基准）+ 应用配色：沿用 "Paint Tint" 真名（M3.5 实证）
	BaseWalkSpeed = MoveSpeed;
	BaseTint = Tint;
	if (UCharacterMovementComponent* CmcSlow = GetCharacterMovement())
	{
		CmcSlow->MaxWalkSpeed = BaseWalkSpeed;   // 幂等：重定型时清掉残留减速
	}
	GetMesh()->SetVectorParameterValueOnMaterials(TEXT("Paint Tint"), Tint);
}

void AEnemyGrunt::ApplyDifficulty(const float HpMult, const float XpMult)
{
	// 难度祭坛注入（09-25）：血量上限上浮并按比例回血（保持当前血量比例——对满血新生即充满），
	// 经验值上浮取整——"强者主动加压换成长"的数值落点
	if (HealthComponent)
	{
		const float NewMax = HealthComponent->GetMaxHealth() * HpMult;
		const float Ratio = HealthComponent->GetCurrentHealth() / FMath::Max(HealthComponent->GetMaxHealth(), 1.f);
		HealthComponent->SetMaxHealth(NewMax, /*bFillCurrent*/ false);
		HealthComponent->Heal(NewMax * Ratio - HealthComponent->GetCurrentHealth());
	}
	XpValue = FMath::Max(1, FMath::RoundToInt(XpValue * XpMult));
}

void AEnemyGrunt::BeginPlay()
{
	Super::BeginPlay();

	HealthComponent->OnDeath.AddDynamic(this, &AEnemyGrunt::HandleDeath);

	// 变体定型：默认 Standard = M3.5 基线（速度/血量/伤害/近黑染色与换代版完全一致——静态阵行为零漂移）
	ApplyVariant(GruntVariant);
	// 不限导演刷怪路径：召唤师直接生成的杂兵也继承当前狂暴视觉。
	if (AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>())
		if (const AWaveDirector* Director = GM->GetWaveDirector()) SetEnraged(Director->IsEnraged());

	// 游走绕向随机：够不着高处时的包围姿态不做整齐划一（复测四呆立治理配套）
	CircleDirection = FMath::FRand() < 0.5f ? -1.f : 1.f;

	// 汇聚球染红（BeginPlay 期 MID 可建——构造期只有 BasicMat 白球）
	for (USphereComponent* Orb : ChargeOrbs)
	{
		if (Orb)
		{
			if (UMaterialInstanceDynamic* OrbMid = Orb->CreateAndSetMaterialInstanceDynamic(0))
			{
				OrbMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.12f, 0.05f));
			}
		}
	}

	// 诊断日志：入场即报号（感知不到敌人时先查此行是否存在）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] Grunt spawned: %s at %s（变体 %d，XP %d）"),
		*GetNameSafe(this), *GetActorLocation().ToCompactString(), static_cast<int32>(GruntVariant), XpValue);
}

void AEnemyGrunt::UpdateChargeOrbs()
{
	// 汇聚插值（09-30 特效升级）：8 球沿各自方位角从 320cm 收敛至胸口，越接近越大——蓄力进度可视化
	const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - ChargeStartTime);
	const float P = FMath::Clamp(Elapsed / FMath::Max(BeamChargeSeconds, 0.01f), 0.f, 1.f);
	const FVector ChestRel = FVector(0.f, 0.f, 130.f);   // 胸口相对胶囊（Root=胶囊）
	for (int32 i = 0; i < 8; ++i)
	{
		if (!ChargeOrbs[i])
		{
			continue;
		}
		const float Angle = i * (2.f * PI / 8.f) + 0.7f;
		const float Dist = FMath::Lerp(320.f, 10.f, P);
		ChargeOrbs[i]->SetRelativeLocation(ChestRel + FVector(
			FMath::Cos(Angle) * Dist,
			FMath::Sin(Angle) * Dist,
			FMath::Sin(Elapsed * 9.f + i * 1.3f) * 30.f * (1.f - P)));
		ChargeOrbs[i]->SetWorldScale3D(FVector(FMath::Lerp(0.5f, 1.6f, P)));
	}
}
