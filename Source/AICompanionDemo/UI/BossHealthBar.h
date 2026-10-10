/**
 * [INPUT]: 依赖 UMG 的 UUserWidget（纯 C++ 无蓝图资产——RebuildWidget 手搭 Slate 树）、Slate 的 SOverlay/SProgressBar/STextBlock、本模块 HealthComponent 的血量读数（轮询源）
 * [OUTPUT]: 对外提供 UBossHealthBar——Boss 头顶血条（WidgetComponent Screen 空间装载）：BindSource 注入血量组件与 Boss 名，NativeTick 0.1s 轮询百分比与数字
 * [POS]: UI/ 界面层第六件（Boss 波 1，与 CompanionHud 姊妹）：由 BossBase 的 WidgetComponent 装载；真·大血条（全屏下方）留波 5 视觉包
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BossHealthBar.generated.h"

class UHealthComponent;
class SProgressBar;
class STextBlock;

UCLASS()
class AICOMPANIONDEMO_API UBossHealthBar : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 数据注入（BossBase BeginPlay 调用）：血量组件弱引用 + Boss 名 */
	void BindSource(UHealthComponent* InHealth, const FString& InBossName);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TSharedPtr<SProgressBar> HealthBar;
	TSharedPtr<STextBlock> NameText;

	TWeakObjectPtr<UHealthComponent> Health;
	FString BossName = TEXT("BOSS");
};
