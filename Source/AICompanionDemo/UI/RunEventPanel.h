/**
 * [INPUT]: Events/RunEventLog的每世界有界记录、UMG/Slate。
 * [OUTPUT]: 左下事件记录、自动滚至最新记录；只读真实字幕与Boss提示，不参与玩法判定。
 * [POS]: GameMode装配的常驻展示层；离局拆除，记录生命周期归WorldSubsystem。
 * [PROTOCOL]: 变更时更新此头部，然后检查AGENTS.md。
 */
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RunEventPanel.generated.h"
class SVerticalBox;
class SScrollBox;
UCLASS()
class AICOMPANIONDEMO_API URunEventPanel : public UUserWidget
{
 GENERATED_BODY()
public:
 URunEventPanel(const FObjectInitializer& Initializer);
 UFUNCTION(BlueprintPure, Category="Debug|UI") FString DebugEventPanelState() const;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
 virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
 void RefreshEntries();
 TSharedPtr<SVerticalBox> Rows;
 TSharedPtr<SScrollBox> Scroll;
 int64 LastRevision = -1;
 int32 DisplayedRows = 0;
};
