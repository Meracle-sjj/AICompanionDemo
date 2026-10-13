/**
 * [INPUT]: BossBase组件契约、Health OnDeath/ApplyCombatDamage、玩家查询、GameMode击杀上报；中央上方血条由CompanionHud读取身份/Health
 * [OUTPUT]: 大胶囊/Manny/追击/死亡与稳定头顶伤害锚点；旧WidgetComponent提供真实世界锚点且永不显示，三Boss共用屏幕HUD血条。
 * [POS]: AI/ Boss 家族基类实现：占位近战行为波 2-4 由三 Boss 特色逻辑替换（召唤师护盾轮/巨像地刺/蠕虫钻地）
 *  猎印统一叠层/消费与UPROPERTY世界数字组件，免伤时不新叠，死亡只隐藏以保留当次主伤害后的消费。
 *  BeginPlay/死亡/EndPlay通知玩家刷新世界活Boss远景，存活判断与镜头计时仍归宿主Character。
 *  三Boss真实BeginPlay/死亡直接入队专属语音，绕过闲聊节流；字幕/颜色由播放队列绑定。
 *  近战提交明确类别与受击前落点；实际量和敌方来源由Health一次提交现有漫画层。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "BossBase.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "AICompanionDemoCharacter.h"
#include "AI/WaveDirector.h"
#include "AI/BossSummoner.h"
#include "AI/BossColossus.h"
#include "AI/BossWurm.h"
#include "Audio/SFXLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Vfx/HuntMarkVisual.h"

ABossBase::ABossBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 09-30 祭坛降世案：默认碰撞策略下嵌入地形即自毁——Boss 生成必须强制落位（AdjustIfPossibleButAlwaysSpawn）
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// 大体型载体（Boss 压迫力的体量基础）：胶囊 60×160
	GetCapsuleComponent()->InitCapsuleSize(60.f, 160.f);

	// Pawn 预设对 Visibility 默认 Ignore（BaseEngine.ini 原文 ECR_Ignore）——玩家弹走 ECC_Visibility
	// 穿透 Boss 胶囊=永远打不中（09-30 用户实测"完全不掉血"定罪）。显式 Block 恢复可被弹道命中
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 220.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 250.f;   // 缓慢逼近——Boss 压迫靠体量与技能不靠腿

	// 裸 C++ Character 显式装配四件套对齐（EnemyGrunt 同案）
	AIControllerClass = nullptr;   // Boss 自主 Tick 行为，不走 AIController（行为全自绘）
	// 09-30 浮空冻结案定罪：上注释张冠李戴——"无控制器模拟"必须显式开启，胶囊预设只承担
	// Visibility 通道（四件套之④）。缺此标志=引擎对无 Controller 的 Character 中止移动组件
	// 并清零速度（D10 定罪原文"生成即悬浮冻结"）：AddMovementInput 喂了没腿跑+无重力=悬空呆立
	GetCharacterMovement()->bRunPhysicsWithNoController = true;

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

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	// 保留子类与猎印使用的定位锚点；血条迁入中央上方HUD，不在Boss头顶创建widget。
	HealthBarComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarComp"));
	HealthBarComp->SetupAttachment(GetCapsuleComponent());
	HealthBarComp->SetRelativeLocation(FVector(0.f, 0.f, 260.f));   // 大体型头顶
	HealthBarComp->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarComp->SetDrawSize(FVector2D(500.f, 60.f));
	HealthBarComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarComp->SetVisibility(false);

	HuntMarkVisual = CreateDefaultSubobject<UHuntMarkVisual>(TEXT("HuntMarkVisual"));
	HuntMarkVisual->SetupAttachment(GetCapsuleComponent());
	HuntMarkVisual->SetRelativeLocation(FVector(0.f, 0.f, 360.f));
	HuntMarkVisual->SetWorldScale3D(FVector(0.65f));
}

void ABossBase::BeginPlay()
{
	Super::BeginPlay();
	if (HuntMarkVisual && HealthBarComp)
	{
		HuntMarkVisual->SetRelativeLocation(HealthBarComp->GetRelativeLocation() + FVector(0.f, 0.f, 100.f));
	}

	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(2500.f, /*bFillCurrent*/ true);
		HealthComponent->OnDeath.AddDynamic(this, &ABossBase::HandleDeath);
	}
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->RefreshBossEncounterCamera();

	// 身份可在BeginPlay后注入；HUD每次轮询读取真实名字，不依赖widget创建时序。
	if (HealthBarComp) { HealthBarComp->SetVisibility(false); }
	const EGameSFX Voice = IsA<ABossSummoner>() ? EGameSFX::Voice_BossSummonerAppear
		: IsA<ABossColossus>() ? EGameSFX::Voice_BossGolemAppear
		: IsA<ABossWurm>() ? EGameSFX::Voice_BossWurmAppear : EGameSFX::None;
	if (Voice != EGameSFX::None) USFXLibrary::PlaySFX(GetWorld(), Voice);
}

FVector ABossBase::GetDamageFeedbackAnchor() const
{
	if (IsValid(HealthBarComp)) return HealthBarComp->GetComponentLocation() + FVector(0.f, 0.f, 12.f);
	const float Height = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 160.f;
	return GetActorLocation() + FVector(0.f, 0.f, Height + 12.f);
}

void ABossBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->RefreshBossEncounterCamera(this);
	Super::EndPlay(EndPlayReason);
}

void ABossBase::SetBossIdentity(const FString& InName, int32 InIndex)
{
	BossName = InName;
	BossIndex = InIndex;
	if (HealthBarComp)
	{
		HealthBarComp->SetVisibility(false);
	}
}

void ABossBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 死亡保险丝（09-30 用户实测"0 血站桩"案防御纵深）：血量归零而死亡链未走=竞态漏网——
	// 强制引爆标准死亡链（幂等，NotifyBossKilled 不重复计数）
	if (HealthComponent && HealthComponent->GetCurrentHealth() <= 0.f && !HealthComponent->IsDeathConfirmed())
	{
		UE_LOG(LogAICompanionDemo, Error, TEXT("[Boss] %s 血量归零而死亡链未走——保险丝引爆"), *BossName);
		HealthComponent->ConfirmDeath();
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	const FVector ToPlayer = PlayerPawn->GetActorLocation() - GetActorLocation();
	const float DistCm = ToPlayer.Size2D();

	// 追击：直指玩家（占位行为——波 2-4 三 Boss 各自特色移动替换；bHoldPosition=子类行为期站桩闸）
	if (DistCm > MeleeRange * 0.8f && !bHoldPosition)
	{
		AddMovementInput(ToPlayer.GetSafeNormal2D(), 1.f);
	}

	// 贴脸近战节拍（占位攻击——波 2-4 特色攻击轮换替换）
	MeleeCooldown = FMath::Max(MeleeCooldown - DeltaSeconds, 0.f);
	if (DistCm <= MeleeRange && MeleeCooldown <= 0.f && !bHoldPosition)
	{
		MeleeCooldown = MeleeInterval;
		if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			PlayerHealth->ApplyCombatDamage(MeleeDamage, this, {ECombatDamageKind::Melee, PlayerPawn->GetActorLocation() + FVector(0, 0, 160)});
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] %s 近战 %.0f 伤 → 玩家"), *BossName, MeleeDamage);
		}
	}
}

bool ABossBase::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}

void ABossBase::AddHuntMark(int32 MaxStacks)
{
	if (!HealthComponent || IsDead() || HealthComponent->IsInvulnerable() || MaxStacks <= 0) { return; }
	MarkStacks = FMath::Min(MarkStacks + 1, MaxStacks);
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(MarkStacks); }
}

int32 ABossBase::ConsumeHuntMarks()
{
	const int32 Count = MarkStacks;
	MarkStacks = 0;
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(0); }
	return Count;
}

FString ABossBase::DebugHuntMarkState() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("{\"mark_stacks\":%d,\"dead\":%s,\"invulnerable\":%s,\"visual\":%s}"),
		MarkStacks, IsDead() ? TEXT("true") : TEXT("false"),
		HealthComponent && HealthComponent->IsInvulnerable() ? TEXT("true") : TEXT("false"),
		HuntMarkVisual ? *HuntMarkVisual->DebugVisualState() : TEXT("null"));
#else
	return FString();
#endif
}

// ---------- 死亡表现默认段（原 HandleDeath 内联逻辑，抽口供蠕虫覆写） ----------

void ABossBase::PerformDeathPresentation()
{
	// 子类注入专属死亡动画（Sevarog Death_front）优先；无动画走 ragdoll 兜底
	//（EnemyGrunt 死亡链同款：mesh 先切 Ragdoll 预设再开模拟——QueryOnly 只开
	// 模拟零碰撞响应=尸体直沉地底）
	if (DeathPresentationAnim)
	{
		GetMesh()->Stop();
		GetMesh()->PlayAnimation(DeathPresentationAnim, false);
	}
	else
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetSimulatePhysics(true);
		GetMesh()->SetAllBodiesSimulatePhysics(true);
	}
}

// ---------- Boss 家族公用探地（巨像私有副本同款逻辑，见 BossBase.h 收编注记） ----------

void ABossBase::ResolveGroundZ(FVector& Loc)
{
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(BossGroundDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = FVector(Loc.X, Loc.Y, Loc.Z + 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		Loc.Z = FloorHit.ImpactPoint.Z;
	}
}

void ABossBase::HandleDeath(UHealthComponent* /*HealthComp*/)
{
	if (bDeathHandled) return;
	bDeathHandled = true;
	const EGameSFX Voice = IsA<ABossSummoner>() ? EGameSFX::Voice_BossSummonerDefeated
		: IsA<ABossColossus>() ? EGameSFX::Voice_BossGolemDefeated
		: IsA<ABossWurm>() ? EGameSFX::Voice_BossWurmDefeated : EGameSFX::None;
	if (Voice != EGameSFX::None) USFXLibrary::PlaySFX(GetWorld(), Voice);
	// 所有Boss共享活体远景；同帧排除此宿主，不能提前恢复另一场仍在进行的Boss战。
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->RefreshBossEncounterCamera(this);
	// 先隐藏，保留层数供当前队友主伤害之后的处决消费；不让尸体继续带标记。
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(0); }
	// 上报击杀（经组合根：GameMode→WaveDirector 计数——战绩三行的数据源）
	if (AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>())
	{
		if (AWaveDirector* Director = GM->GetWaveDirector())
		{
			Director->NotifyBossKilled(BossIndex, BossName, GetActorLocation());
		}
	}

	if (HealthBarComp)
	{
		HealthBarComp->SetVisibility(false);
	}

	// 死亡表现（09-30 用户复测"死亡后没有消失"案根治：旧版无任何死亡形体语言——站尸 3s 突然
	// pop，死没死毫无区分度）。表现段抽虚函数——蠕虫覆写演"节链失引坠落"（ragdoll 不适用）
	GetCharacterMovement()->DisableMovement();
	PerformDeathPresentation();

	// 金色光柱谢幕（信标谢幕线同款范式——Boss 倒下要有仪式感）；尸体留 3s 供观礼
	DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + FVector(0.f, 0.f, 5000.f),
		FColor::FromHex(TEXT("FFD700")), false, 2.5f, 0, 16.f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] %s（%d 号）被击倒！死亡表现=%s，3s 后销毁"),
		*BossName, BossIndex, DeathPresentationAnim ? TEXT("死亡动画") : TEXT("布娃娃"));

	// 销毁到期诊断打点（09-30"不消失"悬案留档）：3.2s 后若本体仍在场，日志直接分层——
	// LifeSpan 到期而未销毁只可能是 World TimerManager 异常，不再靠肉眼猜
	TWeakObjectPtr<ABossBase> WeakThis(this);
	FTimerHandle DiagHandle;
	GetWorldTimerManager().SetTimer(DiagHandle, [WeakThis, this]()
	{
		if (WeakThis.IsValid())
		{
			UE_LOG(LogAICompanionDemo, Error, TEXT("[Boss] %s 销毁到期仍未消失（LifeSpan 异常）"), *BossName);
		}
	}, 3.2f, false);

	// Dedicated director reward queue offers the boss's three unique relics.
	SetLifeSpan(3.f);
}
