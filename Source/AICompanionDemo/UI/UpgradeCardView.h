/**
 * [INPUT]: 宿主注入三/六强化事实与选择委托，及独立可选Flip委托；Slate/展示元数据/纹理GC生命周期
 * [OUTPUT]: 升级/遗物三卡或锁箱六卡；Flip额外页脚按钮不进入Options/Slot，点击仅执行宿主委托；成员style/纹理/brush保活
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
		const FString& Heading, const FString& Hint, bool bChest, const FSimpleDelegate& FlipAction = FSimpleDelegate());

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
