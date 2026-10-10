/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产）、Slate 的 SBorder/SVerticalBox/STextBlock/SButton、FPlatformTime 真实时钟（世界 dilation=0 定格下动画仍走——UMG tick 收到 dt=0，绝不可用世界时钟）、Kismet 的 GetCurrentLevelName/OpenLevel
 * [OUTPUT]: 对外提供 UDeathScreen——死亡结算全屏（Setup 注入统计）：黑幕 1.2s 渐入 → "你倒下了" + 存活/击杀/等级 → 2s 后浮现 [重新开始]（防误点缓冲）；点击重开 = OpenLevel 当前图全量重置
 * [POS]: UI/ 三件套之二（G2 终局幕）：玩家 HealthComponent::OnDeath → Character → GameMode::ShowDeathScreen 装配弹入（Z 序 10，盖 CompanionHud）；世界 dilation=0 定格即"舞台落幕"——WaveDirector/敌人/计时全部冻结
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DeathScreen.generated.h"

class SBorder;
class SVerticalBox;
class STextBlock;
class SButton;
class AAICompanionDemoGameMode;

UCLASS()
class AICOMPANIONDEMO_API UDeathScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 统计注入（GameMode 从 WaveDirector 快照装配） */
	void Setup(float SurvivalSeconds, int32 Kills, int32 Level);

	/** 组合根注入：倒地窗（1.5s）后回调 FreezeWorld——布娃娃需要真实世界时间倒下，定格不能跟着黑幕一起落 */
	void BindHost(AAICompanionDemoGameMode* InHost);

protected:
	/** 纯代码构建 Slate 树（零蓝图资产哲学） */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** 真实时钟驱动的渐入编排：黑幕 → 标题/统计 → 按钮 */
	void UpdateReveal();

	/** [重新开始] 点击：OpenLevel 当前图全量重置（dilation 随关卡重载归位） */
	FReply OnRestartClicked();

	/** 黑幕（渐入的真身）与中央内容 */
	TSharedPtr<SBorder> DimBorder;
	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> StatText;
	TSharedPtr<SButton> RestartButton;

	/** 弹入时刻（真实时钟基准）：所有动画相位从此起算 */
	double OpenRealSeconds = 0.0;

	/** 结算统计（Setup 注入） */
	float SurvivalSeconds = 0.f;
	int32 Kills = 0;
	int32 Level = 1;

	/** 组合根（FreezeWorld 宿主）与一次性定格标记 */
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	bool bFreezeSent = false;
};
