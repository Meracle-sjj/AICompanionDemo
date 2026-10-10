/**
 * [INPUT]: UUserWidget/Slate、UGameUserSettings真实窗口/分辨率/保存API、Kismet系统可用分辨率查询；宿主关闭设置入口
 * [OUTPUT]: UDisplaySettingsMenu——窗口/无边框/全屏与分辨率选择、应用并保存、返回；打开读取已有设置，未应用的选择不写配置
 * [POS]: UI/显示设置层；宿主保活与输入/暂停仲裁，不强制重置用户Saved配置、不持有玩法状态
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameUserSettings.h"
#include "Styling/SlateTypes.h"
#include "DisplaySettingsMenu.generated.h"

class AAICompanionDemoGameMode;
template<typename OptionType> class SComboBox;

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
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
	void BuildResolutionOptions();
	FReply OnApplyClicked();
	FReply OnBackClicked();
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	TArray<TSharedPtr<FString>> ModeOptions;
	TArray<TSharedPtr<FString>> ResolutionOptions;
	TArray<FIntPoint> AvailableResolutions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModeCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ResolutionCombo;
	TSharedPtr<FString> SelectedMode;
	TSharedPtr<FString> SelectedResolution;
	FIntPoint PendingResolution = FIntPoint(1280, 720);
	EWindowMode::Type PendingMode = EWindowMode::Windowed;
	FButtonStyle ButtonStyle;
	FComboBoxStyle ComboStyle;
	FTableRowStyle OptionRowStyle;
	FString Status;
	bool bStylesReady = false;
	bool bRefreshingOptions = false;
};
