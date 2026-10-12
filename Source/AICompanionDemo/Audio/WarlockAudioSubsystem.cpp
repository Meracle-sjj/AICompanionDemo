/**
 * [INPUT]: WarlockAudioSubsystem契约、玩家Pawn位置、ICombatant::IsDead、Kismet空间起播与组件真实状态。
 * [OUTPUT]: 两声共享2组件硬预算、.5/.2游戏秒成功CD、玩家1200+1200cm线性音量与原位空间方向及编辑器真实入口探针。
 * [POS]: Audio妖术师专用短声预算；危机距离不受Boss拉远镜头改变，成功组件不等于可听批准，不使用墙钟重触发或未播队列。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "Audio/WarlockAudioSubsystem.h"
#include "AI/Combatant.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr int32 MaxComponents = 2;
	constexpr double ChargeInterval = .5;
	constexpr double FireInterval = .2;
	constexpr float InnerDistance = 1200.f;
	constexpr float FalloffDistance = 1200.f;

	float PlayerDistanceGain(const APawn* Player, const FVector& Location)
	{
		if (!IsValid(Player) || Player->GetActorLocation().ContainsNaN()) return 0.f;
		const float Distance = FVector::Distance(Player->GetActorLocation(), Location);
		return 1.f - FMath::Clamp((Distance - InnerDistance) / FalloffDistance, 0.f, 1.f);
	}

	bool IsLiveSource(AActor* Source, const UWorld* World)
	{
		const ICombatant* Combatant = IsValid(Source) ? Cast<ICombatant>(Source) : nullptr;
		return Combatant && Source->GetWorld() == World && !Source->IsActorBeingDestroyed() && !Combatant->IsDead();
	}

	void StopInstance(UAudioComponent* Audio)
	{
		if (!IsValid(Audio)) return;
		Audio->Stop();
		Audio->DestroyComponent();
	}
}

bool UWarlockAudioSubsystem::IsFrozen() const
{
	const UWorld* World = GetWorld();
	return !World || World->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f;
}

void UWarlockAudioSubsystem::PruneActive()
{
	for (int32 Index = Active.Num() - 1; Index >= 0; --Index)
	{
		const FWarlockSoundInstance& Item = Active[Index];
		if (!IsLiveSource(Item.Source.Get(), GetWorld()))
		{
			StopInstance(Item.Audio);
			++StoppedSourceCount;
			Active.RemoveAt(Index);
		}
		else if (!IsValid(Item.Audio) || !Item.Audio->IsActive())
		{
			// 音频线程拒绝不一定发Finished委托，轮询同样释放预算；不把它计成人耳成功。
			StopInstance(Item.Audio);
			++FinishedComponentCount;
			Active.RemoveAt(Index);
		}
	}
}

bool UWarlockAudioSubsystem::Play(USoundBase* Sound, EGameSFX Event, float Volume, float Pitch,
	AActor* Source, const FVector& Location)
{
	check(IsInGameThread());
	++RequestedCount;
	if (Event != EGameSFX::WarlockBeamCharge && Event != EGameSFX::WarlockBeamFire) return false;
	if (!IsLiveSource(Source, GetWorld())) { ++RejectedSourceCount; return false; }
	PruneActive();
	if (IsFrozen()) { ++RejectedFrozenCount; return false; }
	if (!IsValid(Sound) || !FMath::IsFinite(Volume) || !FMath::IsFinite(Pitch) || Volume <= 0.f || Pitch <= 0.f
		|| Location.ContainsNaN()) { ++FailedSpawnCount; return false; }
	// ---------- 距离听玩家，方向听相机 ----------
	// 拉远Boss镜头只改变取景，不能使玩家仍在22米攻击窗口内的危机声被相机距离吞掉。
	// 手算一次玩家距离音量；引擎只保留原音源空间方向，避免第二次相机距离衰减。
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(Camera) || Camera->GetCameraLocation().ContainsNaN()
		|| !IsValid(Player) || Player->GetActorLocation().ContainsNaN()) { ++RejectedListenerCount; return false; }
	const float DistanceGain = PlayerDistanceGain(Player, Location);
	if (DistanceGain <= 0.f)
	{
		++RejectedDistanceCount;
		return false;
	}
	double& ReadyAt = Event == EGameSFX::WarlockBeamCharge ? ChargeReadyAt : FireReadyAt;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < ReadyAt) { ++RejectedCooldownCount; return false; }
	if (Active.Num() >= MaxComponents) { ++RejectedBudgetCount; return false; }
	if (!Attenuation)
	{
		Attenuation = NewObject<USoundAttenuation>(this);
		Attenuation->Attenuation.bAttenuate = false;
		Attenuation->Attenuation.bSpatialize = true;
		Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(InnerDistance, 0.f, 0.f);
		Attenuation->Attenuation.FalloffDistance = FalloffDistance;
	}
	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Sound, Location,
		FRotator::ZeroRotator, Volume * DistanceGain, Pitch, 0.f, Attenuation, nullptr, false);
	if (!IsValid(Audio) || !Audio->IsActive())
	{
		StopInstance(Audio);
		++FailedSpawnCount;
		return false;
	}
	FWarlockSoundInstance& Item = Active.AddDefaulted_GetRef();
	Item.Audio = Audio;
	Item.Source = Source;
	Item.Event = Event;
	Item.BaseVolume = Volume;
	ReadyAt = Now + (Event == EGameSFX::WarlockBeamCharge ? ChargeInterval : FireInterval);
	++StartedComponentCount;
	MaxObservedComponents = FMath::Max(MaxObservedComponents, Active.Num());
	return true;
}

void UWarlockAudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PruneActive();
	const bool bFrozen = IsFrozen();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	for (const FWarlockSoundInstance& Item : Active)
	{
		if (!IsValid(Item.Audio)) continue;
		Item.Audio->SetPaused(bFrozen);
		if (!bFrozen)
			Item.Audio->SetVolumeMultiplier(Item.BaseVolume * PlayerDistanceGain(Player, Item.Audio->GetComponentLocation()));
	}
}

bool UWarlockAudioSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UWarlockAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWarlockAudioSubsystem, STATGROUP_Tickables);
}

void UWarlockAudioSubsystem::Deinitialize()
{
	for (const FWarlockSoundInstance& Item : Active) StopInstance(Item.Audio);
	Active.Reset();
	Attenuation = nullptr;
	Super::Deinitialize();
}

bool UWarlockAudioSubsystem::DebugTryWarlockSFX(AActor* Source, bool bFire)
{
#if WITH_EDITOR
	// 仅转生产入口，不造测试组件或延长短声；正式构建保留反射签名但拒绝执行。
	if (!IsValid(Source)) return false;
	const FVector Location = Source->GetActorLocation() + FVector(0.f, 0.f, 140.f * Source->GetActorScale3D().Z);
	return USFXLibrary::PlayWarlockSFX(GetWorld(), bFire ? EGameSFX::WarlockBeamFire : EGameSFX::WarlockBeamCharge,
		Source, Location);
#else
	return false;
#endif
}

FString UWarlockAudioSubsystem::DebugWarlockAudioState() const
{
	const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetNumberField(TEXT("requested"), RequestedCount);
	State->SetNumberField(TEXT("started_components"), StartedComponentCount);
	State->SetNumberField(TEXT("rejected_frozen"), RejectedFrozenCount);
	State->SetNumberField(TEXT("rejected_budget"), RejectedBudgetCount);
	State->SetNumberField(TEXT("rejected_cooldown"), RejectedCooldownCount);
	State->SetNumberField(TEXT("rejected_source"), RejectedSourceCount);
	State->SetNumberField(TEXT("rejected_distance"), RejectedDistanceCount);
	State->SetNumberField(TEXT("rejected_listener"), RejectedListenerCount);
	State->SetNumberField(TEXT("failed_spawn"), FailedSpawnCount);
	State->SetNumberField(TEXT("finished_components"), FinishedComponentCount);
	State->SetNumberField(TEXT("stopped_source"), StoppedSourceCount);
	State->SetNumberField(TEXT("max_observed_components"), MaxObservedComponents);
	State->SetNumberField(TEXT("active_components"), Active.Num());
	State->SetNumberField(TEXT("inner_distance"), InnerDistance);
	State->SetNumberField(TEXT("falloff_distance"), FalloffDistance);
	State->SetNumberField(TEXT("max_components"), MaxComponents);
	State->SetStringField(TEXT("distance_listener"), TEXT("player_pawn"));
	State->SetBoolField(TEXT("spatialized"), Attenuation && Attenuation->Attenuation.bSpatialize);
	State->SetBoolField(TEXT("engine_distance_attenuation"), Attenuation && Attenuation->Attenuation.bAttenuate);
	State->SetBoolField(TEXT("frozen"), IsFrozen());
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	State->SetNumberField(TEXT("charge_cooldown_remaining"), FMath::Max(0.0, ChargeReadyAt - Now));
	State->SetNumberField(TEXT("fire_cooldown_remaining"), FMath::Max(0.0, FireReadyAt - Now));
	TArray<TSharedPtr<FJsonValue>> Instances;
	for (const FWarlockSoundInstance& Item : Active)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("source"), GetNameSafe(Item.Source.Get()));
		Entry->SetBoolField(TEXT("source_alive"), IsLiveSource(Item.Source.Get(), GetWorld()));
		Entry->SetBoolField(TEXT("audio_exists"), IsValid(Item.Audio));
		Entry->SetBoolField(TEXT("active"), IsValid(Item.Audio) && Item.Audio->IsActive());
		Entry->SetBoolField(TEXT("playing"), IsValid(Item.Audio) && Item.Audio->IsPlaying());
		Entry->SetBoolField(TEXT("paused"), IsValid(Item.Audio) && Item.Audio->bIsPaused);
		Entry->SetStringField(TEXT("event"), Item.Event == EGameSFX::WarlockBeamCharge ? TEXT("charge") : TEXT("fire"));
		if (IsValid(Item.Audio))
		{
			const FVector Location = Item.Audio->GetComponentLocation();
			Entry->SetStringField(TEXT("sound"), GetPathNameSafe(Item.Audio->Sound));
			Entry->SetStringField(TEXT("audio"), Item.Audio->GetName());
			Entry->SetNumberField(TEXT("volume"), Item.Audio->VolumeMultiplier);
			Entry->SetNumberField(TEXT("base_volume"), Item.BaseVolume);
			Entry->SetNumberField(TEXT("distance_gain"), Item.BaseVolume > 0.f ? Item.Audio->VolumeMultiplier / Item.BaseVolume : 0.f);
			Entry->SetNumberField(TEXT("player_distance"), IsValid(Player) ? FVector::Distance(Player->GetActorLocation(), Location) : -1.f);
			Entry->SetNumberField(TEXT("camera_distance"), IsValid(Camera) ? FVector::Distance(Camera->GetCameraLocation(), Location) : -1.f);
			Entry->SetNumberField(TEXT("pitch"), Item.Audio->PitchMultiplier);
			Entry->SetNumberField(TEXT("x"), Item.Audio->GetComponentLocation().X);
			Entry->SetNumberField(TEXT("y"), Item.Audio->GetComponentLocation().Y);
			Entry->SetNumberField(TEXT("z"), Item.Audio->GetComponentLocation().Z);
		}
		Instances.Add(MakeShared<FJsonValueObject>(Entry));
	}
	State->SetArrayField(TEXT("instances"), Instances);
	FString Result;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(State, Writer);
	return Result;
}
