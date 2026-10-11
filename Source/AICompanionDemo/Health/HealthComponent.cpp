/**
 * [INPUT]: HealthComponent/CombatDamageFeedback、真实AI归属与遗物减伤、既有UI/CombatFeedbackSubsystem、圣盾Timer。
 * [OUTPUT]: 对外提供 UHealthComponent 的实现——伤害/治疗钳制与三委托广播时序（先受击带凶手 → 血量事件 → 死亡事件；Heal 只走血量事件且死者无效）；E5 增补圣盾无敌（ApplyDamage 全额早退 + 定时自恢复）；LastDamageTaken 快照（09-30 受击阈值入口数据源）
 *  玩家遗物减伤在扣血/致死判定前统一乘算，LastDamageTaken保存实际扣血供反伤；无敌早退不触发遗物反伤。
 *  Q歼灭模式对实际AI来源伤害统一乘当前模式系数；护卫模式仅增强玩家收到的治疗，钳制与治疗意图均使用增强后的值。
 * [POS]: Health唯一生命结算；扣血后先复制落点/模式/实际量，再沿原委托与死亡时序推进，反馈不参与账本。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "HealthComponent.h"

#include "AICompanionDemo.h"
#include "AI/CompanionAIController.h"
#include "AI/Combatant.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Relics/BossRelicComponent.h"
#include "UI/CombatFeedbackSubsystem.h"

namespace
{
	// 当前模式在伤害落地时读取；子弹、传导、领域等真实AI来源均只在生命入口增强一次。
	const ACompanionAIController* ResolveDamageCompanion(AActor* Source)
	{
		for (int32 Depth = 0; IsValid(Source) && Depth < 8; ++Depth)
		{
			if (const ACompanionAIController* Controller = Cast<ACompanionAIController>(Source)) return Controller;
			if (const APawn* Pawn = Cast<APawn>(Source))
				if (const ACompanionAIController* Controller = Cast<ACompanionAIController>(Pawn->GetController())) return Controller;
			if (const APawn* Pawn = Source->GetInstigator())
				if (const ACompanionAIController* Controller = Cast<ACompanionAIController>(Pawn->GetController())) return Controller;
			Source = Source->GetOwner();
		}
		return nullptr;
	}

	ECombatDamageSource ResolveFeedbackSource(AActor* Source, const ACompanionAIController* Companion, const AActor* Player)
	{
		if (Companion)
			return Companion->CurrentMode == ECompanionMode::Berserk
				? ECombatDamageSource::CompanionBerserk : ECombatDamageSource::CompanionGuard;
		for (int32 Depth = 0; IsValid(Source) && Depth < 8; ++Depth)
		{
			if (Player && Source == Player) return ECombatDamageSource::Player;
			if (Cast<ICombatant>(Source)) return ECombatDamageSource::Hostile;
			if (Player && Source->GetInstigator() == Player) return ECombatDamageSource::Player;
			Source = Source->GetOwner();
		}
		return ECombatDamageSource::Unknown;
	}
}

UHealthComponent::UHealthComponent()
{
	// 组件默认不 Tick：血量是被动数据，事件驱动即可（DRY/KISS：不做每帧检查）
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// 运行时才取 MaxHealth 初值：让 EditAnywhere 的实例级覆盖先生效
	CurrentHealth = MaxHealth;
}

void UHealthComponent::SetMaxHealth(const float NewMax, const bool bFillCurrent)
{
	MaxHealth = FMath::Max(NewMax, 1.f);
	// 充满语义供出生定型（组件 BeginPlay 已把 Current 钉在旧 Max 上）；扩容语义只抬上限不回血
	CurrentHealth = bFillCurrent ? MaxHealth : FMath::Min(CurrentHealth, MaxHealth);
}

void UHealthComponent::ApplyDamage(float Damage, AActor* Instigator)
{
	const FVector Location = GetOwner() ? GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 90.f) : FVector::ZeroVector;
	ApplyCombatDamage(Damage, Instigator, {ECombatDamageKind::Generic, Location});
}

void UHealthComponent::ApplyCombatDamage(float Damage, AActor* Instigator, const FCombatDamageContext& Context)
{
	if (bDead || Damage <= 0.f)
	{
		return;
	}

	// E5 圣盾：无敌期伤害全额早退（先于一切钳制与广播——无敌不是减伤）
	if (bInvulnerable)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Aegis] 伤害被圣盾抵消 %.0f"), Damage);
		return;
	}

	const float OldHealth = CurrentHealth;
	const ACompanionAIController* Controller = ResolveDamageCompanion(Instigator);
	const ECombatDamageSource SourceSnapshot = ResolveFeedbackSource(Instigator, Controller, UGameplayStatics::GetPlayerPawn(this, 0));
	if (Controller)
		Damage *= Controller->GetModeDamageMultiplier();
	if (const UBossRelicComponent* Relics = GetOwner()->FindComponentByClass<UBossRelicComponent>())
	{ Damage = Relics->ModifyIncomingDamage(Damage); }
	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.f, MaxHealth);
	const float Delta = CurrentHealth - OldHealth;
	LastDamageTaken = -Delta;   // 受击阈值入口的数据源（09-30：受击表现分级判定用）

	// 时序契约：受击带凶手（最早信号）→ 血量事件（受击表现）→ 死亡事件（清理逻辑）——订阅方可以依赖此顺序。
	// 致死事实快照（09-30 召唤师"0 血站桩"案定罪）：死亡判定读成员变量时，同步订阅方（如旧护盾的
	// 回血抵消）在 OnHealthChanged 广播期间 Heal 把血拉回——判定读到回血后的正数，死亡被静默吃掉。
	// 本次伤害致死与否在钳制后即成事实，不受订阅方反应篡改
	const bool bLethal = CurrentHealth <= 0.f;
	// 位置与实际量在同步反伤/治疗/死亡回调之前成为值快照；UI不再读取会被回调改变的血量或模式。
	UCombatFeedbackSubsystem::ShowDamage(GetWorld(), Context, -Delta, SourceSnapshot);
	OnDamaged.Broadcast(this, Instigator);
	OnHealthChanged.Broadcast(this, CurrentHealth, Delta);

	if (bLethal)
	{
		bDead = true;
		OnDeath.Broadcast(this);
	}
}

void UHealthComponent::SetInvulnerable(float Duration)
{
	bInvulnerable = true;
	if (UWorld* World = GetWorld())
	{
		// 重复触发刷新定时器（ApplySlow 同范式——不叠加错乱）
		World->GetTimerManager().SetTimer(InvulnTimerHandle, this, &UHealthComponent::ExpireInvulnerability, Duration, /*bLoop*/ false);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Aegis] 圣盾无敌 %.0fs"), Duration);
}

void UHealthComponent::ExpireInvulnerability()
{
	bInvulnerable = false;
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Aegis] 圣盾到期，恢复受击"));
}

void UHealthComponent::ClearInvulnerable()
{
	// 主动提前解除（召唤师破盾：护盾从"事后回血抵消"迁入无敌早退通道后，破盾即清无敌）
	bInvulnerable = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InvulnTimerHandle);
	}
}

void UHealthComponent::ConfirmDeath()
{
	// 死亡链保险丝（幂等）：血量归零而 OnDeath 未走（任何竞态漏网）——调用方强制引爆标准死亡链。
	// bDead 置位后 ApplyDamage 早退、Heal 无效，死亡事实不可逆
	if (!bDead)
	{
		bDead = true;
		OnDeath.Broadcast(this);
	}
}

void UHealthComponent::Heal(float Amount)
{
	// 死者无效（蛋糕不救尸）与零值早退：与 ApplyDamage 的 bDead 闸门对称
	if (bDead || Amount <= 0.f)
	{
		return;
	}

	if (GetOwner() == UGameplayStatics::GetPlayerPawn(this, 0))
		if (const ACompanionAIController* Controller = Cast<ACompanionAIController>(
			UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass())))
			Amount *= Controller->GetPlayerHealingMultiplier();
	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + Amount, 0.f, MaxHealth);
	const float Delta = CurrentHealth - OldHealth;

	// 治疗意图广播（10-07 回响脉冲重做）：满血也照发——Attempted=施放量、Actual=钳制后实际回血量。
	// 血量事件（下方）保持满血静默的纪律：无变化的血量事件是噪声；"治疗意图"与"血量变化"是两种语义
	OnHealApplied.Broadcast(this, Amount, Delta);

	// 只广播血量事件（无受击凶手、绝不触发死亡）；Delta=0（满血吃的）不广播
	if (Delta > 0.f)
	{
		OnHealthChanged.Broadcast(this, CurrentHealth, Delta);
	}
}
