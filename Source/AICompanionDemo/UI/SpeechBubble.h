/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SBorder/STextBlock 与 FSlateRoundedBoxBrush（零资产圆角）、世界游戏DeltaTime与暂停/奖励冻结状态
 * [OUTPUT]: 对外提供 USpeechBubble——头顶 3D 聊天气泡（Screen 空间覆盖层，检索定稿：WoW/ESO 惯例恒定字号不被遮挡）：ShowLine(LINE,Duration,Kind)接收播放台词/时长/类别并重置游戏时钟；**早词缓存**（09-28 定罪：CreateWidget 后 Slate 树惰性构建，首词 ShowLine 时 LineText 未出生被静默丢弃→空气泡——PendingLine 缓存待 RebuildWidget 补挂）；三段状态机=浮现 0.2s 上浮淡入 → 停留 4.5s → 淡出 0.25s
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
	/** 显示一句台词：新词覆盖旧词并重置生命周期（WoW 惯例——新指令压过闲聊，队列归Audio子系统） */
	void ShowLine(const FString& Line, float Duration = 4.5f, ERunEventKind Kind = ERunEventKind::General);

protected:
	/** 纯代码构建 Slate 树：圆角深底 + 台词白字（AutoWrapText 防长句溢出） */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** 生命周期状态机：游戏时钟驱动；暂停/奖励冻结保留当前字幕 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TSharedPtr<SBorder> BubbleBorder;
	TSharedPtr<STextBlock> LineText;

	/** 气泡底板 brush（UniquePtr 持有——无默认构造，UHT 生成代码要求值成员可默认构造，CompanionHud 同案） */
	TUniquePtr<FSlateRoundedBoxBrush> BubbleBrush;

	/** 生命周期：浮现 0.2s（上浮 8px 淡入）→ 停留 Duration → 淡出 0.25s */

	float HoldDuration = 4.5f;
	bool bShowing = false;

	/** 早词缓存（09-28 空气泡案）：Slate 树建好前到达的台词先存此，RebuildWidget 末尾补挂 */
	FString PendingLine;
	float PendingDuration = 4.5f;
	ERunEventKind PendingKind = ERunEventKind::General;
	float ElapsedSeconds = 0.f;
};
