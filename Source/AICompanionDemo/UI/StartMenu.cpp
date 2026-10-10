/**
 * [INPUT]: StartMenu纹理/brush/style生命周期、无字合作封面、Slate等比布局与复合字体；GameMode开始/测试/显示设置接口
 * [OUTPUT]: 可焦点1920×1080等比封面页；左侧开始/测试入口，下方同排显示设置与退出，保留作品/制作周期标注与缺图菜单
 * [POS]: UI/ 开场展示实现。SStartMenuBackdrop只绘制背景，冻结/输入/测试模式仍由GameMode管理；所有文字均为Slate控件，封面不烘焙文字
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */

#include "StartMenu.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor Cyan(0.32f, 0.88f, 0.94f, 1.f);
	const FLinearColor White(0.94f, 0.97f, 0.99f, 1.f);
	const FLinearColor Muted(0.63f, 0.71f, 0.76f, 1.f);
	const FLinearColor Ink(0.025f, 0.07f, 0.09f, 1.f);

	FSlateFontInfo MenuFont(float Size, FName Weight = TEXT("Regular"), int32 Tracking = 0)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Weight, Size);
		Font.LetterSpacing = Tracking;
		return Font;
	}

	/** 固定设计画布内按比例绘图；渐变是绘制元素，不是阻挡按钮的命中控件。 */
	class SStartMenuBackdrop : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SStartMenuBackdrop) : _ArtBrush(nullptr) {}
			SLATE_ARGUMENT(const FSlateBrush*, ArtBrush)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			ArtBrush = Args._ArtBrush;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
		{
			const FVector2f Size(Geometry.GetLocalSize());
			if (ArtBrush && ArtBrush->GetResourceObject())
			{
				const FVector2f ArtSize(ArtBrush->ImageSize);
				const float Scale = FMath::Min(Size.X / ArtSize.X, Size.Y / ArtSize.Y);
				const FVector2f DrawSize = ArtSize * Scale;
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(DrawSize,
					FSlateLayoutTransform((Size - DrawSize) * 0.5f)), ArtBrush, ESlateDrawEffect::None,
					Style.GetColorAndOpacityTint());
			}

			TArray<FSlateGradientStop> LeftShade;
			LeftShade.Emplace(FVector2f(0.f, 0.f), FLinearColor(0.015f, 0.028f, 0.045f, 0.96f));
			LeftShade.Emplace(FVector2f(Size.X * 0.27f, 0.f), FLinearColor(0.015f, 0.028f, 0.045f, 0.90f));
			LeftShade.Emplace(FVector2f(Size.X * 0.48f, 0.f), FLinearColor(0.015f, 0.028f, 0.045f, 0.22f));
			LeftShade.Emplace(FVector2f(Size.X * 0.66f, 0.f), FLinearColor(0.015f, 0.028f, 0.045f, 0.f));
			// Slate枚举表示等色带方向：Vertical沿X插值，Horizontal沿Y插值（ElementBatcher）。
			FSlateDrawElement::MakeGradient(Elements, Layer + 1, Geometry.ToPaintGeometry(), LeftShade, Orient_Vertical);

			TArray<FSlateGradientStop> EdgeShade;
			EdgeShade.Emplace(FVector2f(0.f, 0.f), FLinearColor(0.01f, 0.02f, 0.035f, 0.24f));
			EdgeShade.Emplace(FVector2f(0.f, Size.Y * 0.45f), FLinearColor(0.01f, 0.02f, 0.035f, 0.f));
			EdgeShade.Emplace(FVector2f(0.f, Size.Y * 0.83f), FLinearColor(0.01f, 0.02f, 0.035f, 0.10f));
			EdgeShade.Emplace(FVector2f(0.f, Size.Y), FLinearColor(0.01f, 0.02f, 0.035f, 0.88f));
			FSlateDrawElement::MakeGradient(Elements, Layer + 2, Geometry.ToPaintGeometry(), EdgeShade, Orient_Horizontal);
			return Layer + 2;
		}

	private:
		const FSlateBrush* ArtBrush = nullptr;
	};
}

UStartMenu::UStartMenu(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UStartMenu::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
}

void UStartMenu::PrepareArtworkAndStyles()
{
	if (!CoverTexture)
	{
		CoverTexture = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/UI/StartScreen/T_StartCover_Coop_v1.T_StartCover_Coop_v1"));
		if (!CoverTexture)
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[StartMenu] 封面未载入，保留深色底与开始入口。"));
		}
	}
	CoverBrush.SetResourceObject(CoverTexture);
	// 明确使用源图比例，避免编辑器异步纹理编译期间的32×32占位尺寸改变构图。
	CoverBrush.SetImageSize(FVector2D(1672.f, 941.f));
	CoverBrush.DrawAs = ESlateBrushDrawType::Image;
	CoverBrush.ImageType = ESlateBrushImageType::FullColor;
	CoverBrush.TintColor = FSlateColor(FLinearColor::White);

	if (!bStylesReady)
	{
		PrimaryButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.29f, 0.82f, 0.88f, 1.f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.52f, 0.96f, 1.f, 1.f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.18f, 0.65f, 0.72f, 1.f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.17f, 0.29f, 0.32f, 1.f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		SecondaryButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.035f, 0.075f, 0.095f, 0.88f)))
			.SetHovered(FSlateColorBrush(FLinearColor(0.075f, 0.18f, 0.22f, 1.f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.04f, 0.12f, 0.15f, 1.f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.035f, 0.055f, 0.07f, 1.f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		TextButtonStyle.SetNormal(FSlateColorBrush(FLinearColor::Transparent))
			.SetHovered(FSlateColorBrush(FLinearColor(0.08f, 0.18f, 0.22f, 0.55f)))
			.SetPressed(FSlateColorBrush(FLinearColor(0.05f, 0.13f, 0.16f, 0.65f)))
			.SetDisabled(FSlateColorBrush(FLinearColor::Transparent))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		bStylesReady = true;
	}
}

TSharedRef<SWidget> UStartMenu::RebuildWidget()
{
	PrepareArtworkAndStyles();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

	auto Button = [this, WhiteBrush](const TCHAR* Label, const TCHAR* Detail, const TCHAR* Number,
		bool bPrimary, FOnClicked Click) -> TSharedRef<SWidget>
	{
		const FLinearColor TextColor = bPrimary ? Ink : White;
		return SNew(SBox).WidthOverride(460.f).HeightOverride(78.f)
		[
			SNew(SBorder).BorderImage(WhiteBrush).Padding(1.f)
			.BorderBackgroundColor(bPrimary ? Cyan : FLinearColor(0.24f, 0.44f, 0.49f, 0.85f))
			[
				SNew(SButton).ButtonStyle(bPrimary ? &PrimaryButtonStyle : &SecondaryButtonStyle)
				.ContentPadding(FMargin(22.f, 10.f)).OnClicked(Click)
				[
					SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 18.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(Number)).Font(MenuFont(13.f, TEXT("Medium"), 50))
						.ColorAndOpacity(TextColor.CopyWithNewOpacity(0.65f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(Label)).Font(MenuFont(20.f, TEXT("Medium"), 30))
							.ColorAndOpacity(TextColor).AutoWrapText(false)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Detail)).Font(MenuFont(12.f))
							.ColorAndOpacity(TextColor.CopyWithNewOpacity(0.72f)).AutoWrapText(false)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(MenuFont(23.f, TEXT("Light")))
						.ColorAndOpacity(TextColor)
					]
				]
			]
		];
	};

	return SNew(SBorder).BorderImage(WhiteBrush).BorderBackgroundColor(FLinearColor(0.01f, 0.018f, 0.028f))
		.Padding(0.f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(1920.f).HeightOverride(1080.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SStartMenuBackdrop).ArtBrush(&CoverBrush)]

					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(108.f, 68.f, 0.f, 0.f)
					[
						SNew(SBorder).BorderImage(WhiteBrush).BorderBackgroundColor(FLinearColor(0.92f, 0.79f, 0.50f, 0.8f))
						.Padding(1.f)
						[
							SNew(SBorder).BorderImage(WhiteBrush).BorderBackgroundColor(FLinearColor(0.04f, 0.075f, 0.095f, 0.95f))
							.Padding(FMargin(16.f, 8.f))
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("2027届秋招作品"))).Font(MenuFont(15.f, TEXT("Medium"), 35))
								.ColorAndOpacity(FLinearColor(0.93f, 0.82f, 0.59f))
							]
						]
					]

					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(108.f, 164.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(560.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("CO-OP  SURVIVAL"))).Font(MenuFont(13.f, TEXT("Medium"), 150))
								.ColorAndOpacity(Cyan)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 17.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("AI"))).Font(MenuFont(88.f, TEXT("Black"), -20))
								.ColorAndOpacity(White).AutoWrapText(false)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, -8.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("COMPANION"))).Font(MenuFont(51.f, TEXT("BoldCondensed"), 20))
								.ColorAndOpacity(White).AutoWrapText(false)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 22.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("与你并肩，迎战无尽。"))).Font(MenuFont(22.f, TEXT("Medium")))
								.ColorAndOpacity(Cyan).AutoWrapText(false)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("一个玩家，一个 AI 队友。\n在怪群与三位首领之间，相互支援。"))).Font(MenuFont(16.f))
								.ColorAndOpacity(Muted).AutoWrapText(false)
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 32.f, 0.f, 0.f)
							[
								Button(TEXT("开始游戏"), TEXT("与你的 AI 队友并肩作战"), TEXT("01"), true,
									FOnClicked::CreateUObject(this, &UStartMenu::OnStartClicked))
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 12.f, 0.f, 0.f)
							[
								Button(TEXT("测试模式"), TEXT("自由验证技能与首领"), TEXT("02"), false,
									FOnClicked::CreateUObject(this, &UStartMenu::OnTestClicked))
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 14.f, 0.f, 0.f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 22.f, 0.f)
								[
									SNew(SButton).ButtonStyle(&TextButtonStyle).ContentPadding(FMargin(3.f, 7.f))
									.OnClicked(FOnClicked::CreateUObject(this, &UStartMenu::OnSettingsClicked))
									[
										SNew(STextBlock).Text(FText::FromString(TEXT("显示设置"))).Font(MenuFont(14.f))
										.ColorAndOpacity(Cyan)
									]
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton).ButtonStyle(&TextButtonStyle).ContentPadding(FMargin(3.f, 7.f))
									.OnClicked(FOnClicked::CreateUObject(this, &UStartMenu::OnQuitClicked))
									[
										SNew(STextBlock).Text(FText::FromString(TEXT("退出游戏"))).Font(MenuFont(14.f))
										.ColorAndOpacity(Muted)
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 21.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("WASD 移动  ·  E 开箱"))).Font(MenuFont(14.f))
								.ColorAndOpacity(Muted.CopyWithNewOpacity(0.85f)).AutoWrapText(false)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("Shift 闪烁（升级解锁）"))).Font(MenuFont(12.f))
								.ColorAndOpacity(Muted.CopyWithNewOpacity(0.75f)).AutoWrapText(false)
							]
						]
					]

					+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(108.f, 0.f, 108.f, 48.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 15.f)
						[
							SNew(SBox).HeightOverride(1.f)
							[SNew(SBorder).BorderImage(WhiteBrush).BorderBackgroundColor(FLinearColor(0.40f, 0.62f, 0.69f, 0.25f)).Padding(0.f)]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("制作周期：整个国庆节假期"))).Font(MenuFont(16.f))
							.ColorAndOpacity(Muted).AutoWrapText(false)
						]
					]
				]
			]
		];
}

FReply UStartMenu::OnStartClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get())
	{
		GM->StartRun();
	}
	return FReply::Handled();
}

FReply UStartMenu::OnTestClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get())
	{
		GM->StartTestRun();
	}
	return FReply::Handled();
}

FReply UStartMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	return FReply::Handled();
}

FReply UStartMenu::OnSettingsClicked()
{
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ShowDisplaySettings(); }
	return FReply::Handled();
}

void UStartMenu::DebugActivateMenuAction(FName Action)
{
#if WITH_EDITOR
	if (!IsInViewport() || !Host.IsValid())
	{
		return;
	}
	if (Action == TEXT("start")) { OnStartClicked(); }
	else if (Action == TEXT("test")) { OnTestClicked(); }
	else if (Action == TEXT("settings")) { OnSettingsClicked(); }
#endif
}
