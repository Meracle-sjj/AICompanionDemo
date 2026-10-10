/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产）、Slate 的 SOverlay/SBorder/SBox/SSpacer、FPlatformTime 真实时钟（世界冻结期也要能闪——世界时钟不可用）
 * [OUTPUT]: 对外提供 UDamageFlash——受击警示层 v3（09-24 用户裁决：方向不重要，删箭头；要"血迹般的闪红"）：
 *   全屏淡红脉冲（峰值 0.18 快闪）+ 四边红色血条（峰值 0.55 略慢消散）——"屏幕稍微闪红一下"的纯色近似（血迹贴图待 Fab 素材）；
 *   Pulse() 触发，全部真实时钟衰减
 * [POS]: UI/ 家族第四件：GameMode::NotifyPlayerHurt() 脉冲入口；塌缩教训两连（v1 全屏层、菜单幕布均空内容零尺寸）——所有可视元素 SSpacer 撑满保命
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageFlash.generated.h"

class SBorder;

UCLASS()
class AICOMPANIONDEMO_API UDamageFlash : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 受击脉冲：全屏闪红 + 四边血条同时亮起（重复受击重冲） */
	void Pulse();

protected:
	/** 纯代码构建 Slate 树（零蓝图资产哲学） */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** 真实时钟驱动的衰减 */
	void UpdateReveal();

	/** 全屏淡红层（"闪红一下"的真身）与四边血条 */
	TSharedPtr<SBorder> FullFlash;
	TSharedPtr<SBorder> EdgeBars[4];

	/** 上次受击时刻（真实时钟）：负值=从未受击 */
	double PulseRealSeconds = -10.0;
};
