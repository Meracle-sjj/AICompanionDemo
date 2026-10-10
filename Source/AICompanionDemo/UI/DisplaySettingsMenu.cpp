/**
 * [INPUT]: DisplaySettingsMenu契约、UE5.6 UGameUserSettings/Kismet分辨率查询、纯Slate按钮/下拉列表
 * [OUTPUT]: 实际窗口与分辨率应用/确认/保存；按当前设备生成选项，显示配置值与真实viewport回读；单次ESC/返回只关闭设置、消费ESC重复事件
 * [POS]: UI/显示设置实现；不修改GamePaused/dilation、不重载用户配置或全局写默认值
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */
#include "DisplaySettingsMenu.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const EWindowMode::Type DisplayModes[] = {EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen};
	FString FormatResolution(FIntPoint Resolution)
	{
		return FString::Printf(TEXT("%d × %d"), Resolution.X, Resolution.Y);
	}
}

UDisplaySettingsMenu::UDisplaySettingsMenu(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UDisplaySettingsMenu::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
	RefreshFromUserSettings();
}

void UDisplaySettingsMenu::RefreshFromUserSettings()
{
	TGuardValue<bool> Guard(bRefreshingOptions, true);
	Status.Empty();
	if (const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		PendingMode = Settings->GetFullscreenMode();
		PendingResolution = Settings->GetScreenResolution();
	}
	ModeOptions = {MakeShared<FString>(TEXT("窗口")), MakeShared<FString>(TEXT("无边框全屏")), MakeShared<FString>(TEXT("全屏"))};
	int32 ModeIndex = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DisplayModes); ++Index)
	{
		if (DisplayModes[Index] == PendingMode) { ModeIndex = Index; break; }
	}
	SelectedMode = ModeOptions[ModeIndex];
	BuildResolutionOptions();
	if (ModeCombo) { ModeCombo->RefreshOptions(); ModeCombo->SetSelectedItem(SelectedMode); }
}

void UDisplaySettingsMenu::BuildResolutionOptions()
{
	TGuardValue<bool> Guard(bRefreshingOptions, true);
	AvailableResolutions.Reset();
	FIntPoint Desktop = FIntPoint::ZeroValue;
	if (const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings()) { Desktop = Settings->GetDesktopResolution(); }
	if (PendingMode == EWindowMode::WindowedFullscreen)
	{
		AvailableResolutions.Add(Desktop.X > 0 && Desktop.Y > 0 ? Desktop : PendingResolution);
	}
	else if (PendingMode == EWindowMode::Fullscreen)
	{
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(AvailableResolutions);
		if (AvailableResolutions.IsEmpty())
		{
			AvailableResolutions.Add(Desktop.X > 0 && Desktop.Y > 0 ? Desktop : PendingResolution);
			Status = TEXT("无法查询设备分辨率，暂时保留当前显示尺寸。");
		}
	}
	else
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(AvailableResolutions);
		if (PendingResolution.X > 0 && PendingResolution.Y > 0) { AvailableResolutions.AddUnique(PendingResolution); }
		for (const FIntPoint Size : {FIntPoint(960, 540), FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080)})
		{
			if (Desktop.X <= 0 || Desktop.Y <= 0 || (Size.X <= Desktop.X && Size.Y <= Desktop.Y)) { AvailableResolutions.AddUnique(Size); }
		}
	}
	AvailableResolutions.RemoveAll([](const FIntPoint& Size) { return Size.X < 640 || Size.Y < 360; });
	if (AvailableResolutions.IsEmpty()) { AvailableResolutions.Add(FIntPoint(1280, 720)); }
	AvailableResolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	if (!AvailableResolutions.Contains(PendingResolution))
	{
		PendingResolution = AvailableResolutions.Contains(Desktop) ? Desktop : AvailableResolutions[0];
	}
	ResolutionOptions.Reset();
	SelectedResolution.Reset();
	for (const FIntPoint Size : AvailableResolutions)
	{
		TSharedPtr<FString> Option = MakeShared<FString>(FormatResolution(Size));
		ResolutionOptions.Add(Option);
		if (Size == PendingResolution) { SelectedResolution = Option; }
	}
	if (ResolutionCombo) { ResolutionCombo->RefreshOptions(); ResolutionCombo->SetSelectedItem(SelectedResolution); }
}

TSharedRef<SWidget> UDisplaySettingsMenu::RebuildWidget()
{
	RefreshFromUserSettings();
	if (!bStylesReady)
	{
		ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.045f, 0.11f, 0.14f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.10f, 0.25f, 0.30f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.045f, 0.18f, 0.23f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.06f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		ComboStyle = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox"));
		ComboStyle.ComboButtonStyle.SetMenuBorderBrush(FSlateColorBrush(FLinearColor(0.035f, 0.075f, 0.10f, 1.f)))
			.SetMenuBorderPadding(FMargin(4.f));
		OptionRowStyle = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ComboBox.Row"));
		const FSlateColorBrush Clear(FLinearColor::Transparent);
		const FSlateColorBrush Hover(FLinearColor(0.09f, 0.21f, 0.26f));
		const FSlateColorBrush Selected(FLinearColor(0.12f, 0.35f, 0.40f));
		OptionRowStyle.SetEvenRowBackgroundBrush(Clear).SetOddRowBackgroundBrush(Clear)
			.SetEvenRowBackgroundHoveredBrush(Hover).SetOddRowBackgroundHoveredBrush(Hover)
			.SetActiveBrush(Selected).SetActiveHoveredBrush(Selected)
			.SetInactiveBrush(Selected).SetInactiveHoveredBrush(Selected);
		bStylesReady = true;
	}
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	auto Text = [](const FString& Label, int32 Size = 16) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
			.ColorAndOpacity(FLinearColor(0.91f, 0.96f, 0.98f));
	};
	auto OptionWidget = [Text](TSharedPtr<FString> Option) -> TSharedRef<SWidget>
	{
		return SNew(SBox).MinDesiredHeight(34.f).Padding(FMargin(8.f, 5.f))[Text(Option ? *Option : FString())];
	};
	auto Button = [this, Text](const TCHAR* Label, FOnClicked Click) -> TSharedRef<SWidget>
	{
		return SNew(SBox).MinDesiredHeight(48.f)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(16.f, 10.f))
			.HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Click)[Text(Label)]
		];
	};
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(WhiteBrush).Padding(0.f)
			.BorderBackgroundColor(FLinearColor(0.005f, 0.012f, 0.022f, 0.86f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
			[
				SNew(SBox).WidthOverride(560.f)
				[
					SNew(SBorder).BorderImage(WhiteBrush).Padding(24.f)
					.BorderBackgroundColor(FLinearColor(0.025f, 0.065f, 0.085f, 0.99f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(TEXT("显示设置"), 25)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 18.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("选择显示方式与分辨率，然后应用并保存。\n返回不会保存尚未应用的选择。")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).ColorAndOpacity(FLinearColor(0.65f, 0.74f, 0.78f))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Text(TEXT("显示方式"), 14)]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(ModeCombo, SComboBox<TSharedPtr<FString>>).OptionsSource(&ModeOptions)
							.ComboBoxStyle(&ComboStyle).ButtonStyle(&ButtonStyle).ItemStyle(&OptionRowStyle).HasDownArrow(false)
							.ContentPadding(FMargin(12.f, 10.f)).ForegroundColor(FLinearColor::White)
							.InitiallySelectedItem(SelectedMode).OnGenerateWidget_Lambda(OptionWidget)
							.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice, ESelectInfo::Type)
							{
								if (bRefreshingOptions || !Choice) { return; }
								const int32 Index = ModeOptions.IndexOfByKey(Choice);
								if (Index >= 0 && Index < UE_ARRAY_COUNT(DisplayModes))
								{
									SelectedMode = Choice;
									PendingMode = DisplayModes[Index];
									Status.Empty();
									BuildResolutionOptions();
								}
							})
							[
								SNew(STextBlock).Text_Lambda([this]() { return FText::FromString((SelectedMode ? *SelectedMode : FString()) + TEXT("  ▾")); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).ColorAndOpacity(FLinearColor::White)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)[Text(TEXT("分辨率"), 14)]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(ResolutionCombo, SComboBox<TSharedPtr<FString>>).OptionsSource(&ResolutionOptions)
							.ComboBoxStyle(&ComboStyle).ButtonStyle(&ButtonStyle).ItemStyle(&OptionRowStyle).HasDownArrow(false)
							.ContentPadding(FMargin(12.f, 10.f)).ForegroundColor(FLinearColor::White)
							.IsEnabled_Lambda([this]() { return PendingMode != EWindowMode::WindowedFullscreen; })
							.InitiallySelectedItem(SelectedResolution).OnGenerateWidget_Lambda(OptionWidget)
							.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice, ESelectInfo::Type)
							{
								if (bRefreshingOptions || !Choice) { return; }
								const int32 Index = ResolutionOptions.IndexOfByKey(Choice);
								if (AvailableResolutions.IsValidIndex(Index))
								{
									SelectedResolution = Choice;
									PendingResolution = AvailableResolutions[Index];
									Status.Empty();
								}
							})
							[
								SNew(STextBlock).Text_Lambda([this]() { return FText::FromString((SelectedResolution ? *SelectedResolution : FString()) + TEXT("  ▾")); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).ColorAndOpacity(FLinearColor::White)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 18.f)
						[
							SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(PendingMode == EWindowMode::WindowedFullscreen
								? TEXT("无边框全屏使用桌面分辨率。") : TEXT("设置保存在本机，下次启动继续使用。")); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).ColorAndOpacity(FLinearColor(0.65f, 0.74f, 0.78f))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
							[ Button(TEXT("应用并保存"), FOnClicked::CreateUObject(this, &UDisplaySettingsMenu::OnApplyClicked)) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[ Button(TEXT("返回"), FOnClicked::CreateUObject(this, &UDisplaySettingsMenu::OnBackClicked)) ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status.IsEmpty() ? TEXT("ESC 返回") : Status); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).WrapTextAt(500.f)
							.ColorAndOpacity(FLinearColor(0.32f, 0.88f, 0.94f))
						]
					]
				]
			]
		];
}

FReply UDisplaySettingsMenu::OnApplyClicked()
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		if (PendingResolution.X <= 0 || PendingResolution.Y <= 0 || !AvailableResolutions.Contains(PendingResolution)) { return FReply::Handled(); }
		Settings->SetScreenResolution(PendingResolution);
		Settings->SetFullscreenMode(PendingMode);
		Settings->ApplyResolutionSettings(false);
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
		Status = TEXT("已保存显示设置。");
		UE_LOG(LogAICompanionDemo, Log, TEXT("[DisplaySettings] Saved mode=%d resolution=%dx%d"),
			static_cast<int32>(Settings->GetFullscreenMode()), Settings->GetScreenResolution().X, Settings->GetScreenResolution().Y);
	}
	else { Status = TEXT("显示设置暂不可用。"); }
	return FReply::Handled();
}

FReply UDisplaySettingsMenu::OnBackClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->CloseDisplaySettings(); }
	return FReply::Handled();
}

FReply UDisplaySettingsMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return InKeyEvent.IsRepeat() ? FReply::Handled() : OnBackClicked();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UDisplaySettingsMenu::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ModeCombo.Reset();
	ResolutionCombo.Reset();
	// style/options为成员：旧Slate树可能仍持有其地址，不在此提前清空。
}

FString UDisplaySettingsMenu::DebugDisplaySettingsState() const
{
	const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	const FIntPoint Configured = Settings ? Settings->GetScreenResolution() : FIntPoint::ZeroValue;
	FIntPoint Actual = FIntPoint::ZeroValue;
	int32 ActualMode = -1;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Actual = GEngine->GameViewport->Viewport->GetSizeXY();
		ActualMode = static_cast<int32>(GEngine->GameViewport->Viewport->GetWindowMode());
	}
	FString Options;
	for (const FIntPoint Size : AvailableResolutions) { Options += FString::Printf(TEXT("%dx%d;"), Size.X, Size.Y); }
	return FString::Printf(TEXT("inViewport=%d focusable=%d pendingMode=%d pending=%dx%d configuredMode=%d configured=%dx%d viewportMode=%d viewport=%dx%d options=%s status=%s"),
		IsInViewport(), IsFocusable(), static_cast<int32>(PendingMode), PendingResolution.X, PendingResolution.Y,
		Settings ? static_cast<int32>(Settings->GetFullscreenMode()) : -1, Configured.X, Configured.Y,
		ActualMode, Actual.X, Actual.Y, *Options, *Status);
}

bool UDisplaySettingsMenu::DebugSelectDisplaySettings(int32 Width, int32 Height, int32 FullscreenMode)
{
#if WITH_EDITOR
	if (!IsInViewport() || FullscreenMode < 0 || FullscreenMode > 2) { return false; }
	PendingMode = EWindowMode::ConvertIntToWindowMode(FullscreenMode);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DisplayModes); ++Index)
	{
		if (DisplayModes[Index] == PendingMode)
		{
			SelectedMode = ModeOptions[Index];
			if (ModeCombo) { TGuardValue<bool> Guard(bRefreshingOptions, true); ModeCombo->SetSelectedItem(SelectedMode); }
		}
	}
	BuildResolutionOptions();
	const int32 ResolutionIndex = AvailableResolutions.IndexOfByKey(FIntPoint(Width, Height));
	if (!AvailableResolutions.IsValidIndex(ResolutionIndex)) { return false; }
	PendingResolution = AvailableResolutions[ResolutionIndex];
	SelectedResolution = ResolutionOptions[ResolutionIndex];
	if (ResolutionCombo) { TGuardValue<bool> Guard(bRefreshingOptions, true); ResolutionCombo->SetSelectedItem(SelectedResolution); }
	return true;
#else
	return false;
#endif
}

void UDisplaySettingsMenu::DebugActivateDisplayAction(FName Action)
{
#if WITH_EDITOR
	if (!IsInViewport() || !Host.IsValid()) { return; }
	if (Action == TEXT("apply")) { OnApplyClicked(); }
	else if (Action == TEXT("back")) { OnBackClicked(); }
#endif
}
