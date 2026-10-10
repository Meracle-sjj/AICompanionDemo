/**
 * [INPUT]: CoreMinimal 的 FName/FLinearColor；宿主提供展示稀有度，卡图与中文元数据源自 v2 manifest
 * [OUTPUT]: EUpgradeCardRarity 普通/稀有/传说/独特遗物；蓝/紫/金与金红样式、29升级+9遗物稳定元数据/纹理路径查询；未知 ID 返回 nullptr
 * [POS]: UI/ 纯展示真源；升级选择页与常驻 HUD 共用，不持有纹理、不加载资产、不引用玩法枚举或抽卡状态
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"

/** 展示层稀有度；游戏宿主显式映射真实玩法枚举。 */
enum class EUpgradeCardRarity : uint8
{
	Common,
	Rare,
	Legendary,
	UniqueRelic
};

namespace UpgradeCardPresentation
{
	struct FCardMetadata
	{
		const TCHAR* Id;
		const TCHAR* Title;
		const TCHAR* Category;
		const TCHAR* ShortDescription;
		const TCHAR* TextureObjectPath;
	};

	AICOMPANIONDEMO_API FLinearColor GetRarityColor(EUpgradeCardRarity Rarity);
	AICOMPANIONDEMO_API const TCHAR* GetRarityLabel(EUpgradeCardRarity Rarity);
	/** 指针由静态表提供，生命周期覆盖模块；未知 ID 返回 nullptr。 */
	AICOMPANIONDEMO_API const FCardMetadata* FindCardMetadata(FName UpgradeId);
	AICOMPANIONDEMO_API const TCHAR* GetCardTextureAssetPath(FName UpgradeId);
}
