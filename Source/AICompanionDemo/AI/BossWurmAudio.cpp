/**
 * [INPUT]: 蠕虫真实Burrow状态/头部根组件、SFXLibrary独立钻岩循环接口、实际玩家与镜头位置、AudioComponent与世界暂停/有效流速。
 * [OUTPUT]: 地下3秒随头投影音源；玩家30m到4m近感0.7到1.5与进程0.85到1.0乘基准增益，约0.1s平滑；暂停冻结，出土/死亡/EndPlay停止销毁。
 * [POS]: AI/蠕虫音频生命周期；状态时钟仍归BossWurm，音频槽与素材归Audio公共入口。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "BossWurm.h"
#include "Audio/SFXLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// ---------- 玩家脚下接近感不随Boss拉远镜头改变；空间方向仍由真实声源衰减 ----------
	float BurrowDistanceFactor(float PlayerDistance)
	{
		const float Nearness = 1.f - FMath::Clamp((PlayerDistance - 400.f) / 2600.f, 0.f, 1.f);
		return .7f + .8f * Nearness;
	}

	float BurrowProgressFactor(float StateTime, float Duration)
	{
		return .85f + .15f * FMath::Clamp(StateTime / FMath::Max(Duration, .1f), 0.f, 1.f);
	}
}

void ABossWurm::RefreshBurrowAudio()
{
	if (!GetWorld() || !HealthComponent || HealthComponent->IsDead() || IsActorBeingDestroyed() || State != EWurmState::Burrow)
	{
		StopBurrowAudio();
		return;
	}
	const bool bPaused = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
	bool bStarted = false;
	if (!IsValid(BurrowAudio) && !bPaused)
	{
		BurrowAudio = USFXLibrary::StartWurmBurrowAudio(GetWorld(), GetRootComponent());
		if (IsValid(BurrowAudio))
		{
			// Actor根随地下头节移动；音源抬到表层，避免5米埋深吞掉迫近声。
			BurrowAudio->SetRelativeLocation(FVector(0, 0, BurrowDepth));
			++BurrowAudioStarts;
			bStarted = true;
		}
	}
	if (IsValid(BurrowAudio))
	{
		BurrowAudio->SetPaused(bPaused);
		bBurrowAudioPaused = bPaused;
		if (bPaused) return;
		const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
		const float PlayerDistance = Player ? FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) : 3000.f;
		const float DesiredVolume = FMath::Max(GetDefault<USFXLibrary>()->WurmBurrowVolume, 0.f)
			* BurrowDistanceFactor(PlayerDistance) * BurrowProgressFactor(StateTime, BurrowDuration);
		// 新组件同帧设真实起始增益，已有组件按游戏时钟平滑；暂停不追赶真实时间。
		BurrowAudio->SetVolumeMultiplier(bStarted ? DesiredVolume
			: FMath::FInterpTo(BurrowAudio->VolumeMultiplier, DesiredVolume, GetWorld()->GetDeltaSeconds(), 10.f));
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
	const APawn* Player = GetWorld() ? UGameplayStatics::GetPlayerPawn(this, 0) : nullptr;
	const APlayerCameraManager* Camera = GetWorld() ? UGameplayStatics::GetPlayerCameraManager(this, 0) : nullptr;
	const float PlayerDistance = Player ? FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) : -1.f;
	const float ListenerDistance = bExists && Camera ? FVector::Dist(Location, Camera->GetCameraLocation()) : -1.f;
	const float DistanceFactor = BurrowDistanceFactor(PlayerDistance >= 0.f ? PlayerDistance : 3000.f);
	const float ProgressFactor = BurrowProgressFactor(StateTime, BurrowDuration);
	const float DesiredVolume = FMath::Max(GetDefault<USFXLibrary>()->WurmBurrowVolume, 0.f) * DistanceFactor * ProgressFactor;
	return FString::Printf(TEXT("{\"state\":%d,\"stateTime\":%.4f,\"undergroundDuration\":%.3f,\"audioExists\":%s,\"audioPlaying\":%s,\"audioPaused\":%s,\"starts\":%d,\"stops\":%d,\"audioX\":%.3f,\"audioY\":%.3f,\"audioZ\":%.3f,\"headX\":%.3f,\"headY\":%.3f,\"playerDistance2d\":%.3f,\"listenerDistance3d\":%.3f,\"volumeMultiplier\":%.4f,\"desiredVolume\":%.4f,\"distanceFactor\":%.4f,\"progressFactor\":%.4f}"),
		static_cast<int32>(State), StateTime, BurrowDuration, bExists ? TEXT("true") : TEXT("false"),
		bExists && BurrowAudio->IsPlaying() ? TEXT("true") : TEXT("false"), bBurrowAudioPaused ? TEXT("true") : TEXT("false"),
		BurrowAudioStarts, BurrowAudioStops, Location.X, Location.Y, Location.Z, GetActorLocation().X, GetActorLocation().Y,
		PlayerDistance, ListenerDistance, bExists ? BurrowAudio->VolumeMultiplier : 0.f, DesiredVolume, DistanceFactor, ProgressFactor);
#else
	return TEXT("{}");
#endif
}
