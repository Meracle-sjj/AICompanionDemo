/**
 * [INPUT]: 蠕虫真实入场/死亡/EndPlay，GameMode导演出口，Character镜头状态。
 * [OUTPUT]: 生命周期事件即刻刷新世界活虫压力；虫镜头旗只控制虫冲击资格，远景由Character世界活Boss统一计数，多家族宿主不提前恢复。
 * [POS]: BossWurm局部接线，不持有数量/镜头时钟；不修改Boss战四态数值与移动。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */
#include "AI/BossWurm.h"
#include "AI/WaveDirector.h"
#include "AICompanionDemoCharacter.h"
#include "AICompanionDemoGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

void ABossWurm::NotifyEncounterPresence(bool bLeaving)
{
	if (!GetWorld()) return;
	const AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	if (AWaveDirector* Director = GM ? GM->GetWaveDirector() : nullptr)
	{
		Director->RefreshWurmEncounter(bLeaving ? this : nullptr);
		return;
	}
	bool bAnyAlive = false;
	for (TActorIterator<ABossWurm> It(GetWorld()); It; ++It)
		if ((!bLeaving || *It != this) && !It->IsActorBeingDestroyed() && !It->IsDead()) { bAnyAlive = true; break; }
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->SetWurmEncounterActive(bAnyAlive);
}
