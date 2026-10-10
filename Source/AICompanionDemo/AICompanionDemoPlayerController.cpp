// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 EnhancedInputSubsystem（本地玩家 IMC 注入）、SVirtualJoystick 触摸判定、AICompanionDemo.h 日志类别
 * [OUTPUT]: BeginPlay（触摸控件装配）与 SetupInputComponent（IMC 注册）两处模板实现，逻辑零扩展
 * [POS]: .h 声明的实现面；模板默认代码——本项目游戏逻辑不在此层
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "AICompanionDemoPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "AICompanionDemo.h"
#include "Widgets/Input/SVirtualJoystick.h"

void AAICompanionDemoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogAICompanionDemo, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AAICompanionDemoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}
