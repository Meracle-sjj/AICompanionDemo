// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 EnhancedInput（IMC 注册管线）、UMG（移动端触摸控件，桌面零触发）
 * [OUTPUT]: 对外提供 AAICompanionDemoPlayerController——模板默认保留件：DefaultMappingContexts 注册 + 移动端控件装配，桌面ESC转交GameMode统一暂停（玩家输入的游戏侧响应在 Character，世界流转在 GameMode）
 * [POS]: 模板随附的标准 PlayerController；保留以维持 Enhanced Input 初始化管线——它不是本项目的决策面，改动前先确认职责归属
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AICompanionDemoPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AAICompanionDemoPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
	void HandlePauseKey();

};
