/**
 * [INPUT]: 蠕虫真实Burrow状态/头部根组件、SFXLibrary钻岩循环接口、AudioComponent与世界暂停/有效流速。
 * [OUTPUT]: 地下3秒随头投影音源的空间隆隆声；暂停/奖励零流速暂停，露头/死亡/EndPlay同步Stop+Destroy；实际播放/计数只读回读。
 * [POS]: AI/蠕虫音频生命周期；状态时钟仍归BossWurm，音频槽与素材归Audio公共入口。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "BossWurm.h"
#include "Audio/SFXLibrary.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"

void ABossWurm::RefreshBurrowAudio()
{
	if (!GetWorld() || !HealthComponent || HealthComponent->IsDead() || IsActorBeingDestroyed() || State != EWurmState::Burrow)
	{
		StopBurrowAudio();
		return;
	}
	const bool bPaused = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
	if (!IsValid(BurrowAudio) && !bPaused)
	{
		BurrowAudio = USFXLibrary::StartWurmBurrowAudio(GetWorld(), GetRootComponent());
		if (IsValid(BurrowAudio))
		{
			// Actor根随地下头节移动；音源抬到表层，避免5米埋深吞掉迫近声。
			BurrowAudio->SetRelativeLocation(FVector(0, 0, BurrowDepth));
			++BurrowAudioStarts;
		}
	}
	if (IsValid(BurrowAudio))
	{
		BurrowAudio->SetPaused(bPaused);
		bBurrowAudioPaused = bPaused;
	}
}

void ABossWurm::StopBurrowAudio()
{
	if (IsValid(BurrowAudio))
	{
		UAudioComponent* Component = BurrowAudio;
		BurrowAudio = nullptr;
		Component->Stop();
		if (IsValid(Component)) Component->DestroyComponent();
		++BurrowAudioStops;
	}
	else BurrowAudio = nullptr;
	bBurrowAudioPaused = false;
}

FString ABossWurm::DebugBurrowAudioState() const
{
#if WITH_EDITOR
	const bool bExists = IsValid(BurrowAudio);
	const FVector Location = bExists ? BurrowAudio->GetComponentLocation() : FVector::ZeroVector;
	return FString::Printf(TEXT("{\"state\":%d,\"stateTime\":%.4f,\"undergroundDuration\":%.3f,\"audioExists\":%s,\"audioPlaying\":%s,\"audioPaused\":%s,\"starts\":%d,\"stops\":%d,\"audioX\":%.3f,\"audioY\":%.3f,\"audioZ\":%.3f,\"headX\":%.3f,\"headY\":%.3f}"),
		static_cast<int32>(State), StateTime, BurrowDuration, bExists ? TEXT("true") : TEXT("false"),
		bExists && BurrowAudio->IsPlaying() ? TEXT("true") : TEXT("false"), bBurrowAudioPaused ? TEXT("true") : TEXT("false"),
		BurrowAudioStarts, BurrowAudioStops, Location.X, Location.Y, Location.Z, GetActorLocation().X, GetActorLocation().Y);
#else
	return TEXT("{}");
#endif
}
