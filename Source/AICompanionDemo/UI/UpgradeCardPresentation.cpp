/**
 * [INPUT]: UpgradeCardPresentation.h、32成长展示ID/9遗物的独立标题/短说明；已有UpgradeCards/BossRelics纹理路径；Flip无外部纹理
 * [OUTPUT]: 选择页/HUD共用41项稳定ID；针刺短句同步主攻/弹射30%与50伤/5s流血，动态说明由宿主覆盖静态短句
 * [POS]: UI/ 无状态展示资料实现；不加载 UObject，不决定卡池稀有度或融合资格
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "UpgradeCardPresentation.h"

namespace
{
	// 与 assets/upgrade-cards/v2/manifest.json 枚举顺序一致；双行按语义断句。
	const UpgradeCardPresentation::FCardMetadata CardMetadata[] = {
		{ TEXT("Blink"), TEXT("闪烁"), TEXT("技能"), TEXT("朝移动方向瞬移 8m\n冷却 5s。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_Blink_v1.T_Upgrade_Blink_v1") },
		{ TEXT("Scatter"), TEXT("散射"), TEXT("技能"), TEXT("队友三向射击\n侧向造成 70% 伤害。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_Scatter_v1.T_Upgrade_Scatter_v1") },
		{ TEXT("PierceRounds"), TEXT("穿透弹体"), TEXT("技能"), TEXT("队友光束贯穿敌群\n后排造成 70% 伤害。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_PierceRounds_v1.T_Upgrade_PierceRounds_v1") },
		{ TEXT("RapidFire"), TEXT("狂暴速射"), TEXT("火力"), TEXT("队友开火间隔 ×0.6\n可叠加，提升射击密度。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_RapidFire_v1.T_Upgrade_RapidFire_v1") },
		{ TEXT("HeavyRounds"), TEXT("重击弹头"), TEXT("火力"), TEXT("队友伤害提升 50%\n可叠加，弹道加粗爆闪。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_HeavyRounds_v1.T_Upgrade_HeavyRounds_v1") },
		{ TEXT("Radiance"), TEXT("辉耀领域"), TEXT("领域"), TEXT("4m内每0.5s灼烧5伤\n每张叠加+10伤、+1m。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_Radiance_v1.T_Upgrade_Radiance_v1") },
		{ TEXT("BuddyHealing"), TEXT("贴贴疗愈"), TEXT("羁绊"), TEXT("距队友 2.5m 内\n双方持续回血。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_BuddyHealing_v1.T_Upgrade_BuddyHealing_v1") },
		{ TEXT("GlassCannon"), TEXT("玻璃大炮"), TEXT("契约"), TEXT("移速设为 7m/s\n生命上限降低 40%。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_GlassCannon_v1.T_Upgrade_GlassCannon_v1") },
		{ TEXT("BloodSacrifice"), TEXT("血祭强化"), TEXT("契约"), TEXT("队友伤害提升 50%\n你的生命上限减 20。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_BloodSacrifice_v1.T_Upgrade_BloodSacrifice_v1") },
		{ TEXT("Vigor"), TEXT("强身"), TEXT("成长"), TEXT("生命上限增加 30\n并立即恢复生命。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_Vigor_v1.T_Upgrade_Vigor_v1") },
		{ TEXT("Barrage"), TEXT("融合·弹幕风暴"), TEXT("融合"), TEXT("队友五向弹幕\n每条光束都可贯穿。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_Barrage_v1.T_Upgrade_Barrage_v1") },
		{ TEXT("BlinkBomb"), TEXT("融合·闪现轰击"), TEXT("融合"), TEXT("八方射线继承当前元素\n印记与概率地刺。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_BlinkBomb_v1.T_Upgrade_BlinkBomb_v1") },
		{ TEXT("RadianceOverload"), TEXT("融合·辉耀过载"), TEXT("融合"), TEXT("辉耀半径至少6m\n当前每拍灼烧伤害翻倍。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_RadianceOverload_v1.T_Upgrade_RadianceOverload_v1") },
		{ TEXT("PrismRounds"), TEXT("折射弹体"), TEXT("技能"), TEXT("队友弹道连续折射\n每跳保留 70% 伤害。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_PrismRounds_v2.T_Upgrade_PrismRounds_v2") },
		{ TEXT("SwiftMove"), TEXT("疾行"), TEXT("成长"), TEXT("移速提升 15%\n可叠加，持续提速。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_SwiftMove_v1.T_Upgrade_SwiftMove_v1") },
		{ TEXT("ExplosiveRounds"), TEXT("高爆弹头"), TEXT("火力"), TEXT("旧版兼容：队友范围爆炸\n现役奖励池不发放。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_ExplosiveRounds_v1.T_Upgrade_ExplosiveRounds_v1") },
		{ TEXT("FrostRounds"), TEXT("寒霜弹"), TEXT("火力"), TEXT("减速40%，持续2s\n本体火伤触发等额融化\n队友辉耀不触发。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_FrostRounds_v1.T_Upgrade_FrostRounds_v1") },
		{ TEXT("RadianceBond"), TEXT("融合·辉耀结界"), TEXT("融合"), TEXT("辉耀圈内双方持续回血\n无需贴近。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_RadianceBond_v1.T_Upgrade_RadianceBond_v1") },
		{ TEXT("HealNova"), TEXT("回响脉冲"), TEXT("羁绊"), TEXT("治疗积累到阈值\n绿域持续伤敌后爆发。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_HealNova_v1.T_Upgrade_HealNova_v1") },
		{ TEXT("FrostShotPlayer"), TEXT("寒霜弹·本体版"), TEXT("元素"), TEXT("冰弹减速30% 1.5s\n0.5s/2m传染，不接力\n队友火伤另结等额融化。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_FrostShotPlayer_v1.T_Upgrade_FrostShotPlayer_v1") },
		{ TEXT("ShockShot"), TEXT("感电弹"), TEXT("元素"), TEXT("枪口赠70%散射，带电5s\n每0.5s两端至少10伤，不消费。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_ShockShot_v1.T_Upgrade_ShockShot_v1") },
		{ TEXT("HuntMark"), TEXT("猎杀印记"), TEXT("元素"), TEXT("你的命中叠加印记\n队友命中引爆额外伤害。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_HuntMark_v1.T_Upgrade_HuntMark_v1") },
		{ TEXT("ShockOverload"), TEXT("感电过载"), TEXT("深化"), TEXT("每0.5s两端至少20伤\n或队友原伤27.5%，取高。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_ShockOverload_v1.T_Upgrade_ShockOverload_v1") },
		{ TEXT("MarkMaster"), TEXT("猎杀大师"), TEXT("深化"), TEXT("印记上限至少 5 层\n保留成长，每层处决22伤。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_MarkMaster_v1.T_Upgrade_MarkMaster_v1") },
		{ TEXT("FrostDeep"), TEXT("深度冻结"), TEXT("深化"), TEXT("减速60%，持续4s\n传播继承深化数值。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_FrostDeep_v1.T_Upgrade_FrostDeep_v1") },
		{ TEXT("PlayerScatter"), TEXT("玩家散射"), TEXT("弹道"), TEXT("你的射击分裂三向\n侧弹造成 70% 伤害。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_PlayerScatter_v1.T_Upgrade_PlayerScatter_v1") },
		{ TEXT("PlayerPierce"), TEXT("玩家穿透"), TEXT("弹道"), TEXT("贯穿敌群保留全额伤害\n可与玩家折射共同强化。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_PlayerPierce_v1.T_Upgrade_PlayerPierce_v1") },
		{ TEXT("PlayerPrism"), TEXT("玩家折射"), TEXT("弹道"), TEXT("每个贯穿命中单跳70%\n全弹目标去重，分支不递归。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_PlayerPrism_v2.T_Upgrade_PlayerPrism_v2") },
		{ TEXT("GoldPouch"), TEXT("金币袋"), TEXT("厚礼"), TEXT("立即获得 15 金币\n锁箱卡池干涸时填位。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_GoldPouch_v1.T_Upgrade_GoldPouch_v1") },
		// 1.1追加展示；复用既有插图，不新增或改写Content素材。Flip使用视图自有双箭头几何。
		{ TEXT("PlayerRapidFire"), TEXT("本体速射"), TEXT("玩家成长"), TEXT("你的射击间隔 ×0.85\n最低0.15秒，到限离池。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_RapidFire_v1.T_Upgrade_RapidFire_v1") },
		{ TEXT("CompanionSwiftMove"), TEXT("陪伴疾行"), TEXT("玩家成长·协作"), TEXT("队友移速提升10%\n最多3次，不快于玩家。"), TEXT("/Game/UI/UpgradeCards/T_Upgrade_SwiftMove_v1.T_Upgrade_SwiftMove_v1") },
		{ TEXT("Flip"), TEXT("Flip · 转交队友"), TEXT("本次操作"), TEXT("改抽本次AI升级池\n不领取、不改变下次归属。"), nullptr },
		{ TEXT("BloodLink"), TEXT("血誓共鸣"), TEXT("召唤师遗物"), TEXT("距队友8m内，弹道实际伤害\n的25%转化为你的生命。"), TEXT("/Game/UI/BossRelics/T_Relic_BloodLink_v1.T_Relic_BloodLink_v1") },
		{ TEXT("SpiritFrenzy"), TEXT("不灭执念"), TEXT("召唤师遗物"), TEXT("队友每20s狂暴5s\n狂暴药剂持续时间+50%。"), TEXT("/Game/UI/BossRelics/T_Relic_SpiritFrenzy_v1.T_Relic_SpiritFrenzy_v1") },
		{ TEXT("MovingFireball"), TEXT("逐焰灵核"), TEXT("召唤师遗物"), TEXT("行走15m一球，闪烁3必中球\n直击30／范围20，互斥。"), TEXT("/Game/UI/BossRelics/T_Relic_MovingFireball_v1.T_Relic_MovingFireball_v1") },
		{ TEXT("StoneSpikes"), TEXT("裂地獠牙"), TEXT("巨像遗物"), TEXT("主攻/弹射30%地刺：50伤\n附5s流血，每0.5s10伤。"), TEXT("/Game/UI/BossRelics/T_Relic_StoneSpikes_v1.T_Relic_StoneSpikes_v1") },
		{ TEXT("SeismicStrike"), TEXT("撼岳重击"), TEXT("巨像遗物"), TEXT("每6s主攻震爆：35伤\n震退命中点4.5m内敌人。"), TEXT("/Game/UI/BossRelics/T_Relic_SeismicStrike_v1.T_Relic_SeismicStrike_v1") },
		{ TEXT("StoneCarapace"), TEXT("磐岩逆甲"), TEXT("巨像遗物"), TEXT("永久减伤15%\n受击向伤害来源反击10伤。"), TEXT("/Game/UI/BossRelics/T_Relic_StoneCarapace_v1.T_Relic_StoneCarapace_v1") },
		{ TEXT("VolcanicRounds"), TEXT("熔核爆弹"), TEXT("蠕虫遗物"), TEXT("每次主攻附带火焰爆炸\n命中点2.5m内额外12伤。"), TEXT("/Game/UI/BossRelics/T_Relic_VolcanicRounds_v1.T_Relic_VolcanicRounds_v1") },
		{ TEXT("FlameWard"), TEXT("焚天护环"), TEXT("蠕虫遗物"), TEXT("每10s火盾4s，减伤20%\n灼烧近敌，遇冰霜触发融化。"), TEXT("/Game/UI/BossRelics/T_Relic_FlameWard_v1.T_Relic_FlameWard_v1") },
		{ TEXT("BlinkPyre"), TEXT("踏烬之径"), TEXT("蠕虫遗物"), TEXT("闪烁起终留下3m火圈4s\n每0.5s灼烧4伤。"), TEXT("/Game/UI/BossRelics/T_Relic_BlinkPyre_v1.T_Relic_BlinkPyre_v1") }
	};
}

FLinearColor UpgradeCardPresentation::GetRarityColor(const EUpgradeCardRarity Rarity)
{
	switch (Rarity)
	{
	case EUpgradeCardRarity::UniqueRelic: return FLinearColor(1.f, 0.68f, 0.18f, 1.f);
	case EUpgradeCardRarity::Rare: return FLinearColor(0.72f, 0.40f, 1.f, 1.f);
	case EUpgradeCardRarity::Legendary: return FLinearColor(1.f, 0.73f, 0.20f, 1.f);
	case EUpgradeCardRarity::Common:
	default: return FLinearColor(0.24f, 0.62f, 1.f, 1.f);
	}
}

const TCHAR* UpgradeCardPresentation::GetRarityLabel(const EUpgradeCardRarity Rarity)
{
	switch (Rarity)
	{
	case EUpgradeCardRarity::UniqueRelic: return TEXT("独特遗物");
	case EUpgradeCardRarity::Rare: return TEXT("稀有");
	case EUpgradeCardRarity::Legendary: return TEXT("传说");
	case EUpgradeCardRarity::Common:
	default: return TEXT("普通");
	}
}

const UpgradeCardPresentation::FCardMetadata* UpgradeCardPresentation::FindCardMetadata(const FName UpgradeId)
{
	for (const FCardMetadata& Entry : CardMetadata)
	{
		if (UpgradeId == FName(Entry.Id))
		{
			return &Entry;
		}
	}
	return nullptr;
}

const TCHAR* UpgradeCardPresentation::GetCardTextureAssetPath(const FName UpgradeId)
{
	const FCardMetadata* Metadata = FindCardMetadata(UpgradeId);
	return Metadata ? Metadata->TextureObjectPath : nullptr;
}
