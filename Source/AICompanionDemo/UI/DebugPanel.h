/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SOverlay/SScrollBox/SVerticalBox/SHorizontalBox/SButton/STextBlock/SBorder/SBox、FCoreStyle 默认字体、本模块 AICompanionDemoGameMode 的调试转发组
 * [OUTPUT]: UDebugPanel——测试模式右侧滚动列；固定顶部返回主菜单/收起，收起后保留展开条；按钮按实际字体测量和自然期望高度排版，DebugPanelState回读文字几何
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
class STextBlock;

UCLASS()
class AICOMPANIONDEMO_API UDebugPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 宿主注入（GameMode 组合根——全部按钮回调经宿主调试转发组执行） */
	void BindHost(AAICompanionDemoGameMode* InHost);

	/** 只读实际Slate文字期望/分配高度，不将代码存在当作实机验收。 */
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugPanelState() const;

	/** 编辑器验收桥：复用真正的收起/展开逻辑。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	void DebugSetCollapsed(bool bInCollapsed);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	/** 通用小按钮（测试面板统一样式：小字+深底） */
	TSharedRef<SWidget> MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick);
	void SetCollapsed(bool bInCollapsed);
	void PrepareButtonStyle();

	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	TSharedPtr<SBorder> ExpandedPanel;
	TSharedPtr<SBorder> CollapsedBar;
	TArray<TWeakPtr<STextBlock>> LabelWidgets;
	FButtonStyle ButtonStyle;
	bool bStylesReady = false;
	bool bCollapsed = false;
	int32 RenderedUpgradeCount = 0;
};
