/**
 * [INPUT]: 依赖 Engine 的 ACharacter（胶囊/网格/移动组件继承）与 UMG 的 UWidgetComponent（头顶气泡装载）、同目录 CompanionAIController 的 StaticClass 作为默认控制器、UI/SpeechBubble 的气泡 widget
 * [OUTPUT]: AI队友Pawn自动接管与Screen气泡装载；ShowSpeechBubble转交实际起播词/时长/类别及显式奖励继续旗，不持语音优先级规则。
 * [POS]: Source/AICompanionDemo/AI 的成员，M1 跟随主体；与玩家 AICompanionDemoCharacter 平行不继承（玩家类绑相机弹簧臂与输入，队友职责隔离）；外观由 BP 子类装配 SKM_Manny（玩家默认 Quinn，视觉区分）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Events/RunEventLog.h"
#include "GameFramework/Character.h"
#include "CompanionCharacter.generated.h"

class UWidgetComponent;
class USpeechBubble;

/**
 * AI 队友角色：无输入、无相机，仅作为被 AIController 驱动的 Pawn 存在。
 * 移动手感与玩家同规格（转向朝移动方向），保证跟随动画自然。
 */
UCLASS()
class AICOMPANIONDEMO_API ACompanionCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACompanionCharacter();
	virtual void BeginPlay() override;

	/** Audio传入当前词/时长/类别及奖励期继续旗；默认普通冻结，真实暂停归UI检查。 */
	void ShowSpeechBubble(const FString& Line, float Duration = 4.5f, ERunEventKind Kind = ERunEventKind::General,
		bool bContinueDuringReward = false);

private:
	/** 头顶气泡装载（Screen 模式参数在 BeginPlay 定稿：DrawSize 620×140 / 底中锚定 / Screen空间） */
	UPROPERTY(VisibleAnywhere, Category = "Companion|UI")
	UWidgetComponent* SpeechBubble;

	/** 气泡 widget 实例（纯 C++ 类零蓝图资产；BeginPlay 期 PlayerController 可能未就绪——ShowSpeechBubble 惰性建） */
	UPROPERTY(Transient)
	USpeechBubble* BubbleWidget = nullptr;
};
