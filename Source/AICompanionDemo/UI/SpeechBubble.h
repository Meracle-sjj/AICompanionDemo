/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SBorder/STextBlock 与 FSlateRoundedBoxBrush（零资产圆角）、HAL 的 FPlatformTime（生命周期时钟——世界 TimeDilation=0 时 UMG 世界时钟是死的，铁律）
 * [OUTPUT]: 对外提供 USpeechBubble——头顶 3D 聊天气泡（Screen 空间覆盖层，检索定稿：WoW/ESO 惯例恒定字号不被遮挡）：ShowLine(LINE) 新词覆盖旧词+重置真实时钟；**早词缓存**（09-28 定罪：CreateWidget 后 Slate 树惰性构建，首词 ShowLine 时 LineText 未出生被静默丢弃→空气泡——PendingLine 缓存待 RebuildWidget 补挂）；三段状态机=浮现 0.2s 上浮淡入 → 停留 4.5s → 淡出 0.25s
 * [POS]: UI/ 四件套之一（09-26 夜轮，与 CompanionHud/DeathScreen/StartMenu 姊妹）：由 CompanionCharacter 的 WidgetComponent 装载（DrawSize/Pivot/MaxDrawDistance 挂载侧定）；未来敌人/玩家复用时组件与方法随迁
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
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
	/** 显示一句台词：新词覆盖旧词并重置生命周期（WoW 惯例——新指令压过闲聊，不做队列） */
	void ShowLine(const FString& Line, float Duration = 4.5f);

protected:
	/** 纯代码构建 Slate 树：圆角深底 + 台词白字（AutoWrapText 防长句溢出） */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** 生命周期状态机：真实时钟差分驱动（FPlatformTime——世界冻结期唯一活的钟） */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TSharedPtr<SBorder> BubbleBorder;
	TSharedPtr<STextBlock> LineText;

	/** 气泡底板 brush（UniquePtr 持有——无默认构造，UHT 生成代码要求值成员可默认构造，CompanionHud 同案） */
	TUniquePtr<FSlateRoundedBoxBrush> BubbleBrush;

	/** 生命周期：浮现 0.2s（上浮 8px 淡入）→ 停留 Duration → 淡出 0.25s */
	double ShowTime = 0.0;
	float HoldDuration = 4.5f;
	bool bShowing = false;

	/** 早词缓存（09-28 空气泡案）：Slate 树建好前到达的台词先存此，RebuildWidget 末尾补挂 */
	FString PendingLine;
	float PendingDuration = 4.5f;
};
