/**
 * [INPUT]: UMG World WidgetComponent、Slate 原生折线绘制、宿主交入的猎杀层数与相机视点
 * [OUTPUT]: UHuntMarkVisual——头顶世界空间猎印/真实数字/离散层数点，1–2层金黄、≥3层红色；SetStacks(0) 隐藏，提供只读验收状态
 * [POS]: Vfx/ 纯显示组件；宿主保有叠层/消耗/伤害规则，组件只转自身平面、不改角色朝向、不加载游戏特效资产
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md；成员地图由主 agent 统一维护
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "HuntMarkVisual.generated.h"

class SHuntMarkGlyph;

/** 原创猎印固定在三维世界中；World Widget 的局部 +X 法线始终朝向本地玩家相机。 */
UCLASS(ClassGroup=(Visual), meta=(BlueprintSpawnableComponent))
class AICOMPANIONDEMO_API UHuntMarkVisual : public UWidgetComponent
{

	GENERATED_BODY()

public:
	UHuntMarkVisual(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 只交入显示层数；零层立即隐藏并停 Tick，不消费或修改宿主印记。 */
	UFUNCTION(BlueprintCallable, Category="HuntMark")
	void SetStacks(int32 InStacks);

	UFUNCTION(BlueprintPure, Category="HuntMark")
	int32 GetDisplayedStacks() const { return DisplayedStacks; }

	UFUNCTION(BlueprintPure, Category="HuntMark")
	bool IsHuntMarkWidgetReady() const;

	/** 读取真实组件/Slate 状态，供宿主验收桥使用；不触发重建或加载。 */
	UFUNCTION(BlueprintPure, Category="HuntMark|Debug")
	FString DebugVisualState() const;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void ReleaseResources() override;

private:
	void EnsureGlyphWidget();
	void FaceLocalCamera();

	UPROPERTY(Transient)
	int32 DisplayedStacks = 0;

	TSharedPtr<SHuntMarkGlyph> GlyphWidget;
};
