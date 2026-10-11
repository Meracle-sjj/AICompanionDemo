/**
 * [INPUT]: 依赖 SFXLibrary.h 的槽位契约、Kismet/GameplayStatics 的 SpawnSound2D/SpawnSoundAtLocation、Engine/World、Engine/SoundAttenuation（运行时构造衰减——空间化语音零资产）
 * [OUTPUT]: 固定录音字幕/预算；妖术师短声交专用世界预算，铡刀/地下声与原BGM独立
 * [POS]: Audio槽位解析与播放分流；语音/妖术师交各世界所有者，其他事件保持原2D入口
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "Audio/SFXLibrary.h"
#include "Audio/CompanionVoiceSubsystem.h"
#include "Audio/WarlockAudioSubsystem.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

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
		case EGameSFX::Voice_PlayerAttackUnlock: Sound = Slots->Voice_PlayerAttackUnlock; Volume = Slots->Voice_PlayerAttackUnlock_Volume; break;
		case EGameSFX::Voice_BeaconRepeat: Sound = Slots->Voice_BeaconRepeat; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossSummonerAppear: Sound = Slots->Voice_BossSummonerAppear; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossSummonerDefeated: Sound = Slots->Voice_BossSummonerDefeated; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossGolemAppear: Sound = Slots->Voice_BossGolemAppear; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossGolemDefeated: Sound = Slots->Voice_BossGolemDefeated; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossWurmAppear: Sound = Slots->Voice_BossWurmAppear; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::Voice_BossWurmDefeated: Sound = Slots->Voice_BossWurmDefeated; Volume = Slots->EventVoiceVolume; break;
		case EGameSFX::WurmBurrowLoop: Sound = Slots->WurmBurrowLoop; Volume = Slots->WurmBurrowVolume; break;
		case EGameSFX::RelicStoneCleaver: Sound = Slots->RelicStoneCleaver; Volume = Slots->RelicStoneCleaverVolume; break;
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
		case EGameSFX::Voice_ChestOpenSecond: Sound = Slots->Voice_ChestOpenSecond; Volume = Slots->Voice_ChestOpenVolume; break;
		case EGameSFX::Voice_ChestOpenThird: Sound = Slots->Voice_ChestOpenThird; Volume = Slots->Voice_ChestOpenVolume; break;
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

	// 地下声单独覆盖远景镜头，语音原400cm内圈不受此调整影响。
	USoundAttenuation* WurmAttenuation()
	{
		static TStrongObjectPtr<USoundAttenuation> Holder;
		if (!Holder.IsValid())
		{
			USoundAttenuation* Att = NewObject<USoundAttenuation>(GetTransientPackage());
			Att->Attenuation = VoiceAttenuation()->Attenuation;
			Att->Attenuation.bAttenuate = true;
			Att->Attenuation.bSpatialize = true;
			Att->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
			Att->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
			Att->Attenuation.AttenuationShapeExtents = FVector(1200.f, 0.f, 0.f);
			Att->Attenuation.FalloffDistance = 4800.f;
			Holder = TStrongObjectPtr<USoundAttenuation>(Att);
		}
		return Holder.Get();
	}
}

void USFXLibrary::PlaySFX(UWorld* World, EGameSFX Event)
{
	if (!World)
	{
		return;
	}
	// 妖术师不能回退到全局2D叠响；真实来源由专用入口显式传入。
	if (Event == EGameSFX::WarlockBeamCharge || Event == EGameSFX::WarlockBeamFire) return;

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
		if (!GetVoiceLine(Event).IsEmpty())
		{
			if (UCompanionVoiceSubsystem* Queue = World->GetSubsystem<UCompanionVoiceSubsystem>())
				if (Queue->Enqueue(Loaded, Event, Volume, FVector::ZeroVector, false)) CommitPickupVoice(World, Event);
			return;
		}
		if (Volume > 0.f) UGameplayStatics::SpawnSound2D(World, Loaded, Volume);
	}
}

bool USFXLibrary::PlayWarlockSFX(UWorld* World, EGameSFX Event, AActor* Source, const FVector& Location)
{
	if (!World || (Event != EGameSFX::WarlockBeamCharge && Event != EGameSFX::WarlockBeamFire)) return false;
	const USFXLibrary* Slots = GetDefault<USFXLibrary>();
	TSoftObjectPtr<USoundBase> Sound;
	float Volume = 0.f;
	if (!ResolveSlot(Slots, Event, Sound, Volume) || Volume <= 0.f) return false;
	USoundBase* Loaded = Sound.LoadSynchronous();
	UWarlockAudioSubsystem* Budget = World->GetSubsystem<UWarlockAudioSubsystem>();
	const float Pitch = Event == EGameSFX::WarlockBeamCharge ? Slots->WarlockBeamChargePitch : Slots->WarlockBeamFirePitch;
	return Budget && Budget->Play(Loaded, Event, Volume, Pitch, Source, Location);
}

void USFXLibrary::PlayVoice(UWorld* World, EGameSFX Event, const FVector& Location)
{
	TryPlayVoice(World, Event, Location);
}

bool USFXLibrary::TryPlayVoice(UWorld* World, EGameSFX Event, const FVector& Location)
{
	if (!World || GetVoiceLine(Event).IsEmpty())
	{
		return false;
	}

	static const USFXLibrary* Slots = GetDefault<USFXLibrary>();
	TSoftObjectPtr<USoundBase> Sound;
	float Volume = 1.f;
	if (!ResolveSlot(Slots, Event, Sound, Volume))
	{
		return false;
	}

	if (USoundBase* Loaded = Sound.LoadSynchronous())
	{
		if (UCompanionVoiceSubsystem* Queue = World->GetSubsystem<UCompanionVoiceSubsystem>())
		{
			if (Queue->Enqueue(Loaded, Event, Volume, Location, true))
			{
				CommitPickupVoice(World, Event);
				return true;
			}
		}
	}
	return false;
}

bool USFXLibrary::IsInterruptingVoice(EGameSFX Event)
{
	return Event == EGameSFX::Voice_PlayerHurt || Event == EGameSFX::Voice_Recovered
		|| GetVoiceKind(Event) == ERunEventKind::BossAppear || GetVoiceKind(Event) == ERunEventKind::BossDefeated;
}

bool USFXLibrary::IsLimitedItemVoice(EGameSFX Event)
{
	return (Event >= EGameSFX::CardVoice_Blink && Event <= EGameSFX::CardVoice_RadianceBond)
		|| IsPickupVoice(Event) || Event == EGameSFX::Voice_BeaconRepeat;
}

void USFXLibrary::PlayChestOpenVoice(UWorld* World, const FVector& Location)
{
	if (!IsValid(World)) return;
	if (UCompanionVoiceSubsystem* Queue = World->GetSubsystem<UCompanionVoiceSubsystem>())
	{
		const EGameSFX Event = Queue->RegisterChestOpened();
		if (Event != EGameSFX::None) TryPlayVoice(World, Event, Location);
	}
}

float USFXLibrary::GetPickupVoiceCooldownRemaining(const UObject* WorldContextObject)
{
	return PickupVoiceRemaining(WorldContextObject ? WorldContextObject->GetWorld() : nullptr);
}

void USFXLibrary::PlayBGM(UWorld* World)
{
	static TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UAudioComponent>> LiveBGM;
	for (auto It = LiveBGM.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
	if (!World) return;
	TWeakObjectPtr<UAudioComponent>& Live = LiveBGM.FindOrAdd(World);
	if (Live.IsValid() && Live->IsPlaying()) return;
	if (Live.IsValid()) { Live->DestroyComponent(); Live.Reset(); }
	const USFXLibrary* Slots = GetDefault<USFXLibrary>();
	if (USoundBase* Loaded = Slots->BGM_Track.LoadSynchronous())
	{
		if (USoundWave* Track = Cast<USoundWave>(Loaded)) Track->bLooping = true;
		if (UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(World, Loaded, Slots->BGM_Volume))
		{
			Comp->bIsMusic = true;
			Live = Comp;
		}
	}
}

UAudioComponent* USFXLibrary::StartWurmBurrowAudio(UWorld* World, USceneComponent* AttachTo)
{
	if (!World || !IsValid(AttachTo) || AttachTo->GetWorld() != World) return nullptr;
	const USFXLibrary* Slots = GetDefault<USFXLibrary>();
	USoundBase* Sound = Slots->WurmBurrowLoop.LoadSynchronous();
	if (!Sound || Slots->WurmBurrowVolume <= 0.f) return nullptr;
	if (USoundWave* Loop = Cast<USoundWave>(Sound)) Loop->bLooping = true;
	return UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector,
		FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true, Slots->WurmBurrowVolume,
		1.f, 0.f, WurmAttenuation(), nullptr, false);
}

FName USFXLibrary::GetVoiceEventId(EGameSFX Event)
{
	if (GetVoiceLine(Event).IsEmpty()) return NAME_None;
	return FName(*StaticEnum<EGameSFX>()->GetNameStringByValue(static_cast<int64>(Event)));
}

ERunEventKind USFXLibrary::GetVoiceKind(EGameSFX Event)
{
	if (Event == EGameSFX::Voice_PlayerAttackUnlock) return ERunEventKind::AttackUnlock;
	if (Event == EGameSFX::Voice_BossSummonerAppear || Event == EGameSFX::Voice_BossGolemAppear
		|| Event == EGameSFX::Voice_BossWurmAppear) return ERunEventKind::BossAppear;
	if (Event == EGameSFX::Voice_BossSummonerDefeated || Event == EGameSFX::Voice_BossGolemDefeated
		|| Event == EGameSFX::Voice_BossWurmDefeated) return ERunEventKind::BossDefeated;
	return ERunEventKind::General;
}

FString USFXLibrary::GetVoiceLine(EGameSFX Event)
{
	switch (Event)
	{
	case EGameSFX::Voice_Discover: return TEXT("发现敌人，交给我！");
	case EGameSFX::Voice_Kill: return TEXT("威胁已清除。");
	case EGameSFX::Voice_Clear: return TEXT("威胁解除。");
	case EGameSFX::Voice_PlayerHurt: return TEXT("放开我的伙伴！");
	case EGameSFX::Voice_PlayerLow: return TEXT("你撑住，我顶上！");
	case EGameSFX::Voice_ModeGuard: return TEXT("护卫协议，掩护优先。");
	case EGameSFX::Voice_ModeBerserk: return TEXT("全部交给我！");
	case EGameSFX::Voice_Magnet: return TEXT("磁暴展开，战利品都过来！");
	case EGameSFX::Voice_Frenzy: return TEXT("火力全开，都交给我！");
	case EGameSFX::Voice_Aegis: return TEXT("圣盾加身，五秒无敌！");
	case EGameSFX::Voice_ChestOpen: return TEXT("是的，这个世界确实有一些尘封的武器。");
	case EGameSFX::Voice_ChestOpenSecond: return TEXT("继续找吧，还有很多。");
	case EGameSFX::Voice_ChestOpenThird: return TEXT("看来你已经知道了如何武装自己。");
	case EGameSFX::Voice_LockedChest: return TEXT("运气不错。");
	case EGameSFX::Voice_Recovered: return TEXT("生命体征恢复，很好。");
	case EGameSFX::Voice_TreasureKey: return TEXT("钥匙到手，找带锁的箱子。");
	case EGameSFX::Voice_BeaconActivate: return TEXT("我一直在你身边。");
	case EGameSFX::Voice_BeaconRecall: return TEXT("我从未离开过你");
	case EGameSFX::Voice_PlayerAttackUnlock: return TEXT("看来你找回了一些自己的能力");
	case EGameSFX::CardVoice_Blink: return TEXT("空间折叠，出发。");
	case EGameSFX::CardVoice_Scatter: return TEXT("现在可以攻击到更多敌人了。");
	case EGameSFX::CardVoice_Pierce: return TEXT("这一枪，谁也挡不住。");
	case EGameSFX::CardVoice_Prism: return TEXT("折射的光线，会替我们追击敌人。");
	case EGameSFX::CardVoice_RapidFire: return TEXT("射速提升，火力压制。");
	case EGameSFX::CardVoice_Heavy: return TEXT("每一发都更重了。");
	case EGameSFX::CardVoice_Explosive: return TEXT("让我们来制造一些爆炸。");
	case EGameSFX::CardVoice_Frost: return TEXT("把它们冻在原地。");
	case EGameSFX::CardVoice_Radiance: return TEXT("光之领域，随我展开。");
	case EGameSFX::CardVoice_BuddyHeal: return TEXT("靠近我，就能活下去。");
	case EGameSFX::CardVoice_HealNova: return TEXT("治愈之力，亦可杀敌。");
	case EGameSFX::CardVoice_GlassCannon: return TEXT("防御？我们不需要防御。");
	case EGameSFX::CardVoice_BloodSacrifice: return TEXT("力量，总有代价。");
	case EGameSFX::CardVoice_Vigor: return TEXT("结构强化完毕。");
	case EGameSFX::CardVoice_SwiftMove: return TEXT("移动模块，提速。");
	case EGameSFX::CardVoice_Barrage: return TEXT("弹幕覆盖，一个不留。");
	case EGameSFX::CardVoice_BlinkBomb: return TEXT("落点，即是爆点。");
	case EGameSFX::CardVoice_RadianceOverload: return TEXT("领域扩张至极限。");
	case EGameSFX::CardVoice_RadianceBond: return TEXT("光域之内，皆受庇护。");
	case EGameSFX::Voice_BeaconRepeat: return TEXT("我好像离你更近一些了。");
	case EGameSFX::Voice_BossSummonerAppear: return TEXT("抓住他幽紫结界消散的时机！");
	case EGameSFX::Voice_BossSummonerDefeated: return TEXT("结界碎了，召唤也结束了。");
	case EGameSFX::Voice_BossGolemAppear: return TEXT("可别在他狂怒的时候接近。");
	case EGameSFX::Voice_BossGolemDefeated: return TEXT("他的狂怒，到此为止。");
	case EGameSFX::Voice_BossWurmAppear: return TEXT("这就是最后的挑战了吗？");
	case EGameSFX::Voice_BossWurmDefeated: return TEXT("最后的挑战，也被我们跨过去了。");
	default: return FString();
	}
}
