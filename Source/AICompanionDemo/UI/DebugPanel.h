/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SOverlay/SScrollBox/SVerticalBox/SHorizontalBox/SButton/STextBlock/SBorder/SBox、FCoreStyle 默认字体、本模块 AICompanionDemoGameMode 的调试转发组
 * [OUTPUT]: 测试分区控制台与独立常驻F1键帽/动作/状态提示（收起后仍可见且不拦截鼠标）；只读GameMode原提示，DebugPanelState保留原全文并回读实际显示文字/分配几何
 * [POS]: UI/ 界面层第七件（与 CompanionHud 姊妹）：仅测试模式挂载（StartTestRun→ShowDebugPanel），正常模式零痕迹
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "DebugPanel.generated.h"

class AAICompanionDemoGameMode;
class SBorder;
class SBox;
class STextBlock;

UCLASS()
class AICOMPANIONDEMO_API UDebugPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 宿主注入（GameMode 组合根——全部按钮回调经宿主调试转发组执行） */
	void BindHost(AAICompanionDemoGameMode* InHost);

	/** 只读原模式提示、实际显示文字与期望/分配几何，不将代码存在当作实机验收。 */
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugPanelState() const;

	/** 编辑器验收桥：复用真正的收起/展开逻辑。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	void DebugSetCollapsed(bool bInCollapsed);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	/** 明确长标签换行宽度，0表示单行；不使用依赖上一帧分配宽度的AutoWrapText。 */
	TSharedRef<SWidget> MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick, float WrapWidth = 260.f);
	void SetCollapsed(bool bInCollapsed);
	void PrepareButtonStyle();

	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	TSharedPtr<SBorder> ExpandedPanel;
	TSharedPtr<SBorder> CollapsedBar;
	TSharedPtr<SBorder> TestInputHintPanel;
	TSharedPtr<SBox> TestInputKeycap;
	TSharedPtr<STextBlock> TestInputAction;
	TSharedPtr<STextBlock> TestInputHint;
	TArray<TWeakPtr<STextBlock>> LabelWidgets;
	FButtonStyle ButtonStyle;
	bool bStylesReady = false;
	bool bCollapsed = false;
	int32 RenderedUpgradeCount = 0;
};
