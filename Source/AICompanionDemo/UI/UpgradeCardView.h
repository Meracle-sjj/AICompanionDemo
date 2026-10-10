/**
 * [INPUT]: 依赖 UObject 生命周期、Slate 的 SWidget/FSlateBrush/FButtonStyle、UpgradeCardPresentation 展示稀有度与元数据；宿主注入升级 ID、槽位、领取状态、真实稀有度/融合资格、完整说明、可选短说明与弱绑定选择委托
 * [OUTPUT]: FUpgradeCardOption 与 UUpgradeCardView::Build：升级/遗物三卡横排、锁箱六卡两行；蓝/紫/金/独特金红，融合传说金红大拇指；独立图文、纹理UPROPERTY保活，点击只执行宿主委托
 * [POS]: UI/ 升级卡片展示层；不依赖 WaveDirector、升级枚举、抽卡或效果逻辑。宿主必须以 UPROPERTY 持有本对象，并在释放对象前移除返回的 Slate 树
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UObject/Object.h"
#include "UpgradeCardPresentation.h"
#include "UpgradeCardView.generated.h"

class SWidget;
class UTexture2D;
struct FSlateBrush;

/** 展示输入：升级事实与选择行为均由宿主提供。 */
struct FUpgradeCardOption
{
	FName UpgradeId;
	int32 Slot = 0;
	bool bTaken = false;
	EUpgradeCardRarity Rarity = EUpgradeCardRarity::Common;
	bool bFusionLegendary = false;
	FString FullDescription;
	FString ShortDescriptionOverride;
	FSimpleDelegate OnSelected;
};

UCLASS()
class AICOMPANIONDEMO_API UUpgradeCardView : public UObject
{
	GENERATED_BODY()

public:
	/** 返回全屏展示树；不冻结世界、不抽卡、不改变输入模式。 */
	TSharedRef<SWidget> Build(const TArray<FUpgradeCardOption>& Options,
		const FString& Heading, const FString& Hint, bool bChest);

private:
	/** 惰性装载纹理并返回稳定的 brush 地址；失败缓存纯色底并仅向日志告警。 */
	const FSlateBrush* ResolveBrush(FName UpgradeId);
	TSharedRef<SWidget> MakeCard(const FUpgradeCardOption& Option, bool bChest);
	void PrepareButtonStyle();

	/** Slate brush 的资源对象引用不能代替 UObject 的 GC 所有权。 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> LoadedTextures;

	/** heap 上的 brush 不随 TMap 扩容移动；返回树存活期间禁止清空此缓存。 */
	TMap<FName, TSharedPtr<FSlateBrush>> ArtBrushes;

	/** SButton 保存样式指针；样式随本对象存活，不能在 Build/MakeCard 栈上构造。 */
	FButtonStyle CardButtonStyles[4];
	bool bButtonStyleReady = false;
};
