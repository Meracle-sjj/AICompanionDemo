/**
 * [INPUT]: 依赖 Engine 的 USoundBase（TSoftObjectPtr 软引用槽位——素材由用户从 Epic/Fab 官方渠道导入，代码零内容耦合）与 UWorld、Kismet 的 SpawnSound2D（2D 全局播放——演示玩法无空间化需求）
 * [OUTPUT]: EGameSFX事件槽、USFXLibrary(Config=Game)与PlaySFX/PlayVoice；药剂/钥匙/首次信标拾取语音共用每世界5秒CD，冷却内跳过不排队；BossImpact独立真实爆炸槽与音量，其他事件音效/语音不占拾取CD；空槽静默返回
 *  信标首次解锁/成功召回沿用现役两条资源路径，v2声源为冷峻自然女声；台词分别为“我一直在你身边。”“我从未离开过你”。
 * [POS]: Audio/ 的音效基建唯一文件；全部游戏事件经 PlaySFX 收口发声——事件方不知道音效是否存在，素材到位与否只改 ini、代码零改动
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "SFXLibrary.generated.h"

class USoundBase;
class UWorld;

/** 全游戏音效事件清单——挂接点见各业务文件的 PlaySFX 调用行 */
UENUM(BlueprintType)
enum class EGameSFX : uint8
{
	Shot_Companion,      // 队友射击（主弹道——散射/爆射的侧向弹道不重复发声）
	Hit_Enemy,           // 队友弹道命中敌人
	EnemyDie,            // 敌人死亡
	PlayerHurt,          // 玩家受击
	Pickup_Xp,           // 经验晶体收集
	Pickup_Gold,         // 金币收集
	Pickup_Heal,         // 柠檬蛋糕回血收集
	Pickup_Rush,         // 暂态道具收集（磁吸风暴/狂怒攻速/圣盾无敌）
	UpgradeChoice,       // 三选一升级弹出
	ChestOpen,           // 宝箱开启成功
	Blink,               // 玩家闪现
	Recall,              // 紧急呼叫（信标拾取解锁与队友召回传送复用）
	Explosion,           // 爆炸——独立槽：独特音效，稀有度高（每次溅射一声，不逐受击者叠加）
	WarlockBeamCharge,   // 妖术师蓄力起手（红色预警出现）
	WarlockBeamFire,     // 妖术师射线结算（粗束扫过）

	// ---------- 语音（09-27 配音轮：豆包 seed-audio 冷峻女声，Bark 台词同源） ----------
	Voice_Discover,      // 语音：接敌"发现敌人，交给我！"
	Voice_Kill,          // 语音：击杀"威胁已清除"
	Voice_Clear,         // 语音：威胁解除
	Voice_PlayerHurt,    // 语音："放开我的伙伴！"
	Voice_PlayerLow,     // 语音："你撑住，我顶上！"
	Voice_ModeGuard,     // 语音："护卫协议，掩护优先。"
	Voice_ModeBerserk,   // 语音："全部交给我！"
	Voice_Magnet,        // 语音：磁吸风暴"磁暴展开——战利品都过来！"（09-29 药剂台词）
	Voice_Frenzy,        // 语音：狂怒攻速"火力全开，都交给我！"（09-29 药剂台词）
	Voice_Aegis,         // 语音：圣盾无敌"圣盾加身，五秒无敌！"（09-29 药剂台词）

	// ---------- 卡面台词语音（09-27 配音轮二批：抽到新技能时队友评论一句） ----------
	CardVoice_Blink,
	CardVoice_Scatter,
	CardVoice_Pierce,
	CardVoice_Prism,
	CardVoice_RapidFire,
	CardVoice_Heavy,
	CardVoice_Explosive,
	CardVoice_Frost,
	CardVoice_Radiance,
	CardVoice_BuddyHeal,
	CardVoice_HealNova,
	CardVoice_GlassCannon,
	CardVoice_BloodSacrifice,
	CardVoice_Vigor,
	CardVoice_SwiftMove,
	CardVoice_Barrage,
	CardVoice_BlinkBomb,
	CardVoice_RadianceOverload,
	CardVoice_RadianceBond,

	// ---------- 三批（09-27）：事件细分音效与队友事件语音 ----------
	Pickup_Magnet,       // 磁吸风暴独立音（三系共用槽退役于磁铁——用户点名）
	HealNovaBurst,       // 回响脉冲爆发独立音（原共用爆炸槽——爆炸声压独立治理）
	Voice_ChestOpen,     // 语音：金币宝箱开启"补给开启。"
	Voice_LockedChest,   // 语音：锁箱开锁"运气不错。"
	Voice_Recovered,     // 语音：濒死恢复健康"生命体征恢复，很好。"
	Voice_TreasureKey,   // 语音：捡到钥匙"钥匙到手，找带锁的箱子。"
	BossWarn,            // 祭坛警报（09-30 三 Boss 终局：低沉危机锣声——"咚"，祭坛冒出即响）

	Voice_BeaconActivate, // 我一直在你身边。（首次拾取信标）
	Voice_BeaconRecall,   // 我从未离开过你（成功召回）
	BossImpact,          // 虫钻地/破土与巨像敲地：独立真实爆炸采样，不共用溅射音量
	None                 // 哨兵：无语音（Bark 默认值——PlaySFX 对它静默）
};

/**
 * 音效槽位配置真源（Config=Game，落 DefaultGame.ini）：每事件一个软引用 + 音量。
 * 本类不做实例管理——PlaySFX 直接读 CDO；素材路径由用户填 ini，留空即静默。
 */
UCLASS(Config = Game)
class AICOMPANIONDEMO_API USFXLibrary : public UObject
{
	GENERATED_BODY()

public:
	/** 全项目音效总入口：按事件读 CDO 槽位并 2D 播放；World 为空/槽位未配置均静默返回 */
	static void PlaySFX(UWorld* World, EGameSFX Event);

	/** 空间化语音入口（09-27 用户定调"音箱放到 AI 身上"）：3D 定位于 Location——远处队友的喊话
	 *  距离衰减至听不清（400cm 内全响，线性衰减 4000cm 静默，运行时构造衰减零资产）；槽空静默 */
	static void PlayVoice(UWorld* World, EGameSFX Event, const FVector& Location);

	/** 拾取语音共享冷却剩余秒数；跟随当前世界游戏时间，新世界自然重置。 */
	UFUNCTION(BlueprintPure, Category = "SFX|Pickup", meta = (WorldContext = "WorldContextObject"))
	static float GetPickupVoiceCooldownRemaining(const UObject* WorldContextObject);

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0"))
	float PickupVoiceCooldownSeconds = 5.f;

	// ---------- 槽位区：素材到位后填 ini（XXX=/Game/...）；音量为 0~1 线性增益 ----------

	/** 队友射击（主弹道） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> Shot_Companion;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Shot_CompanionVolume = 0.7f;

	/** 命中敌人 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> Hit_Enemy;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Hit_EnemyVolume = 0.5f;

	/** 敌人死亡 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> EnemyDie;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float EnemyDieVolume = 0.8f;

	/** 玩家受击 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> PlayerHurt;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PlayerHurtVolume = 0.8f;

	/** 爆炸——独立槽：独特音效，稀有度高（溅射爆点专用，区别于普通命中） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> Explosion;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ExplosionVolume = 1.0f;

	/** Boss大范围冲击独立采样与声压，普通子弹溅射保持原槽。 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss")
	TSoftObjectPtr<USoundBase> BossImpact;
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BossImpactVolume = 1.f;

	/** 经验晶体收集 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup")
	TSoftObjectPtr<USoundBase> Pickup_Xp;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Pickup_XpVolume = 0.6f;

	/** 金币收集 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup")
	TSoftObjectPtr<USoundBase> Pickup_Gold;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Pickup_GoldVolume = 0.6f;

	/** 柠檬蛋糕回血收集 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup")
	TSoftObjectPtr<USoundBase> Pickup_Heal;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Pickup_HealVolume = 0.7f;

	/** 暂态道具收集（磁吸风暴/狂怒攻速/圣盾无敌共用） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup")
	TSoftObjectPtr<USoundBase> Pickup_Rush;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Pickup_RushVolume = 0.9f;

	/** 三选一升级弹出 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|UI")
	TSoftObjectPtr<USoundBase> UpgradeChoice;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|UI", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float UpgradeChoiceVolume = 0.9f;

	/** 宝箱开启成功 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|UI")
	TSoftObjectPtr<USoundBase> ChestOpen;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|UI", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ChestOpenVolume = 0.9f;

	/** 玩家闪现 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Ability")
	TSoftObjectPtr<USoundBase> Blink;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Ability", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BlinkVolume = 0.7f;

	/** 紧急呼叫（信标拾取与队友召回传送复用） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Ability")
	TSoftObjectPtr<USoundBase> Recall;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Ability", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RecallVolume = 0.9f;

	/** 妖术师蓄力起手（预警本体的一部分——玩家听声辨招） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss")
	TSoftObjectPtr<USoundBase> WarlockBeamCharge;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarlockBeamChargeVolume = 0.8f;

	/** 妖术师射线结算 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss")
	TSoftObjectPtr<USoundBase> WarlockBeamFire;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarlockBeamFireVolume = 0.9f;

	// ---------- 语音槽（09-27 配音轮）：Bark 台词配音，人声 1.0 优先于一切音效（响度=信息优先级） ----------

	/** 接敌："发现敌人，交给我！" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Discover;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_DiscoverVolume = 1.0f;

	/** 击杀："威胁已清除" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Kill;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_KillVolume = 1.0f;

	/** 威胁解除 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Clear;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_ClearVolume = 1.0f;

	/** 玩家挨打："放开我的伙伴！" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_PlayerHurt;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_PlayerHurtVolume = 1.0f;

	/** 玩家低血："你撑住，我顶上！" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_PlayerLow;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_PlayerLowVolume = 1.0f;

	/** 切护卫："护卫协议，掩护优先。" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_ModeGuard;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_ModeGuardVolume = 1.0f;

	/** 切狂人："全部交给我！" */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_ModeBerserk;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_ModeBerserkVolume = 1.0f;

	// ---------- 卡面台词槽（09-27 二批）：抽卡触发，ini 按 card_*.mp3 填 ----------

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Blink;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Scatter;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Pierce;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Prism;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_RapidFire;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Heavy;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Explosive;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Frost;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Radiance;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_BuddyHeal;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_HealNova;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_GlassCannon;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_BloodSacrifice;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Vigor;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_SwiftMove;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_Barrage;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_BlinkBomb;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_RadianceOverload;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|CardVoice")
	TSoftObjectPtr<USoundBase> CardVoice_RadianceBond;

	// ---------- 三批槽位（09-27）：事件细分 ----------

	/** 磁吸风暴收集（独立于 Rush 共用槽——用户点名） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup")
	TSoftObjectPtr<USoundBase> Pickup_Magnet;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Pickup", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Pickup_MagnetVolume = 0.6f;

	/** 回响脉冲爆发（独立于爆炸槽——治疗系声压独立治理，柔和） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat")
	TSoftObjectPtr<USoundBase> HealNovaBurst;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HealNovaBurstVolume = 0.45f;

	/** 金币宝箱开启（队友台词） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_ChestOpen;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_ChestOpenVolume = 1.0f;

	/** 锁箱开锁（队友台词） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_LockedChest;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_LockedChestVolume = 1.0f;

	/** 濒死恢复健康（队友台词） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Recovered;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_RecoveredVolume = 1.0f;

	/** 捡到宝箱钥匙（队友台词，09-28 用户点名） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_TreasureKey;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_TreasureKeyVolume = 1.0f;

	/** 祭坛警报（09-30 三 Boss 终局：祭坛冒出即响的危机锣——用户点名"咚之类的危机降临音"） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss")
	TSoftObjectPtr<USoundBase> BossWarn;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Boss", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BossWarnVolume = 1.0f;

	/** 药剂三句（09-29 药剂台词轮）：磁吸风暴/狂怒攻速/圣盾无敌——收集即喊，TTS 管线同源 */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Magnet;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_MagnetVolume = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Frenzy;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_FrenzyVolume = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_Aegis;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Voice_AegisVolume = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_BeaconActivate;
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	float Voice_BeaconActivateVolume = 1.f;
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	TSoftObjectPtr<USoundBase> Voice_BeaconRecall;
	UPROPERTY(EditAnywhere, Config, Category = "SFX|Voice")
	float Voice_BeaconRecallVolume = 1.f;
	// ---------- BGM（09-26）：循环常驻，点"开始游戏"起播 ----------

	/** BGM 曲目（SoundWave 需 Looping=true——导入脚本已设） */
	UPROPERTY(EditAnywhere, Config, Category = "SFX|BGM")
	TSoftObjectPtr<USoundBase> BGM_Track;

	UPROPERTY(EditAnywhere, Config, Category = "SFX|BGM", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BGM_Volume = 0.45f;

	/** 循环起播（GameMode::StartRun 调用）：BGM 槽空静默；重复调用幂等（已有组件在播则跳过） */
	static void PlayBGM(UWorld* World);
};
