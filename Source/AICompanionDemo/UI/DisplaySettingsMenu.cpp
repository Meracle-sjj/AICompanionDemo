/**
 * [INPUT]: DisplaySettingsMenu契约、UE5.6 UGameUserSettings/Kismet分辨率查询、同层Slate按钮/滚动列表
 * [OUTPUT]: 三模式按钮与内嵌分辨率单选；取消Popup/SListView右键拖拽支路，背景鼠标不冒泡；设备选项/应用确认保存/ESC返回保持原契约
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
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const EWindowMode::Type DisplayModes[] = {EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen};
	const TCHAR* const DisplayModeLabels[] = {TEXT("窗口"), TEXT("无边框全屏"), TEXT("全屏")};
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
	Status.Empty();
	if (const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		PendingMode = Settings->GetFullscreenMode();
		PendingResolution = Settings->GetScreenResolution();
	}
	BuildResolutionOptions();
	RefreshChoiceStyles();
}

void UDisplaySettingsMenu::BuildResolutionOptions()
{
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
	ResolutionButtons.Reset();
	if (!ResolutionList) { return; }
	ResolutionList->ClearChildren();
	for (const FIntPoint Size : AvailableResolutions)
	{
		TSharedPtr<SButton> Choice;
		ResolutionList->AddSlot().Padding(0.f, 2.f)
		[
			SNew(SBox).MinDesiredHeight(36.f)
			[
				SAssignNew(Choice, SButton).ButtonStyle(Size == PendingResolution ? &SelectedChoiceStyle : &ButtonStyle)
				.ContentPadding(FMargin(12.f, 7.f)).HAlign(HAlign_Left).VAlign(VAlign_Center)
				.OnClicked_Lambda([this, Size]() { return OnResolutionClicked(Size); })
				[
					SNew(STextBlock).Text(FText::FromString(FormatResolution(Size)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
					.ColorAndOpacity(FLinearColor(0.91f, 0.96f, 0.98f)).AutoWrapText(false)
				]
			]
		];
		ResolutionButtons.Add(Choice);
	}
	const int32 SelectedIndex = AvailableResolutions.IndexOfByKey(PendingResolution);
	if (ResolutionButtons.IsValidIndex(SelectedIndex))
	{
		ResolutionList->ScrollDescendantIntoView(ResolutionButtons[SelectedIndex], false);
	}
}

void UDisplaySettingsMenu::RefreshChoiceStyles()
{
	if (!bStylesReady) { return; }
	for (int32 Index = 0; Index < ModeButtons.Num(); ++Index)
	{
		if (ModeButtons[Index] && Index < UE_ARRAY_COUNT(DisplayModes))
		{
			ModeButtons[Index]->SetButtonStyle(DisplayModes[Index] == PendingMode ? &SelectedChoiceStyle : &ButtonStyle);
		}
	}
	for (int32 Index = 0; Index < ResolutionButtons.Num(); ++Index)
	{
		if (ResolutionButtons[Index] && AvailableResolutions.IsValidIndex(Index))
		{
			ResolutionButtons[Index]->SetButtonStyle(AvailableResolutions[Index] == PendingResolution ? &SelectedChoiceStyle : &ButtonStyle);
		}
	}
}

FReply UDisplaySettingsMenu::OnModeClicked(EWindowMode::Type Mode)
{
	PendingMode = Mode;
	Status.Empty();
	BuildResolutionOptions();
	RefreshChoiceStyles();
	return FReply::Handled();
}

FReply UDisplaySettingsMenu::OnResolutionClicked(FIntPoint Resolution)
{
	if (PendingMode != EWindowMode::WindowedFullscreen && AvailableResolutions.Contains(Resolution))
	{
		PendingResolution = Resolution;
		Status.Empty();
		RefreshChoiceStyles();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> UDisplaySettingsMenu::RebuildWidget()
{
	if (!bStylesReady)
	{
		ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.045f, 0.11f, 0.14f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.10f, 0.25f, 0.30f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.045f, 0.18f, 0.23f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.06f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		SelectedChoiceStyle = ButtonStyle;
		SelectedChoiceStyle.SetNormal(FSlateColorBrush(FLinearColor(0.09f, 0.28f, 0.33f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.13f, 0.36f, 0.41f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.06f, 0.22f, 0.27f)));
		bStylesReady = true;
	}
	RefreshFromUserSettings();
	ModeButtons.Reset();
	ResolutionButtons.Reset();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	auto Text = [](const FString& Label, int32 Size = 16) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
			.ColorAndOpacity(FLinearColor(0.91f, 0.96f, 0.98f));
	};
	auto Button = [this, Text](const TCHAR* Label, FOnClicked Click) -> TSharedRef<SWidget>
	{
		return SNew(SBox).MinDesiredHeight(48.f)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(16.f, 10.f))
			.HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Click)[Text(Label)]
		];
	};
	TSharedRef<SHorizontalBox> ModeChoices = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DisplayModes); ++Index)
	{
		const EWindowMode::Type Mode = DisplayModes[Index];
		TSharedPtr<SButton> Choice;
		ModeChoices->AddSlot().FillWidth(1.f).Padding(Index == 0 ? 0.f : 4.f, 0.f, Index == 2 ? 0.f : 4.f, 0.f)
		[
			SNew(SBox).MinDesiredHeight(44.f)
			[
				SAssignNew(Choice, SButton).ButtonStyle(Mode == PendingMode ? &SelectedChoiceStyle : &ButtonStyle)
				.ContentPadding(FMargin(8.f, 10.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([this, Mode]() { return OnModeClicked(Mode); })
				[ Text(DisplayModeLabels[Index], 14) ]
			]
		];
		ModeButtons.Add(Choice);
	}
	TSharedRef<SWidget> Root = SNew(SOverlay).Cursor(EMouseCursor::Default)
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
							ModeChoices
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)[Text(TEXT("分辨率"), 14)]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox).HeightOverride(162.f)
							[
								SAssignNew(ResolutionList, SScrollBox).ScrollBarAlwaysVisible(true)
								.ScrollBarThickness(FVector2D(5.f, 5.f)).ScrollBarPadding(FMargin(6.f, 0.f, 0.f, 0.f))
								.AllowOverscroll(EAllowOverscroll::No).EnableTouchScrolling(false)
								.IsEnabled_Lambda([this]() { return PendingMode != EWindowMode::WindowedFullscreen; })
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 18.f)
						[
							SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(PendingMode == EWindowMode::WindowedFullscreen
								? FString::Printf(TEXT("无边框全屏使用桌面分辨率：%s。"), *FormatResolution(PendingResolution))
								: FString::Printf(TEXT("当前选择：%s · 滚轮浏览，单击选择。"), *FormatResolution(PendingResolution))); })
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
	ResolutionList->SetScrollBarRightClickDragAllowed(false);
	BuildResolutionOptions();
	RefreshChoiceStyles();
	return Root;
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

FReply UDisplaySettingsMenu::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 子按钮/滚动条先处理其左键；剩余背景或右键不冒泡到原生窗口、不进入拖拽捕获。
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UDisplaySettingsMenu::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled().ReleaseMouseCapture();
}

void UDisplaySettingsMenu::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ModeButtons.Reset();
	ResolutionButtons.Reset();
	ResolutionList.Reset();
	// style为成员：旧Slate树可能仍持有其地址，不在此提前清空。
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
	return FString::Printf(TEXT("inViewport=%d focusable=%d pendingMode=%d pending=%dx%d configuredMode=%d configured=%dx%d viewportMode=%d viewport=%dx%d options=%s status=%s picker=inline modeButtons=%d resolutionRows=%d"),
		IsInViewport(), IsFocusable(), static_cast<int32>(PendingMode), PendingResolution.X, PendingResolution.Y,
		Settings ? static_cast<int32>(Settings->GetFullscreenMode()) : -1, Configured.X, Configured.Y,
		ActualMode, Actual.X, Actual.Y, *Options, *Status, ModeButtons.Num(), ResolutionButtons.Num());
}

bool UDisplaySettingsMenu::DebugSelectDisplaySettings(int32 Width, int32 Height, int32 FullscreenMode)
{
#if WITH_EDITOR
	if (!IsInViewport() || FullscreenMode < 0 || FullscreenMode > 2) { return false; }
	OnModeClicked(EWindowMode::ConvertIntToWindowMode(FullscreenMode));
	const int32 ResolutionIndex = AvailableResolutions.IndexOfByKey(FIntPoint(Width, Height));
	if (!AvailableResolutions.IsValidIndex(ResolutionIndex)) { return false; }
	PendingResolution = AvailableResolutions[ResolutionIndex];
	RefreshChoiceStyles();
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
