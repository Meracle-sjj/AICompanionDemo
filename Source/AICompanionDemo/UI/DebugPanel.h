/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SOverlay/SScrollBox/SVerticalBox/SHorizontalBox/SButton/STextBlock/SBorder/SBox、FCoreStyle 默认字体、本模块 AICompanionDemoGameMode 的调试转发组
 * [OUTPUT]: 对外提供 UDebugPanel——测试模式调试面板（09-30 用户立项"验收提速专用"）：右侧常驻 360px 滚动列，
 *   四节按钮（BOSS 召唤×3 / 怪物刷删 / 玩家金币等级无敌 / 全卡直加 24 张）——BindHost 注入 GameMode，全部回调经宿主调试转发组
 * [POS]: UI/ 界面层第七件（与 CompanionHud 姊妹）：仅测试模式挂载（StartTestRun→ShowDebugPanel），正常模式零痕迹
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DebugPanel.generated.h"

class AAICompanionDemoGameMode;
class SScrollBox;

UCLASS()
class AICOMPANIONDEMO_API UDebugPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 宿主注入（GameMode 组合根——全部按钮回调经宿主调试转发组执行） */
	void BindHost(AAICompanionDemoGameMode* InHost);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** 通用小按钮（测试面板统一样式：小字+深底） */
	TSharedRef<SWidget> MakeDebugButton(const FString& Label, const TFunction<void()>& OnClick);

	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
};
