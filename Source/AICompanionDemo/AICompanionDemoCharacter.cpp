// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 Engine 的胶囊/移动/弹簧臂/相机组件、EnhancedInput 输入链路、本模块 HealthComponent（M3 玩家血量）、本模块 Audio/SFXLibrary（PlayerHurt/Blink/Recall 事件音效）、本模块 AI/Combatant 的 CollectAliveCombatants（玩家火力链候选集）
 * [OUTPUT]: v1.3 Lv6/信标语音入世界FIFO，字幕由实际播放产生；重复信标总/剩余召回CD减5秒（总CD下限5秒）； 对外提供 AAICompanionDemoCharacter——模板玩家（WASD/跳跃/视角）；M3 起作为敌方火力的合法目标（FindComponentByClass 契约可被任意阵营查询）；**Lv.6 自动射击与回响脉冲的目标/伤害集一律 CollectAliveCombatants 契约收集**（09-30 召唤师案：类型扫描曾让玩家"看不见" Boss——枪口永远越过 Boss 找杂兵；主弹命中判定走 ECC_Visibility 物理扫掠+ICombatant 契约，与走廊收集器双轨同认）；**回响脉冲 2.0（10-07）：累计器挂 OnHealApplied 治疗意图事件（辉耀结界持有者满血也计数），爆发=2s 蔓延领域（三层线框球扩张+跟随绿光+过程0.1s/1伤保留10DPS、历史半径补拍与短命命中反馈+终点爆25伤/冲击环/瞬时爆光/蠕虫同款Explosion音）——状态机实现已局部拆至AICompanionDemoCharacterHealNova.cpp，主文件订阅/推进/死亡清理保持**
 *  九遗物组件随玩家创建；旧固定三遗物逻辑退出。5s闪烁/20s召回，探地成功才耗召回CD/发语音；暂停与死亡禁主动技能，伤害/闪烁回调Relics。镜头先于玩法早退更新，蠕虫平滑远景与短震归CharacterWurmCamera；近归途门E交互优先，宝箱不竞争。
 * [POS]: 模块根的玩家角色，与 AI/CompanionCharacter 平行不继承（职责隔离）；M3 评分的"威胁/玩家安全"维度以此为数据源
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "AICompanionDemoCharacter.h"
#include "AI/Combatant.h"
#include "AI/EnemyGrunt.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionCharacter.h"
#include "AI/DifficultyAltar.h"
#include "AI/LockedChest.h"
#include "AI/TreasureChest.h"
#include "AI/WaveDirector.h"
#include "Audio/SFXLibrary.h"
#include "Events/RunEventLog.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Vfx/VfxLibrary.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Health/HealthComponent.h"
#include "Relics/BossRelicComponent.h"

AAICompanionDemoCharacter::AAICompanionDemoCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;
	// 暂停帧只清相机偏移；玩法Tick仍在世界暂停/零流速早退。
	PrimaryActorTick.bTickEvenWhenPaused = true;
	CameraBoom->PrimaryComponentTick.bTickEvenWhenPaused = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// 玩家血量（M3）：与敌人共用 HealthComponent——敌人 FindComponentByClass 即可造成伤害，零耦合
	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	CreateDefaultSubobject<UBossRelicComponent>(TEXT("BossRelics"));

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AAICompanionDemoCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeWurmCamera();

	// 死亡订阅（09-21 G2）：HealthComponent 广播 OnDeath 即进死亡序列——"恰当的暂停"由此发端
	// 受击订阅（09-22 G4）：镜头震动自持 + 红闪经组合根脉冲
	// 血量变化订阅（09-26 夜轮）：治疗反馈/受伤累计/濒死边沿
	// 治疗意图订阅（10-07 回响脉冲 2.0）：累计器改挂 OnHealApplied——满血治疗也计数（辉耀结界协同）
	if (Health)
	{
		Health->OnDeath.AddDynamic(this, &AAICompanionDemoCharacter::HandleDeath);
		Health->OnDamaged.AddDynamic(this, &AAICompanionDemoCharacter::HandleHurt);
		Health->OnHealthChanged.AddDynamic(this, &AAICompanionDemoCharacter::HandleHealthChanged);
		Health->OnHealApplied.AddDynamic(this, &AAICompanionDemoCharacter::HandleHealApplied);
	}
}

void AAICompanionDemoCharacter::HandleHurt(UHealthComponent* /*HealthComp*/, AActor* InstigatorActor)
{
	// 镜头震动自持（弹簧臂 TargetOffset 随机偏移，Tick 衰减）；闪红走组合根（UI 不归角色管）
	// 方向角已删（09-24 用户裁决：割草游戏方向不重要，红色提示够了）
	// 受击音效已迁 HandleHealthChanged 累计制（09-28：逐击播报太吵，稀缺性=信息价值）
	CameraShakeTime = 0.3f;
	if (UBossRelicComponent* Relics = FindComponentByClass<UBossRelicComponent>())
	{ Relics->NotifyPlayerDamaged(InstigatorActor, Health->GetLastDamageTaken()); }

	if (UWorld* World = GetWorld())
	{
		if (AAICompanionDemoGameMode* GM = World->GetAuthGameMode<AAICompanionDemoGameMode>())
		{
			GM->NotifyPlayerHurt();
		}
	}
}

// ---------- 宝箱钥匙（09-25 扩展 C；09-28 用户点名补语音播报） ----------

void AAICompanionDemoCharacter::GiveTreasureKey()
{
	const bool bFirstKey = !bHasTreasureKey;
	bHasTreasureKey = true;
	if (bFirstKey)
	{
		// 队友播报（事件触发一次性查找成本可接受）：文字+语音+HUD 钥槽点亮三同步
		for (TActorIterator<ACompanionAIController> It(GetWorld()); It; ++It)
		{
			It->Bark(TEXT("钥匙到手，找带锁的箱子。"), TEXT("Key"), 0.f, EGameSFX::Voice_TreasureKey);
			break;
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Key] 拾取宝箱钥匙——锁箱资格到手"));
	}
}

// ---------- 回响脉冲（09-26 夜轮用户提案：回复也是武器） ----------

void AAICompanionDemoCharacter::HandleHealthChanged(UHealthComponent* /*HealthComp*/, float /*NewHealth*/, float Delta)
{
	// 受伤累计警报（09-30 用户复测三定调："3 秒内掉了 20 血就播，否则都不播放"）：
	// 3s 窗累计 ≥20 才响 + 响后 6s 静默冷却；冷却期用内层条件不响——**不再 return 整个函数**
	//（上一版 return 连治疗光效一起吞了，结构缺陷本轮回修）。圣盾期 ApplyDamage 早退不广播，天然正确
	if (Delta < 0.f && FPlatformTime::Seconds() >= HurtAlarmReadyTime)
	{
		const double NowD = FPlatformTime::Seconds();
		if (NowD - DamageWindowStart > 3.0)
		{
			DamageWindowStart = NowD;
			RecentDamageAccum = 0.f;
		}
		RecentDamageAccum += -Delta;
		if (RecentDamageAccum >= 20.f)
		{
			RecentDamageAccum = 0.f;
			HurtAlarmReadyTime = NowD + 6.0;   // 响后消停 6 秒
			USFXLibrary::PlaySFX(GetWorld(), EGameSFX::PlayerHurt);   // [SFX] 受伤警报（累计制+冷却）
			// 队友喊话同口触发（09-30 双通道合并：挨打 Bark 从 HandlePlayerDamaged 迁来——
			// 原本两路异步各自 6s 冷却播同一句=体感频繁；现在一条通路一个冷却）
			for (TActorIterator<ACompanionAIController> CtlIt(GetWorld()); CtlIt; ++CtlIt)
			{
				CtlIt->Bark(TEXT("放开我的伙伴！"), TEXT("PlayerHurt"), 6.f, EGameSFX::Voice_PlayerHurt);
				break;
			}
		}
	}

	// 治疗反馈光效（09-26 夜轮用户点名"贴贴/辉耀治疗在身上没任何效果"）：绿色 +N 飘字 + 周身绿色脉冲圈。
	// 节流 0.6s——贴贴/辉耀是 0.5s 节拍的细水长流，逐拍画会糊成一片；合并成可读的脉冲节奏
	if (Delta > 0.f)
	{
		const float Now = FPlatformTime::Seconds();
		if (Now - LastHealFxTime >= 0.6f)
		{
			LastHealFxTime = Now;
			const FVector Base = GetActorLocation();
			DrawDebugString(GetWorld(), Base + FVector(0.f, 0.f, 130.f),
				FString::Printf(TEXT("+%.0f"), Delta), nullptr, FColor::FromHex(TEXT("7CFC8A")), 0.8f, false, 1.1f);
			DrawDebugSphere(GetWorld(), Base + FVector(0.f, 0.f, 60.f), 55.f, 12, FColor::FromHex(TEXT("7CFC8A")), false, 0.45f, 0, 1.5f);
			// 屏幕级治疗绿闪（检索结论：RoR2/守望式瞬时治疗反馈，alpha 0.08 极轻不抢焦点）
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
				{
					CamMgr->StartCameraFade(0.f, 0.08f, 0.18f, FLinearColor(0.49f, 0.99f, 0.54f, 1.f), false, false);
					CamMgr->StartCameraFade(0.08f, 0.f, 0.30f, FLinearColor(0.49f, 0.99f, 0.54f, 1.f), false, false);
				}
			}
		}

		// 濒死恢复台词（09-27 用户点名"从濒死恢复到健康加语音"）：血量跌破 30% 挂旗，回升穿 70% 喊一句
		if (Health)
		{
			const float Pct = Health->GetCurrentHealth() / FMath::Max(Health->GetMaxHealth(), 1.f);
			if (Pct < 0.3f)
			{
				bNearDeathFlag = true;
			}
			else if (bNearDeathFlag && Pct >= 0.7f)
			{
				bNearDeathFlag = false;
				for (TActorIterator<ACompanionAIController> It(GetWorld()); It; ++It)
				{
					It->Bark(TEXT("生命体征恢复，很好。"), TEXT("Recover"), 20.f, EGameSFX::Voice_Recovered);
					break;
				}
			}
		}
	}

	// 治疗累计器已迁 HandleHealApplied（10-07 回响脉冲 2.0：血量事件在满血时静默，
	// "累计治疗"的消费方必须挂治疗意图事件——本函数只管视觉反馈与濒死台词）
}

void AAICompanionDemoCharacter::HandleDeath(UHealthComponent* /*HealthComp*/)
{
	ResetWurmEncounterCamera();
	// 回响脉冲蔓延态清理（10-07）：死亡即终止领域——光组件销毁防跨局残留
	ClearHealNovaState();

	// 禁操作 + 放出鼠标（死亡屏按钮需要光标）
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
		PC->bShowMouseCursor = true;
	}

	// 布娃娃倒地（零资产死亡动画）：动画蓝图先退位（否则 AnimBP 每帧覆写 pose 与物理打架）+
	// 骨架交物理 + 胶囊碰撞让位（尸体贴地不悬空不挡路）。
	// 注意：倒地需要真实世界时间——此处的 dilation 仍为 1，定格由死亡屏黑幕落定后经 GameMode::FreezeWorld 执行
	//（首版 ShowDeathScreen 立即 dilation=0 把布娃娃物理定格在站姿——用户截图"僵直站立"案）
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetAnimInstanceClass(nullptr);
	GetMesh()->SetSimulatePhysics(true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 死亡屏经组合根弹入（装配/流转/统计全在 GameMode——角色只报告"我倒下了"）
	if (UWorld* World = GetWorld())
	{
		if (AAICompanionDemoGameMode* GM = World->GetAuthGameMode<AAICompanionDemoGameMode>())
		{
			GM->ShowDeathScreen();
		}
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Death] 玩家阵亡——布娃娃 + 死亡屏"));
}

// ---------- 紧急呼叫（09-25 深度扩展 A：信标拾取解锁 C 键召回） ----------

void AAICompanionDemoCharacter::UnlockRecall()
{
	if (bRecallUnlocked)
	{
		const float OldCooldown = RecallCooldownSeconds;
		RecallCooldownSeconds = FMath::Max(5.f, RecallCooldownSeconds - 5.f);
		RecallCooldownCountdown = FMath::Max(0.f, RecallCooldownCountdown - (OldCooldown - RecallCooldownSeconds));
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_BeaconRepeat);
		UE_LOG(LogAICompanionDemo, Log, TEXT("[Beacon] 重复信标 CD %.0f→%.0f remaining=%.2f"), OldCooldown, RecallCooldownSeconds, RecallCooldownCountdown);
		return;
	}
	bRecallUnlocked = true;
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_BeaconActivate);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Beacon] C 键召回已解锁（CD %.0fs）"), RecallCooldownSeconds);
}

void AAICompanionDemoCharacter::RecallCompanion()
{
	if (!Health || Health->GetCurrentHealth() <= 0.f || Health->IsDead()
		|| UGameplayStatics::GetGlobalTimeDilation(this) < .01f || GetWorld()->IsPaused()) return;
	if (!bRecallUnlocked || RecallCooldownCountdown > 0.f)
	{
		if (bRecallUnlocked)
		{
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 160.f),
				FString::Printf(TEXT("信标充能中 %.0fs"), RecallCooldownCountdown),
				this, FColor::Silver, 0.8f, false, 1.f);
		}
		return;
	}

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(
		UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
	if (!Companion)
	{
		return;
	}

	// 队友传送：玩家 3m 环点随机位 + 探地落位（ByObjectType——闪烁四审教训：怪胶囊不挡线）
	const FVector Start = Companion->GetActorLocation();
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const FVector Target = GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 300.f;
	FHitResult GroundHit;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(RecallTeleport), false, Companion);
	FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(GroundHit, Target + FVector(0.f, 0.f, 300.f),
		Target - FVector(0.f, 0.f, 3000.f), GroundObjects, GroundParams))
	{
		if (!Companion->SetActorLocation(FVector(Target.X, Target.Y, GroundHit.ImpactPoint.Z + 96.f), /*bSweep*/ false)) { return; }
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Recall);   // [SFX] 召回传送成功（与信标拾取复用同槽）
	}
	else { return; }
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Voice_BeaconRecall);


	// 光效：起讫双闪 + 青色连线（与玩家闪烁同语言、异色系）
	DrawDebugLine(GetWorld(), Start, Companion->GetActorLocation(), FColor::Cyan, false, 0.6f, 0, 6.f);
	DrawDebugSphere(GetWorld(), Start, 30.f, 12, FColor::Cyan, false, 0.4f, 0, 2.f);
	DrawDebugSphere(GetWorld(), Companion->GetActorLocation(), 34.f, 12, FColor::Cyan, false, 0.5f, 0, 2.f);

	RecallCooldownCountdown = RecallCooldownSeconds;
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Beacon] 紧急呼叫！队友已传送至身边（CD %.0fs）"), RecallCooldownSeconds);
}

void AAICompanionDemoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAICompanionDemoCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AAICompanionDemoCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAICompanionDemoCharacter::Look);
	}
	else
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AAICompanionDemoCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AAICompanionDemoCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AAICompanionDemoCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AAICompanionDemoCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AAICompanionDemoCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AAICompanionDemoCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AAICompanionDemoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickWurmCamera(DeltaSeconds);
	if (!Health || Health->GetCurrentHealth()<=0.f || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this)<0.01f) { return; }

	// 闪烁冷却推进
	DashCooldownCountdown = FMath::Max(DashCooldownCountdown - DeltaSeconds, 0.f);

	// 召回冷却推进（09-25 紧急呼叫）
	RecallCooldownCountdown = FMath::Max(RecallCooldownCountdown - DeltaSeconds, 0.f);

	// 回响脉冲蔓延态推进（10-07 领域重做）：球径扩张/过程伤害/终点爆
	TickHealNova(DeltaSeconds);

	// ---------- 玩家射击（09-25 等级成长：Lv.6 解锁左键） ----------
	// 等级经组合根轻量查询（int 比较零成本）；解锁一次性置旗 + 教学飘字
	if (!bPlayerShootingUnlocked)
	{
		if (UWorld* World = GetWorld())
		{
			if (const AAICompanionDemoGameMode* GM = World->GetAuthGameMode<AAICompanionDemoGameMode>())
			{
				if (const AWaveDirector* Director = GM->GetWaveDirector())
				{
					if (Director->GetUpgradeLevel() >= 6)
					{
						bPlayerShootingUnlocked = true;
						ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
						const bool bVoiceStarted = USFXLibrary::TryPlayVoice(World, EGameSFX::Voice_PlayerAttackUnlock,
							(Buddy ? Buddy->GetActorLocation() : GetActorLocation()) + FVector(0.f,0.f,60.f));
						UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] PlayerAttackUnlock voice=%d"), bVoiceStarted);
						DrawDebugString(GetWorld(), FVector(0.f, 0.f, 220.f),
							TEXT("Lv.6：你的武器开始自动反击！"), this, FColor::Yellow, 3.f, false, 1.2f);
						UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 玩家射击解锁（Lv.6，自动开火）"));
					}
				}
			}
		}
	}

	// 自动节拍射击（09-26 夜轮用户定调"和 AI 队友一样"——玩家也是半个炮台，操作只剩走位）：
	// 解锁后每拍自动选最近活敌开火（距离 ≤ 射程），无敌人零弹道噪音；左键指令退役。
	// 目标集契约收集（09-30 召唤师案：类型扫描让玩家根本"看不见" Boss——Boss 站在你面前
	// 枪口却永远越过他找杂兵）
	PlayerShotCooldown = FMath::Max(PlayerShotCooldown - DeltaSeconds, 0.f);
	if (bPlayerShootingUnlocked && PlayerShotCooldown <= 0.f)
	{
		AActor* Nearest = nullptr;
		float BestSq = PlayerShotRange * PlayerShotRange;
		TArray<AActor*> FireCandidates;
		CollectAliveCombatants(GetWorld(), FireCandidates);
		for (AActor* Grunt : FireCandidates)
		{
			const float Dsq = FVector::DistSquared(GetActorLocation(), Grunt->GetActorLocation());
			if (Dsq < BestSq)
			{
				BestSq = Dsq;
				Nearest = Grunt;
			}
		}
		if (Nearest)
		{
			PlayerShotCooldown = PlayerShotInterval;
			PlayerFire(Nearest);
		}
	}

	// 圣盾护体特效（09-28 用户点名"无敌时要有被保护的感觉"）：金色双线框球常驻（每帧重画短寿命=零资产护盾视觉）；
	// 剩余 <1.5s 时按 0.4s 周期闪烁提示将逝——"盾要没了"也是信息
	if (Health && Health->IsInvulnerable())
	{
		const float InvulnRemain = Health->GetInvulnerableRemaining();
		const bool bFading = InvulnRemain < 1.5f;
		const bool bBlinkOff = bFading && static_cast<int32>(FPlatformTime::Seconds() * 2.5) % 2 == 0;
		if (!bBlinkOff)
		{
			const FVector ShieldBase = GetActorLocation() + FVector(0.f, 0.f, 50.f);
			DrawDebugSphere(GetWorld(), ShieldBase, 95.f, 14, FColor::FromHex(TEXT("FFD700")), false, 0.06f, 0, 2.f);
			DrawDebugSphere(GetWorld(), ShieldBase, 82.f, 12, FColor::FromHex(TEXT("FFE9A0")), false, 0.06f, 0, 1.f);
		}
	}

	// Shift 边沿检测触发闪现（D9 Q 键同款零资产范式——不加 EnhancedInput 资产）
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bShiftDown = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
	if (bBlinkUnlocked && bShiftDown && !bPrevShiftDown && DashCooldownCountdown <= 0.f)
	{
		PerformDash();
	}
	bPrevShiftDown = bShiftDown;

	// E 键边沿触发宝箱交互（M3.75 E3：250cm 内最近未开宝箱——道具交互的代表件；
	// 迭代只在按键瞬间发生，常态零开销。失败原因（金币不足）由宝箱头顶飘字自解释）
	const bool bEDown = PC && PC->IsInputKeyDown(EKeys::E);
	const AAICompanionDemoGameMode* InteractionGM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	if (bEDown && !bPrevEDown && !(InteractionGM && InteractionGM->HasNearbyEndingPortal()))
	{
		ATreasureChest* Best = nullptr;
		float BestDistSq = 250.f * 250.f;
		for (TActorIterator<ATreasureChest> It(GetWorld()); It; ++It)
		{
			ATreasureChest* Chest = *It;
			if (Chest && !Chest->IsOpened())
			{
				const float DistSq = FVector::DistSquared(GetActorLocation(), Chest->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = Chest;
				}
			}
		}
		if (Best)
		{
			Best->TryOpen();
		}
		else
		{
			// 就近万物语义（09-25 扩展）：无宝箱时优先锁箱（持钥即开）、再无则难度祭坛
			ALockedChest* BestLocked = nullptr;
			float LockedDistSq = 250.f * 250.f;
			for (TActorIterator<ALockedChest> LockedIt(GetWorld()); LockedIt; ++LockedIt)
			{
				ALockedChest* Locked = *LockedIt;
				if (Locked && !Locked->IsOpened())
				{
					const float DistSq = FVector::DistSquared(GetActorLocation(), Locked->GetActorLocation());
					if (DistSq < LockedDistSq)
					{
						LockedDistSq = DistSq;
						BestLocked = Locked;
					}
				}
			}
			if (BestLocked)
			{
				BestLocked->TryOpen();
			}
			else
			{
				ADifficultyAltar* BestAltar = nullptr;
				float AltarDistSq = 250.f * 250.f;
				for (TActorIterator<ADifficultyAltar> AltarIt(GetWorld()); AltarIt; ++AltarIt)
				{
					ADifficultyAltar* Altar = *AltarIt;
					if (Altar && !Altar->IsSacrificed())
					{
						const float DistSq = FVector::DistSquared(GetActorLocation(), Altar->GetActorLocation());
						if (DistSq < AltarDistSq)
						{
							AltarDistSq = DistSq;
							BestAltar = Altar;
						}
					}
				}
				if (BestAltar)
				{
					BestAltar->TrySacrifice();
				}
			}
		}
	}
	bPrevEDown = bEDown;

	// C 键边沿触发紧急呼叫（09-25：信标拾取解锁；无技能时零开销早退）
	const bool bCDown = PC && PC->IsInputKeyDown(EKeys::C);
	if (bCDown && !bPrevCDown)
	{
		RecallCompanion();
	}
	bPrevCDown = bCDown;
}

void AAICompanionDemoCharacter::PerformDash()
{
	if (!Health || Health->GetCurrentHealth() <= 0.f || Health->IsDead()
		|| UGameplayStatics::GetGlobalTimeDilation(this) < .01f || GetWorld()->IsPaused()) return;
	if (!bBlinkUnlocked || DashCooldownCountdown > 0.f)
	{
		return;
	}

	// 方向：移动输入优先（往哪推杆往哪闪）；静止时朝相机前向（逃跑方向跟视角）
	FVector Dir = GetLastMovementInputVector();
	if (Dir.IsNearlyZero() && GetFollowCamera())
	{
		Dir = GetFollowCamera()->GetForwardVector();
	}
	Dir = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	// 传送语义（用户 09-12 定调：闪烁=传送，不是沿路拖拽——路径上的敌人/杂物全部穿过）：
	// 09-21 四审定罪（用户复测"面对敌人闪现，连线变短被挡"）：byChannel(WorldStatic) 的真语义是
	// "查所有对 WorldStatic 通道响应 Block 的物体"——Pawn profile 胶囊对 WorldStatic 默认 Block，
	// 敌人/队友全在挡线！改 ByObjectType（对象查询）才真正"只查静态世界"：地形建筑挡人，怪与队友穿过
	const FVector Start = GetActorLocation();
	FVector Desired = Start + Dir * DashDistance;

	FCollisionQueryParams BlinkParams(SCENE_QUERY_STAT(BlinkTeleport), /*bTraceComplex*/ false, /*IgnoredActor*/ this);
	FHitResult WallHit;
	{
		FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
		if (GetWorld()->LineTraceSingleByObjectType(WallHit, Start, Desired, StaticObjects, BlinkParams))
		{
			Desired = Start + Dir * FMath::Max(WallHit.Distance - 50.f, 0.f);
		}
	}

	// 落点贴地：探脚下地面（上坡/台阶自适应——等高瞬移会埋进坡里）；同改对象查询（防探到怪头顶当地面）
	FHitResult GroundHit;
	const FVector GroundProbe = Desired + FVector(0.f, 0.f, 200.f);
	{
		FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
		if (GetWorld()->LineTraceSingleByObjectType(GroundHit, GroundProbe, GroundProbe - FVector(0.f, 0.f, 500.f), StaticObjects, BlinkParams))
		{
			Desired.Z = GroundHit.ImpactPoint.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
	}

	// 落点防重叠（用户 2026-09-13："路径上有敌人就被挡住"的体感根因）——
	// 传送本身穿敌（探线只认 WorldStatic），但落点与敌人胶囊重叠会被物理解算弹开=体感"被挡"。
	// 对策：落点 80cm 内有敌人则沿方向逐级回退 50cm 找空位（09-21 三审：步长 1m 在怪群里把 3m
	// 位移吃成小碎步=加速感元凶之一；8m 位移 + 最多 2.5m 回退，净位移保得住传送感）
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(BlinkLand), false, this);
		FCollisionObjectQueryParams ObjParams(ECollisionChannel::ECC_Pawn);   // 只查敌人/角色胶囊
		const FVector Extent(48.f, 48.f, 96.f);   // 略小于玩家胶囊，留宽容差
		for (int32 Retry = 0; Retry < 5; ++Retry)
		{
			Overlaps.Reset();
			if (!GetWorld()->OverlapMultiByObjectType(Overlaps, Desired, FQuat::Identity,
				ObjParams, FCollisionShape::MakeBox(Extent), OverlapParams))
			{
				break;
			}
			bool bBlockedByEnemy = false;
			for (const FOverlapResult& O : Overlaps)
			{
				AActor* Overlapped = O.GetActor();
				if (Overlapped && Overlapped != this && Overlapped->IsA(APawn::StaticClass()))
				{
					bBlockedByEnemy = true;
					break;
				}
			}
			if (!bBlockedByEnemy)
			{
				break;
			}
			Desired -= Dir * 50.f;   // 沿方向回退 0.5m 找空位
		}
	}

	if (!SetActorLocation(Desired, /*bSweep*/ false)) { return; }
	if (UBossRelicComponent* Relics = FindComponentByClass<UBossRelicComponent>())
	{ Relics->NotifyBlink(Start, GetActorLocation()); }
	DashCooldownCountdown = DashCooldownSeconds;

	// 闪烁光效：起点残影 + 青色连线 + 落点闪光（09-21 四审：连线恢复——特效稀缺阶段连线就是
	// 位移距离的直接读数，用户点名保留；同时连线长短从此成为穿挡 bug 的天然诊断仪）
	DrawDebugSphere(GetWorld(), Start, 26.f, 12, FColor::Cyan, false, 0.3f, 0, 2.f);
	DrawDebugLine(GetWorld(), Start, Desired, FColor::Cyan, false, 0.3f, 0, 3.f);
	DrawDebugSphere(GetWorld(), Desired, 34.f, 12, FColor::Cyan, false, 0.35f, 0, 2.f);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Blink);   // [SFX] 闪现

	// 融合·闪现轰击（闪烁+散射）：落点向八方爆射队友弹道——"你落地即爆炸"
	if (bBlinkBombUnlocked)
	{
		if (ACompanionAIController* CompanionCtl = Cast<ACompanionAIController>(
			UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass())))
		{
			CompanionCtl->NovaBurst(this, Desired);
		}
	}

	UE_LOG(LogAICompanionDemo, Log, TEXT("[Blink] 闪现 dir=%s cd=%.1fs"), *Dir.ToCompactString(), DashCooldownSeconds);
}
