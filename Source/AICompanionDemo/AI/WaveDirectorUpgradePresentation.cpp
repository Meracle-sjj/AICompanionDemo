/**
 * [INPUT]: 真实升级参数/已获次数/固定语音字幕与展示枚举。
 * [OUTPUT]: 稳定ID、实际说明、稀有度/融合标志、HUD快照与成功队友加速卡的独立录音路由。
 * [POS]: AI/WaveDirector的按职责局部实现；机械拆分不改行为。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#include "WaveDirector.h"
#include "Relics/BossRelicComponent.h"
#include "AI/BossAltar.h"
#include "AI/BossBase.h"
#include "AI/BossColossus.h"
#include "AI/BossSummoner.h"
#include "AI/BossWurm.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionCharacter.h"
#include "AI/Combatant.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/RunEventLog.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UI/UpgradeCardView.h"
#include "Engine/GameViewportClient.h"
#include "WaveDirectorPrivate.h"

using namespace WaveDirectorPrivate;

// ---------- 稳定ID、实际说明、稀有度/融合标志和HUD快照。 ----------

FName AWaveDirector::UpgradeCardKey(const EUpgradeId Id)
{
	switch (Id)
	{
	case EUpgradeId::Blink: return TEXT("Blink");
	case EUpgradeId::Scatter: return TEXT("Scatter");
	case EUpgradeId::PierceRounds: return TEXT("PierceRounds");
	case EUpgradeId::RapidFire: return TEXT("RapidFire");
	case EUpgradeId::HeavyRounds: return TEXT("HeavyRounds");
	case EUpgradeId::Radiance: return TEXT("Radiance");
	case EUpgradeId::BuddyHealing: return TEXT("BuddyHealing");
	case EUpgradeId::GlassCannon: return TEXT("GlassCannon");
	case EUpgradeId::BloodSacrifice: return TEXT("BloodSacrifice");
	case EUpgradeId::Vigor: return TEXT("Vigor");
	case EUpgradeId::Barrage: return TEXT("Barrage");
	case EUpgradeId::BlinkBomb: return TEXT("BlinkBomb");
	case EUpgradeId::RadianceOverload: return TEXT("RadianceOverload");
	case EUpgradeId::PrismRounds: return TEXT("PrismRounds");
	case EUpgradeId::SwiftMove: return TEXT("SwiftMove");
	case EUpgradeId::ExplosiveRounds: return TEXT("ExplosiveRounds");
	case EUpgradeId::FrostRounds: return TEXT("FrostRounds");
	case EUpgradeId::RadianceBond: return TEXT("RadianceBond");
	case EUpgradeId::HealNova: return TEXT("HealNova");
	case EUpgradeId::FrostShotPlayer: return TEXT("FrostShotPlayer");
	case EUpgradeId::ShockShot: return TEXT("ShockShot");
	case EUpgradeId::HuntMark: return TEXT("HuntMark");
	case EUpgradeId::ShockOverload: return TEXT("ShockOverload");
	case EUpgradeId::MarkMaster: return TEXT("MarkMaster");
	case EUpgradeId::FrostDeep: return TEXT("FrostDeep");
	case EUpgradeId::PlayerScatter: return TEXT("PlayerScatter");
	case EUpgradeId::PlayerPierce: return TEXT("PlayerPierce");
	case EUpgradeId::PlayerPrism: return TEXT("PlayerPrism");
	case EUpgradeId::GoldPouch: return TEXT("GoldPouch");
	case EUpgradeId::PlayerRapidFire: return TEXT("PlayerRapidFire");
	case EUpgradeId::CompanionSwiftMove: return TEXT("CompanionSwiftMove");
	case EUpgradeId::Flip: return TEXT("Flip");
	}
	return NAME_None;
}

FString AWaveDirector::UpgradeCardDescription(const EUpgradeId Id) const
{
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ACompanionAIController* Ctl = CompanionController.Get();
	if (!Ctl) { Ctl = Cast<ACompanionAIController>(UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass())); }
	const int32 MarkCap = Hero ? Hero->HuntMarkMaxStacks : 3;
	const float ExecuteDamage = Ctl ? Ctl->ExecuteDamagePerMark : 15.f;
	if (Id == EUpgradeId::HuntMark)
	{
		const bool bOwned = Hero && Hero->bHuntMarkUnlocked;
		return bOwned ? FString::Printf(TEXT("已获%d次 · 上限%d→%d层\n再抽+1层，持续成长\n队友处决%.0f伤/层"),
			CardTakenCounts.FindRef(Id), MarkCap, MarkCap + 1, ExecuteDamage)
			: FString::Printf(TEXT("首次解锁 · 上限%d层\n你叠印，队友引爆\n每层处决%.0f伤"), MarkCap, ExecuteDamage);
	}
	if (Id == EUpgradeId::MarkMaster)
	{
		return FString::Printf(TEXT("上限%d→%d层，不降低\n处决%.0f→22伤/层\n保留普通猎印成长"), MarkCap, FMath::Max(MarkCap, 5), ExecuteDamage);
	}
	if (Id == EUpgradeId::PlayerRapidFire && Hero)
	{
		return FString::Printf(TEXT("射击间隔%.3f→%.3fs\n每次×0.85，最低0.15s\n已获%d次，到限离池"), Hero->PlayerShotInterval,
			FMath::Max(Hero->PlayerShotInterval * kPlayerAttackIntervalFactor, kPlayerAttackIntervalFloor), CardTakenCounts.FindRef(Id));
	}
	if (Id == EUpgradeId::CompanionSwiftMove && Hero)
	{
		const ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
		if (Buddy && Buddy->GetCharacterMovement() && Hero->GetCharacterMovement())
		{
			const float CurrentSpeed = Buddy->GetCharacterMovement()->MaxWalkSpeed;
			return FString::Printf(TEXT("队友%.0f→%.0fcm/s\n已获%d/3次，无收益离池\n最高不超过玩家步速"), CurrentSpeed,
				FMath::Min(CurrentSpeed * kCompanionMoveFactor, Hero->GetCharacterMovement()->MaxWalkSpeed), CardTakenCounts.FindRef(Id));
		}
	}
	// 叠加卡显示本次收益，避免再次抽到时仍写首层基线；完整说明继续复用DescribeCard。
	if (Id == EUpgradeId::Radiance && bRadianceUnlocked)
	{
		return TEXT("辉耀半径增加 1m\n每0.5s灼烧增加10伤。");
	}
	if (Id == EUpgradeId::BuddyHealing && bBondHealUnlocked)
	{
		return TEXT("每拍治疗量增加 1\n范围内双方持续回血。");
	}
	return FString();
}

FString AWaveDirector::AcquiredCardDescription(const EUpgradeId Id) const
{
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ACompanionAIController* Ctl = CompanionController.Get();
	if (!Ctl) { Ctl = Cast<ACompanionAIController>(UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass())); }
	const int32 MarkCap = Hero ? Hero->HuntMarkMaxStacks : 3;
	const float ExecuteDamage = Ctl ? Ctl->ExecuteDamagePerMark : 15.f;
	if (Id == EUpgradeId::HuntMark)
	{
		return FString::Printf(TEXT("猎杀印记（元素）｜已获%d次；%s；当前最多%d层，队友命中消费全部印记，每层额外%.0f伤。首抽解锁，此后每张上限+1；普通成长无固定总层数上限。"),
			CardTakenCounts.FindRef(Id), Hero && Hero->bHuntMarkUnlocked ? TEXT("已解锁") : TEXT("未解锁"), MarkCap, ExecuteDamage);
	}
	if (Id == EUpgradeId::MarkMaster)
	{
		return FString::Printf(TEXT("猎杀大师（深化）｜当前印记上限%d层、处决%.0f伤/层；领取后上限至少5，保留更高普通成长，处决强化至22伤/层。"), MarkCap, ExecuteDamage);
	}
	if (Id == EUpgradeId::PlayerRapidFire)
	{
		return FString::Printf(TEXT("本体速射（玩家成长）｜已获%d次，当前射击间隔%.3fs；每张×0.85，下限0.15s，到限不再入池。"), CardTakenCounts.FindRef(Id), Hero ? Hero->PlayerShotInterval : 0.5f);
	}
	if (Id == EUpgradeId::CompanionSwiftMove)
	{
		const ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
		return FString::Printf(TEXT("陪伴疾行（玩家成长·协作）｜已获%d/3次，当前队友最高步速%.0fcm/s；每张×1.10，最终不超过玩家当前最高步速，无实际增量不入池。"),
			CardTakenCounts.FindRef(Id), Buddy && Buddy->GetCharacterMovement() ? Buddy->GetCharacterMovement()->MaxWalkSpeed : 0.f);
	}
	return DescribeCard(Id);
}

FString AWaveDirector::DescribeCard(EUpgradeId Id) const
{
	switch (Id)
	{
	case EUpgradeId::Blink:        return TEXT("闪烁（技能）｜解锁 Shift 闪现：朝移动方向瞬移8m（穿人穿怪），CD5s——走位即保命");
	case EUpgradeId::Scatter:      return TEXT("散射（技能）｜队友每发分裂三向弹道（±15°），侧向 70% 伤害——清杂爽感");
	case EUpgradeId::PierceRounds: return TEXT("穿透弹体（技能）｜红色贯穿光束打穿整条走廊，后排敌人 70% 伤害——一穿多");
	case EUpgradeId::RapidFire:    return TEXT("狂暴速射（火力）｜开火间隔 ×0.6（可叠加），弹道变青色——射得肉眼可见地密");
	case EUpgradeId::HeavyRounds:  return TEXT("重击弹头（火力）｜伤害 ×1.5（可叠加），弹道加粗+命中爆闪——威力看得见");
	case EUpgradeId::Radiance:     return TEXT("辉耀领域（领域·可叠加）｜队友周围4m内每0.5秒灼烧5伤；后续每张半径+1m、每拍伤害+10");
	case EUpgradeId::HealNova:     return TEXT("回响脉冲（羁绊·可叠加）｜每累计恢复 40 点生命，脚下展开蔓延 2 秒的绿色领域：7m 内敌人持续受创，蔓延终点爆发 25 伤冲击；再抽 阈值-5/终点伤+10——回复也是武器");
	case EUpgradeId::BuddyHealing: return TEXT("贴贴疗愈（羁绊·可叠加）｜与队友 2.5m 内双方缓慢回血；再抽 +1 HP/拍——贴住他，也贴住危险");
	case EUpgradeId::FrostShotPlayer: return TEXT("寒霜弹·本体版（元素）｜冰弹减速30%持续1.5s；本体直击、分叉与弹跳命中每0.5s向2m近邻传播，感染者不接力。队友火伤消费本体冰，另结等额融化伤害。");
	case EUpgradeId::ShockShot:       return TEXT("感电弹（元素）｜每次射击从枪口额外散射1条70%弹伤侧弹；已有散射时另偏30°，命中挂电5s且重复刷新；队友每0.5s配对4m内两带电活敌，每端至少10伤或队友原伤15%（取高），每敌每拍一条链，不消费标记");
	case EUpgradeId::HuntMark:        return AcquiredCardDescription(Id) + TEXT(" 本次选择：") + UpgradeCardDescription(Id).Replace(TEXT("\n"), TEXT("；"));
	case EUpgradeId::ShockOverload:   return TEXT("感电过载（深化）｜每0.5s每端基础传导10→20，比例15%→27.5%并取高；带电5s不消费，保留枪口70%散射侧弹");
	case EUpgradeId::MarkMaster:      return AcquiredCardDescription(Id) + TEXT(" 本次选择：") + UpgradeCardDescription(Id).Replace(TEXT("\n"), TEXT("；"));
	case EUpgradeId::FrostDeep:       return TEXT("深度冻结（深化）｜你的寒霜减速60%持续4s，传播继承深化数值；仍只有本体弹路命中能成为传播源。");
	case EUpgradeId::PlayerScatter:   return TEXT("玩家散射（弹道）｜你的射击三向分裂 ±15°——与 AI 散射完全独立");
	case EUpgradeId::PlayerPierce:    return TEXT("玩家穿透（弹道）｜你的子弹贯穿敌群，全额伤害；可与玩家折射同持，每个贯穿目标各折射一次，分支不递归");
	case EUpgradeId::PlayerPrism:     return TEXT("玩家折射（弹道）｜每个主弹/贯穿命中向8m内另一敌人单跳70%伤害；与穿透协同，整发目标去重；独立于感电枪口散射");
	case EUpgradeId::GoldPouch:       return TEXT("金币袋（厚礼）｜直接获得 15 金币——大奖池的保底厚礼，落袋为安");
	case EUpgradeId::PlayerRapidFire: return AcquiredCardDescription(Id) + TEXT(" 本次选择：") + UpgradeCardDescription(Id).Replace(TEXT("\n"), TEXT("；"));
	case EUpgradeId::CompanionSwiftMove: return AcquiredCardDescription(Id) + TEXT(" 本次选择：") + UpgradeCardDescription(Id).Replace(TEXT("\n"), TEXT("；"));
	case EUpgradeId::Flip: return TEXT("Flip · 转交队友（本次操作）｜放弃本次玩家候选，转换为AI三选一；不获得技能，不领取下一次奖励，不改变下次等级奖励归属。每个玩家等级奖励只可转换一次。");
	case EUpgradeId::GlassCannon:  return TEXT("玻璃大炮（契约）｜移速设为 7m/s / 最大生命 -40%：走位流的极限取舍");
	case EUpgradeId::BloodSacrifice: return TEXT("血祭强化（契约）｜队友伤害 +50% / 最大生命 -20：以血换火力");
	case EUpgradeId::Vigor:        return TEXT("强身（成长）｜生命上限 +30 并回满：稳稳的安全垫");
	case EUpgradeId::Barrage:      return TEXT("★ 融合·弹幕风暴（散射+穿透）｜三向弹道进化五向，全部贯穿——真正的弹幕");
	case EUpgradeId::BlinkBomb:    return TEXT("★ 融合·闪现轰击（闪烁+散射）｜落点八方爆射；活命中继承当前冰霜/感电/猎印，并独立判定概率地刺；同目标每次爆射只挂载一次，不额外触发主攻震爆或火爆");
	case EUpgradeId::RadianceOverload: return TEXT("★ 融合·辉耀过载（辉耀+速射）｜光环半径至少6m，已有每拍伤害翻倍，保留叠加成长");
	case EUpgradeId::RadianceBond:     return TEXT("★ 融合·辉耀结界（辉耀+贴贴）｜辉耀圈内即贴贴互回，不必再贴身 2.5m；且回响脉冲计数总是生效（满血治疗也累计）——光之领域也是庇护所");
	case EUpgradeId::PrismRounds:      return TEXT("折射弹体（技能）｜命中后向最近敌人弹射（2 跳，每跳 70%），品红折射线——光棱坦克（与穿透互斥）");
	case EUpgradeId::SwiftMove:        return TEXT("疾行（成长）｜移速 +15%（可叠加）——走位流的复利");
	case EUpgradeId::ExplosiveRounds:  return TEXT("高爆弹头（旧版兼容）｜每次范围 +0.8m、溅射 +12 个百分点（首次 3.3m/47%），橙色大爆闪；现役Boss奖励不自动授予");
	case EUpgradeId::FrostRounds:      return TEXT("寒霜弹（火力）｜队友弹命中减速40%持续2s；本体火伤可消费队友冰并另结等额融化，队友辉耀不触发。");
	}
	return TEXT("？？？");
}

const TCHAR* AWaveDirector::RarityTag(ECardRarity Rarity)
{
	switch (Rarity)
	{
	case ECardRarity::Epic:  return TEXT("◆传说");
	case ECardRarity::Rare:  return TEXT("◇稀有");
	default:                 return TEXT("");
	}
}

AWaveDirector::ECardRarity AWaveDirector::CardRarityOf(EUpgradeId Id) const
{
	switch (Id)
	{
	case EUpgradeId::Blink:
	case EUpgradeId::PierceRounds:
	case EUpgradeId::PrismRounds:
	case EUpgradeId::Radiance:
	case EUpgradeId::ExplosiveRounds:
	case EUpgradeId::BloodSacrifice:
	case EUpgradeId::Barrage:
	case EUpgradeId::BlinkBomb:
	case EUpgradeId::RadianceOverload:
	case EUpgradeId::RadianceBond:
	case EUpgradeId::ShockOverload:
	case EUpgradeId::MarkMaster:
	case EUpgradeId::FrostDeep:
		return ECardRarity::Epic;
	case EUpgradeId::Scatter:
	case EUpgradeId::HeavyRounds:
	case EUpgradeId::FrostRounds:
	case EUpgradeId::GlassCannon:
	case EUpgradeId::HealNova:
	case EUpgradeId::FrostShotPlayer:
	case EUpgradeId::ShockShot:
	case EUpgradeId::HuntMark:
	case EUpgradeId::PlayerScatter:
	case EUpgradeId::PlayerPierce:
	case EUpgradeId::PlayerPrism:
	case EUpgradeId::GoldPouch:
		return ECardRarity::Rare;
	default:
		return ECardRarity::Common;   // RapidFire/Vigor/SwiftMove/BuddyHealing——数值成长与安全垫
	}
}

FString AWaveDirector::CardShortName(EUpgradeId Id) const
{
	const FString Full = DescribeCard(Id);
	const int32 Sep = Full.Find(TEXT("｜"));
	return Sep > 0 ? Full.Left(Sep) : Full;
}

EUpgradeCardRarity AWaveDirector::PresentationRarity(const ECardRarity Rarity)
{
	return Rarity == ECardRarity::Epic ? EUpgradeCardRarity::Legendary
		: (Rarity == ECardRarity::Rare ? EUpgradeCardRarity::Rare : EUpgradeCardRarity::Common);
}

bool AWaveDirector::IsFusionLegendary(const EUpgradeId Id)
{
	return Id == EUpgradeId::Barrage || Id == EUpgradeId::BlinkBomb
		|| Id == EUpgradeId::RadianceOverload || Id == EUpgradeId::RadianceBond;
}

FString AWaveDirector::CardBarkOf(EUpgradeId Id, EGameSFX& OutVoice) const
{
	// 卡面台词表（09-27 配音轮，冷峻人偶人格——机械感与游戏世界观对位；语音槽与文案同源）
	OutVoice = EGameSFX::None;
	switch (Id)
	{
	case EUpgradeId::Blink:            OutVoice = EGameSFX::CardVoice_Blink;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::Scatter:          OutVoice = EGameSFX::CardVoice_Scatter;         return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::PierceRounds:     OutVoice = EGameSFX::CardVoice_Pierce;          return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::PrismRounds:      OutVoice = EGameSFX::CardVoice_Prism;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::RapidFire:        OutVoice = EGameSFX::CardVoice_RapidFire;       return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::HeavyRounds:      OutVoice = EGameSFX::CardVoice_Heavy;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::ExplosiveRounds:  OutVoice = EGameSFX::CardVoice_Explosive;       return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::FrostRounds:      OutVoice = EGameSFX::CardVoice_Frost;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::Radiance:         OutVoice = EGameSFX::CardVoice_Radiance;        return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::BuddyHealing:     OutVoice = EGameSFX::CardVoice_BuddyHeal;       return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::HealNova:         OutVoice = EGameSFX::CardVoice_HealNova;        return USFXLibrary::GetVoiceLine(OutVoice);
	// 元素三卡（09-29）：复用现有录音，字幕必须显示该录音真正的台词，不另造文字
	case EUpgradeId::FrostShotPlayer:  OutVoice = EGameSFX::CardVoice_Frost;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::ShockShot:        OutVoice = EGameSFX::CardVoice_Pierce;          return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::HuntMark:         OutVoice = EGameSFX::CardVoice_Heavy;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::GlassCannon:      OutVoice = EGameSFX::CardVoice_GlassCannon;     return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::BloodSacrifice:   OutVoice = EGameSFX::CardVoice_BloodSacrifice;  return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::Vigor:            OutVoice = EGameSFX::CardVoice_Vigor;           return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::SwiftMove:        OutVoice = EGameSFX::CardVoice_SwiftMove;       return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::CompanionSwiftMove: OutVoice = EGameSFX::CardVoice_CompanionSwiftMove; return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::Barrage:          OutVoice = EGameSFX::CardVoice_Barrage;         return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::BlinkBomb:        OutVoice = EGameSFX::CardVoice_BlinkBomb;       return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::RadianceOverload: OutVoice = EGameSFX::CardVoice_RadianceOverload; return USFXLibrary::GetVoiceLine(OutVoice);
	case EUpgradeId::RadianceBond:     OutVoice = EGameSFX::CardVoice_RadianceBond;    return USFXLibrary::GetVoiceLine(OutVoice);
	default:                           break;
	}
	return FString();
}

AWaveDirector::FEndlessHudSnapshot AWaveDirector::GetHudSnapshot() const
{
	FEndlessHudSnapshot Snap;
	Snap.SurvivalSeconds = ElapsedSeconds;
	Snap.HpMultiplier = bEndlessMode ? FMath::Pow(HpGrowthPerMinute, GetElapsedMinutes()) : 1.f;
	Snap.AliveCount = AliveCount;
	Snap.KillCount = KillCount;
	Snap.Level = Level;
	Snap.CurrentXp = CurrentXp;
	Snap.XpToNext = XpToNextLevel;
	Snap.GoldCount = GoldCount;
	Snap.BossKilledCount = BossKilledCount;
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (const AAICompanionDemoCharacter* Char = Cast<AAICompanionDemoCharacter>(PlayerPawn))
		{
			Snap.bHasTreasureKey = Char->HasTreasureKey();
		}
	}
	for (const TPair<EUpgradeId, int32>& Entry : CardTakenCounts)
	{
		FHudUpgradeCard& Card = Snap.UpgradeCards.AddDefaulted_GetRef();
		Card.UpgradeId = UpgradeCardKey(Entry.Key);
		Card.Name = CardShortName(Entry.Key);
		Card.Count = Entry.Value;
		Card.Description = AcquiredCardDescription(Entry.Key);
		Card.Rarity = PresentationRarity(CardRarityOf(Entry.Key));
	}
	AppendRelicHudCards(Snap);
	Snap.UpgradeCards.Sort([](const FHudUpgradeCard& A, const FHudUpgradeCard& B)
	{
		return A.UpgradeId.LexicalLess(B.UpgradeId);
	});
	return Snap;
}
