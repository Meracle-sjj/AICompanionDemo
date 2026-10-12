/**
 * [INPUT]: UGameUserSettings模式/像素分辨率/保存、Slate当前游戏窗所在显示器指标、宿主关闭设置入口
 * [OUTPUT]: UDisplaySettingsMenu——三模式与同层分辨率单选；无边框随当前屏幕像素模式，应用重查；调试状态区分配置、视口与DPI
 * [POS]: UI/显示设置层；启动DPI感知归Config，窗口创建归UE；宿主保活与暂停输入仲裁，未应用选择不写Saved
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameUserSettings.h"
#include "Styling/SlateTypes.h"
#include "DisplaySettingsMenu.generated.h"

class AAICompanionDemoGameMode;
class SButton;
class SScrollBox;

UCLASS()
class AICOMPANIONDEMO_API UDisplaySettingsMenu : public UUserWidget
{
	GENERATED_BODY()
public:
	UDisplaySettingsMenu(const FObjectInitializer& ObjectInitializer);
	void BindHost(AAICompanionDemoGameMode* InHost);
	void RefreshFromUserSettings();
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugDisplaySettingsState() const;
	/** 编辑器验收：仅选设备列表中的分辨率；Mode使用UE配置值0全屏/1无边框/2窗口。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	bool DebugSelectDisplaySettings(int32 Width, int32 Height, int32 FullscreenMode);
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	void DebugActivateDisplayAction(FName Action);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
	void BuildResolutionOptions();
	void RefreshChoiceStyles();
	FReply OnModeClicked(EWindowMode::Type Mode);
	FReply OnResolutionClicked(FIntPoint Resolution);
	FReply OnApplyClicked();
	FReply OnBackClicked();
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	TArray<FIntPoint> AvailableResolutions;
	TArray<TSharedPtr<SButton>> ModeButtons;
	TArray<TSharedPtr<SButton>> ResolutionButtons;
	TSharedPtr<SScrollBox> ResolutionList;
	FIntPoint PendingResolution = FIntPoint(1280, 720);
	EWindowMode::Type PendingMode = EWindowMode::Windowed;
	FButtonStyle ButtonStyle;
	FButtonStyle SelectedChoiceStyle;
	FString Status;
	bool bStylesReady = false;
};
