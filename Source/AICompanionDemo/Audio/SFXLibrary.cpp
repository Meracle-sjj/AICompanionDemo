/**
 * [INPUT]: 依赖 SFXLibrary.h 的槽位契约、Kismet/GameplayStatics 的 SpawnSound2D/SpawnSoundAtLocation、Engine/World、Engine/SoundAttenuation（运行时构造衰减——空间化语音零资产）
 * [OUTPUT]: PlaySFX/PlayVoice共用槽位解析与拾取语音5秒冷却；弱世界键隔离不同局，仅成功创建播放组件才占CD；BossImpact按独立Config采样和音量播放，普通爆炸槽保持；提供冷却只读出口
 * [POS]: Audio/ 的音效基建实现文件，与 SFXLibrary.h 一体两面；业务文件只认两个静态入口，不感知本文件存在
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "Audio/SFXLibrary.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	// 只存弱世界键和游戏时间，不持有世界；后续调用清走已结束世界，PIE/重开互不继承CD。
	TMap<TWeakObjectPtr<UWorld>, double> PickupVoiceReadyAt;

	bool IsPickupVoice(const EGameSFX Event)
	{
		return Event == EGameSFX::Voice_Magnet || Event == EGameSFX::Voice_Frenzy
			|| Event == EGameSFX::Voice_Aegis || Event == EGameSFX::Voice_TreasureKey
			|| Event == EGameSFX::Voice_BeaconActivate;
	}

	float PickupVoiceRemaining(UWorld* World)
	{
		for (auto It = PickupVoiceReadyAt.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid()) It.RemoveCurrent();
		}
		if (!World) return 0.f;
		const double* ReadyAt = PickupVoiceReadyAt.Find(TWeakObjectPtr<UWorld>(World));
		return ReadyAt ? static_cast<float>(FMath::Max(0.0, *ReadyAt - World->GetTimeSeconds())) : 0.f;
	}

	void CommitPickupVoice(UWorld* World, const EGameSFX Event)
	{
		if (IsPickupVoice(Event))
		{
			PickupVoiceReadyAt.Add(World, World->GetTimeSeconds()
				+ FMath::Max(0.f, GetDefault<USFXLibrary>()->PickupVoiceCooldownSeconds));
		}
	}

	// 槽位解析单点真源（DRY）：PlaySFX/PlayVoice 共用——枚举→CDO 槽位+音量
	bool ResolveSlot(const USFXLibrary* Slots, const EGameSFX Event, TSoftObjectPtr<USoundBase>& Sound, float& Volume)
	{
		switch (Event)
		{
		case EGameSFX::Voice_BeaconActivate: Sound = Slots->Voice_BeaconActivate; Volume = Slots->Voice_BeaconActivateVolume; break;
		case EGameSFX::Voice_BeaconRecall: Sound = Slots->Voice_BeaconRecall; Volume = Slots->Voice_BeaconRecallVolume; break;
		case EGameSFX::Shot_Companion:   Sound = Slots->Shot_Companion;      Volume = Slots->Shot_CompanionVolume;    break;
		case EGameSFX::Hit_Enemy:        Sound = Slots->Hit_Enemy;           Volume = Slots->Hit_EnemyVolume;         break;
		case EGameSFX::EnemyDie:         Sound = Slots->EnemyDie;            Volume = Slots->EnemyDieVolume;          break;
		case EGameSFX::PlayerHurt:       Sound = Slots->PlayerHurt;          Volume = Slots->PlayerHurtVolume;        break;
		case EGameSFX::Explosion:        Sound = Slots->Explosion;           Volume = Slots->ExplosionVolume;         break;
		case EGameSFX::BossImpact:       Sound = Slots->BossImpact;          Volume = Slots->BossImpactVolume;        break;
		case EGameSFX::Pickup_Xp:        Sound = Slots->Pickup_Xp;           Volume = Slots->Pickup_XpVolume;         break;
		case EGameSFX::Pickup_Gold:      Sound = Slots->Pickup_Gold;         Volume = Slots->Pickup_GoldVolume;       break;
		case EGameSFX::Pickup_Heal:      Sound = Slots->Pickup_Heal;         Volume = Slots->Pickup_HealVolume;       break;
		case EGameSFX::Pickup_Rush:      Sound = Slots->Pickup_Rush;         Volume = Slots->Pickup_RushVolume;       break;
		case EGameSFX::UpgradeChoice:    Sound = Slots->UpgradeChoice;       Volume = Slots->UpgradeChoiceVolume;     break;
		case EGameSFX::ChestOpen:        Sound = Slots->ChestOpen;           Volume = Slots->ChestOpenVolume;         break;
		case EGameSFX::Blink:            Sound = Slots->Blink;               Volume = Slots->BlinkVolume;             break;
		case EGameSFX::Recall:           Sound = Slots->Recall;              Volume = Slots->RecallVolume;            break;
		case EGameSFX::WarlockBeamCharge: Sound = Slots->WarlockBeamCharge;  Volume = Slots->WarlockBeamChargeVolume; break;
		case EGameSFX::WarlockBeamFire:  Sound = Slots->WarlockBeamFire;     Volume = Slots->WarlockBeamFireVolume;   break;
		case EGameSFX::Voice_Discover:   Sound = Slots->Voice_Discover;      Volume = Slots->Voice_DiscoverVolume;    break;
		case EGameSFX::Voice_Kill:       Sound = Slots->Voice_Kill;          Volume = Slots->Voice_KillVolume;        break;
		case EGameSFX::Voice_Clear:      Sound = Slots->Voice_Clear;         Volume = Slots->Voice_ClearVolume;       break;
		case EGameSFX::Voice_PlayerHurt: Sound = Slots->Voice_PlayerHurt;    Volume = Slots->Voice_PlayerHurtVolume;  break;
		case EGameSFX::Voice_PlayerLow:  Sound = Slots->Voice_PlayerLow;     Volume = Slots->Voice_PlayerLowVolume;   break;
		case EGameSFX::Voice_ModeGuard:  Sound = Slots->Voice_ModeGuard;     Volume = Slots->Voice_ModeGuardVolume;   break;
		case EGameSFX::Voice_ModeBerserk: Sound = Slots->Voice_ModeBerserk;  Volume = Slots->Voice_ModeBerserkVolume; break;
		case EGameSFX::Voice_Magnet:     Sound = Slots->Voice_Magnet;       Volume = Slots->Voice_MagnetVolume;      break;
		case EGameSFX::Voice_Frenzy:     Sound = Slots->Voice_Frenzy;       Volume = Slots->Voice_FrenzyVolume;      break;
		case EGameSFX::Voice_Aegis:      Sound = Slots->Voice_Aegis;        Volume = Slots->Voice_AegisVolume;       break;
		case EGameSFX::CardVoice_Blink:           Sound = Slots->CardVoice_Blink;           Volume = 1.f; break;
		case EGameSFX::CardVoice_Scatter:         Sound = Slots->CardVoice_Scatter;         Volume = 1.f; break;
		case EGameSFX::CardVoice_Pierce:          Sound = Slots->CardVoice_Pierce;          Volume = 1.f; break;
		case EGameSFX::CardVoice_Prism:           Sound = Slots->CardVoice_Prism;           Volume = 1.f; break;
		case EGameSFX::CardVoice_RapidFire:       Sound = Slots->CardVoice_RapidFire;       Volume = 1.f; break;
		case EGameSFX::CardVoice_Heavy:           Sound = Slots->CardVoice_Heavy;           Volume = 1.f; break;
		case EGameSFX::CardVoice_Explosive:       Sound = Slots->CardVoice_Explosive;       Volume = 1.f; break;
		case EGameSFX::CardVoice_Frost:           Sound = Slots->CardVoice_Frost;           Volume = 1.f; break;
		case EGameSFX::CardVoice_Radiance:        Sound = Slots->CardVoice_Radiance;        Volume = 1.f; break;
		case EGameSFX::CardVoice_BuddyHeal:       Sound = Slots->CardVoice_BuddyHeal;       Volume = 1.f; break;
		case EGameSFX::CardVoice_HealNova:        Sound = Slots->CardVoice_HealNova;        Volume = 1.f; break;
		case EGameSFX::CardVoice_GlassCannon:     Sound = Slots->CardVoice_GlassCannon;     Volume = 1.f; break;
		case EGameSFX::CardVoice_BloodSacrifice:  Sound = Slots->CardVoice_BloodSacrifice;  Volume = 1.f; break;
		case EGameSFX::CardVoice_Vigor:           Sound = Slots->CardVoice_Vigor;           Volume = 1.f; break;
		case EGameSFX::CardVoice_SwiftMove:       Sound = Slots->CardVoice_SwiftMove;       Volume = 1.f; break;
		case EGameSFX::CardVoice_Barrage:         Sound = Slots->CardVoice_Barrage;         Volume = 1.f; break;
		case EGameSFX::CardVoice_BlinkBomb:       Sound = Slots->CardVoice_BlinkBomb;       Volume = 1.f; break;
		case EGameSFX::CardVoice_RadianceOverload: Sound = Slots->CardVoice_RadianceOverload; Volume = 1.f; break;
		case EGameSFX::CardVoice_RadianceBond:    Sound = Slots->CardVoice_RadianceBond;    Volume = 1.f; break;
		case EGameSFX::Pickup_Magnet:    Sound = Slots->Pickup_Magnet;       Volume = Slots->Pickup_MagnetVolume;     break;
		case EGameSFX::HealNovaBurst:    Sound = Slots->HealNovaBurst;       Volume = Slots->HealNovaBurstVolume;     break;
		case EGameSFX::Voice_ChestOpen:  Sound = Slots->Voice_ChestOpen;     Volume = Slots->Voice_ChestOpenVolume;   break;
		case EGameSFX::Voice_LockedChest: Sound = Slots->Voice_LockedChest;  Volume = Slots->Voice_LockedChestVolume; break;
		case EGameSFX::Voice_Recovered:  Sound = Slots->Voice_Recovered;     Volume = Slots->Voice_RecoveredVolume;   break;
		case EGameSFX::Voice_TreasureKey: Sound = Slots->Voice_TreasureKey;  Volume = Slots->Voice_TreasureKeyVolume; break;
		case EGameSFX::BossWarn:         Sound = Slots->BossWarn;            Volume = Slots->BossWarnVolume;         break;
		default:                         return false;
		}
		return Sound.ToSoftObjectPath().IsValid();
	}

	/** 空间化衰减配置（运行时构造 transient 单例，零资产）：400cm 内全响，线性衰减至 4000cm 静默。
	 *  09-27 崩溃案定罪：static 裸指针不阻止 GC——GC 回收后用悬空对象触发 IsValidIndex(-1) 断言；
	 *  TStrongObjectPtr 挂引用阻止回收（游戏生命周期常驻） */
	USoundAttenuation* VoiceAttenuation()
	{
		static TStrongObjectPtr<USoundAttenuation> AttHolder;
		if (!AttHolder.IsValid())
		{
			USoundAttenuation* Att = NewObject<USoundAttenuation>(GetTransientPackage());
			Att->Attenuation.bAttenuate = true;
			Att->Attenuation.bSpatialize = true;
			Att->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
			Att->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
			Att->Attenuation.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);   // Sphere: X=内圈半径
			Att->Attenuation.FalloffDistance = 3600.f;                             // 400+3600 = 4000cm 全静默
			AttHolder = TStrongObjectPtr<USoundAttenuation>(Att);
		}
		return AttHolder.Get();
	}
}

void USFXLibrary::PlaySFX(UWorld* World, EGameSFX Event)
{
	if (!World || (IsPickupVoice(Event) && PickupVoiceRemaining(World) > 0.f))
	{
		return;
	}

	// CDO 缓存：Config 槽位常驻 CDO，读它零成本（不做实例/生命周期管理——演示项目 KISS）
	static const USFXLibrary* Slots = GetDefault<USFXLibrary>();

	TSoftObjectPtr<USoundBase> Sound;
	float Volume = 1.f;
	if (!ResolveSlot(Slots, Event, Sound, Volume))
	{
		return;   // 空槽静默：零资产是常态，绝不报错刷日志
	}

	if (USoundBase* Loaded = Sound.LoadSynchronous())
	{
		if (Volume > 0.f && UGameplayStatics::SpawnSound2D(World, Loaded, Volume))
		{
			CommitPickupVoice(World, Event);
		}
	}
}

void USFXLibrary::PlayVoice(UWorld* World, EGameSFX Event, const FVector& Location)
{
	if (!World || (IsPickupVoice(Event) && PickupVoiceRemaining(World) > 0.f))
	{
		return;
	}

	static const USFXLibrary* Slots = GetDefault<USFXLibrary>();
	TSoftObjectPtr<USoundBase> Sound;
	float Volume = 1.f;
	if (!ResolveSlot(Slots, Event, Sound, Volume))
	{
		return;
	}

	if (USoundBase* Loaded = Sound.LoadSynchronous())
	{
		if (Volume > 0.f && UGameplayStatics::SpawnSoundAtLocation(World, Loaded, Location, FRotator::ZeroRotator,
			Volume, 1.f, 0.f, VoiceAttenuation(), nullptr, true))
		{
			CommitPickupVoice(World, Event);
		}
	}
}

float USFXLibrary::GetPickupVoiceCooldownRemaining(const UObject* WorldContextObject)
{
	return PickupVoiceRemaining(WorldContextObject ? WorldContextObject->GetWorld() : nullptr);
}

void USFXLibrary::PlayBGM(UWorld* World)
{
	// 循环起播（幂等：静态组件指针在播即跳过）；槽空静默
	static TWeakObjectPtr<UAudioComponent> LiveBGM;
	if (World && !LiveBGM.IsValid())
	{
		const USFXLibrary* Slots = GetDefault<USFXLibrary>();
		if (Slots->BGM_Track.ToSoftObjectPath().IsValid())
		{
			if (USoundBase* Loaded = Slots->BGM_Track.LoadSynchronous())
			{
				if (UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(World, Loaded, Slots->BGM_Volume))
				{
					Comp->bIsMusic = true;
					LiveBGM = Comp;
				}
			}
		}
	}
}
