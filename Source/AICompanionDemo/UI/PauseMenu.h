/**
 * [INPUT]: UUserWidget/Slate、宿主GameMode统一暂停仲裁与恢复/设置/回主菜单接口
 * [OUTPUT]: UPauseMenu——恢复、显示设置、返回主菜单、退出的可操作暂停页；ESC复用宿主恢复；Debug桥仅供真实入口验收
 * [POS]: UI/展示层；GameMode保活并控制世界与输入，本类不修改GamePaused或时间流速
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "PauseMenu.generated.h"

class AAICompanionDemoGameMode;

UCLASS()
class AICOMPANIONDEMO_API UPauseMenu : public UUserWidget
{
	GENERATED_BODY()
public:
	UPauseMenu(const FObjectInitializer& ObjectInitializer);
	void BindHost(AAICompanionDemoGameMode* InHost);
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugPauseMenuState() const;
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	void DebugActivatePauseAction(FName Action);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	FReply OnResumeClicked();
	FReply OnSettingsClicked();
	FReply OnMainMenuClicked();
	FReply OnQuitClicked();
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	FButtonStyle ButtonStyle;
	bool bStylesReady = false;
};
