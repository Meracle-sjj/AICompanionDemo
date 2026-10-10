/**
 * [INPUT]: UUserWidget/SObjectWidget 生命周期、稳定 Slate brush/style、/Game/UI/StartScreen 的无字合作封面；宿主 GameMode 的 StartRun/StartTestRun/ShowDisplaySettings 与 Kismet QuitGame
 * [OUTPUT]: UStartMenu——可接受设置返回焦点的等比封面开始页；开始游戏、测试模式、显示设置与退出；UPROPERTY保活纹理，编辑器验收桥复用真实按钮回调
 * [POS]: UI/ 开场展示；GameMode 以 Z10 装配并拥有冻结和输入状态，菜单不持有游戏账本。Brush/style 为成员值，随标准 SObjectWidget 所有权存活
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "StartMenu.generated.h"

class AAICompanionDemoGameMode;
class SButton;
class UTexture2D;

UCLASS()
class AICOMPANIONDEMO_API UStartMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	UStartMenu(const FObjectInitializer& ObjectInitializer);

	/** 组合根注入（开始回调经宿主 StartRun 解冻世界） */
	void BindHost(AAICompanionDemoGameMode* InHost);

	/** 仅编辑器验收：共用真实按钮回调；非编辑器构建无操作。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|StartMenu")
	void DebugActivateMenuAction(FName Action);

protected:
	/** 纯代码构建 Slate 树（零蓝图资产哲学） */
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** [开始游戏]：宿主 StartRun（世界解冻 + 菜单退场） */
	FReply OnStartClicked();

	/** [测试模式]：宿主 StartTestRun（跳过正常节奏，调试面板常驻） */
	FReply OnTestClicked();

	/** 标题页显示设置，宿主负责设置面板及焦点。 */
	FReply OnSettingsClicked();

	/** [退出游戏]：QuitGame（PIE 内即停 PIE） */
	FReply OnQuitClicked();

	/** 组合根（StartRun 的宿主） */
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;

	void PrepareArtworkAndStyles();

	/** Slate 的资源对象引用不能代替 GC 所有权。 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CoverTexture;

	FSlateBrush CoverBrush;
	FButtonStyle PrimaryButtonStyle;
	FButtonStyle SecondaryButtonStyle;
	FButtonStyle TextButtonStyle;
	bool bStylesReady = false;
};
