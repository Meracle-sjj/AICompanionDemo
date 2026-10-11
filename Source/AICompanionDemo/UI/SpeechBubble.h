/**
 * [INPUT]: UUserWidget/Slate圆角树、实际起播词/时长/类别和Audio显式奖励继续旗，Slate DeltaTime及真实世界暂停状态。
 * [OUTPUT]: 固定584px中文换行；普通奖励冻结、紧急继续演出，真实暂停全部停计；早词保完整参数，空词清字幕/旗标。
 * [POS]: UI/ 四件套之一（09-26 夜轮，与 CompanionHud/DeathScreen/StartMenu 姊妹）：由 CompanionCharacter 的 WidgetComponent 装载（DrawSize/Pivot/MaxDrawDistance 挂载侧定）；未来敌人/玩家复用时组件与方法随迁
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Events/RunEventLog.h"
#include "Blueprint/UserWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "SpeechBubble.generated.h"

class SBorder;
class STextBlock;

UCLASS()
class AICOMPANIONDEMO_API USpeechBubble : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 实际起播新词覆盖旧词并重置生命周期；空词立即清除，排队归Audio。 */
	void ShowLine(const FString& Line, float Duration = 4.5f, ERunEventKind Kind = ERunEventKind::General,
		bool bContinueDuringReward = false);
	UFUNCTION(BlueprintPure, Category="Debug|UI") FString DebugSpeechBubbleState() const;

protected:
	/** 纯代码构建圆角深底与固定宽度中文字幕，禁用基于旧分配尺寸的自动换行。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Slate时钟驱动；真实暂停全冻结，奖励期仅Audio显式允许的当前词继续。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TSharedPtr<SBorder> BubbleBorder;
	TSharedPtr<STextBlock> LineText;

	/** 气泡底板 brush（UniquePtr 持有——无默认构造，UHT 生成代码要求值成员可默认构造，CompanionHud 同案） */
	TUniquePtr<FSlateRoundedBoxBrush> BubbleBrush;

	/** 生命周期：浮现 0.2s（上浮 8px 淡入）→ 停留 Duration → 淡出 0.25s */

	float HoldDuration = 4.5f;
	bool bShowing = false;
	bool bContinuesDuringReward = false;

	/** 早词缓存（09-28 空气泡案）：Slate 树建好前到达的台词先存此，RebuildWidget 末尾补挂 */
	FString PendingLine;
	float PendingDuration = 4.5f;
	ERunEventKind PendingKind = ERunEventKind::General;
	bool bPendingContinueDuringReward = false;
	float ElapsedSeconds = 0.f;
};
