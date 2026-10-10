/**
 * [INPUT]: PauseMenu契约、Slate实际按钮、宿主暂停状态和流转接口
 * [OUTPUT]: 适配小窗口的暂停页；按钮及单次ESC走同一宿主回调、消费ESC重复事件，状态回读不改变玩法
 * [POS]: UI/暂停展示；纯色brush/style成员保稳定地址，不拥有奖励/死亡/终局的暂停状态
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */
#include "PauseMenu.h"

#include "AICompanionDemoGameMode.h"
#include "Brushes/SlateColorBrush.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

UPauseMenu::UPauseMenu(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UPauseMenu::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
}

TSharedRef<SWidget> UPauseMenu::RebuildWidget()
{
	if (!bStylesReady)
	{
		ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.045f, 0.11f, 0.14f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.10f, 0.25f, 0.30f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.045f, 0.18f, 0.23f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.06f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		bStylesReady = true;
	}
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	auto Button = [this](const TCHAR* Label, FOnClicked Click) -> TSharedRef<SWidget>
	{
		return SNew(SBox).MinDesiredHeight(52.f)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(18.f, 12.f))
			.HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Click)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
				.ColorAndOpacity(FLinearColor(0.91f, 0.96f, 0.98f))
			]
		];
	};
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(WhiteBrush).Padding(0.f)
			.BorderBackgroundColor(FLinearColor(0.005f, 0.012f, 0.022f, 0.78f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
			[
				SNew(SBox).WidthOverride(480.f)
				[
					SNew(SBorder).BorderImage(WhiteBrush).Padding(26.f)
					.BorderBackgroundColor(FLinearColor(0.025f, 0.065f, 0.085f, 0.98f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("游戏已暂停")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 25))
							.ColorAndOpacity(FLinearColor(0.32f, 0.88f, 0.94f))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 20.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("按 ESC 或选择继续，回到当前进度。")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
							.ColorAndOpacity(FLinearColor(0.65f, 0.74f, 0.78f))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
						[ Button(TEXT("继续游戏"), FOnClicked::CreateUObject(this, &UPauseMenu::OnResumeClicked)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
						[ Button(TEXT("显示设置"), FOnClicked::CreateUObject(this, &UPauseMenu::OnSettingsClicked)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
						[ Button(TEXT("返回主菜单"), FOnClicked::CreateUObject(this, &UPauseMenu::OnMainMenuClicked)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
						[ Button(TEXT("退出游戏"), FOnClicked::CreateUObject(this, &UPauseMenu::OnQuitClicked)) ]
					]
				]
			]
		];
}

FReply UPauseMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return InKeyEvent.IsRepeat() ? FReply::Handled() : OnResumeClicked();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UPauseMenu::OnResumeClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ResumeRunFromPause(); }
	return FReply::Handled();
}

FReply UPauseMenu::OnSettingsClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ShowDisplaySettings(); }
	return FReply::Handled();
}

FReply UPauseMenu::OnMainMenuClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ReturnToMainMenu(); }
	return FReply::Handled();
}

FReply UPauseMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	return FReply::Handled();
}

FString UPauseMenu::DebugPauseMenuState() const
{
	const AAICompanionDemoGameMode* GM = Host.Get();
	return FString::Printf(TEXT("inViewport=%d focusable=%d hostValid=%d gameplayPaused=%d width=%.1f height=%.1f"),
		IsInViewport(), IsFocusable(), GM != nullptr, GM && GM->IsGameplayPaused(),
		GetCachedGeometry().GetLocalSize().X, GetCachedGeometry().GetLocalSize().Y);
}

void UPauseMenu::DebugActivatePauseAction(FName Action)
{
#if WITH_EDITOR
	if (!IsInViewport() || !Host.IsValid()) { return; }
	if (Action == TEXT("resume")) { OnResumeClicked(); }
	else if (Action == TEXT("settings")) { OnSettingsClicked(); }
	else if (Action == TEXT("menu")) { OnMainMenuClicked(); }
	else if (Action == TEXT("quit")) { OnQuitClicked(); }
#endif
}
