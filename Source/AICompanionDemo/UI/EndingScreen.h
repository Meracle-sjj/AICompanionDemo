/**
 * [INPUT]: UUserWidget/Slate全屏生命周期与真实时钟；EndingScreen无字纹理；GameMode正式终局与返回主菜单口
 * [OUTPUT]: 1920×1080等比不裁图感谢页，1秒真实渐入与按钮防误触；独立标题/统计/秋招实践反馈及确认邮箱；支持UIOnly键盘焦点；近门E键纯提示
 * [POS]: UI/终局展示；世界暂停、输入与奖励队列由GameMode/导演拥有。纹理UPROPERTY、brush/style成员保证GC与Slate地址稳定
 * [PROTOCOL]: 变更时更新本头部，然后检查UI/AGENTS.md和模块AGENTS.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "EndingScreen.generated.h"

class AAICompanionDemoGameMode;
class UTexture2D;
class SButton;
class STextBlock;
class SScaleBox;

UCLASS()
class AICOMPANIONDEMO_API UEndingScreen : public UUserWidget
{
	GENERATED_BODY()
public:
	UEndingScreen(const FObjectInitializer& ObjectInitializer);
	void Setup(AAICompanionDemoGameMode* InHost, float Seconds, int32 Kills, int32 Level);
	bool HasArtwork() const { return CoverTexture != nullptr; }
	UFUNCTION(BlueprintCallable, Category = "Debug|Ending")
	void DebugActivateEndingAction(FName Action);
	UFUNCTION(BlueprintPure, Category = "Debug|Ending")
	FString DebugPresentationState() const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
	FReply OnMenuClicked();
	FReply OnQuitClicked();
	bool AreActionsReady() const;
	void UpdateReveal();
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CoverTexture;
	FSlateBrush CoverBrush;
	FButtonStyle MenuStyle;
	FButtonStyle QuitStyle;
	TSharedPtr<SScaleBox> RevealLayer;
	TSharedPtr<SButton> MenuButton;
	TSharedPtr<SButton> QuitButton;
	TSharedPtr<STextBlock> FeedbackText;
	TSharedPtr<STextBlock> EmailText;
	double OpenRealSeconds = 0.0;
	static constexpr float RevealDuration = 1.f;
	float SurvivalSeconds = 0.f;
	int32 KillCount = 0;
	int32 FinalLevel = 1;
};

/** 近门纯提示，不拥有鼠标按钮或交互入口；E键由门读取并正式校验。 */
UCLASS()
class AICOMPANIONDEMO_API UEndingPortalPrompt : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetCanEnter(bool bAllowed);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
private:
	TSharedPtr<STextBlock> PromptText;
	bool bCanEnter = false;
};
