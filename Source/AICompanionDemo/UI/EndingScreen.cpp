/**
 * [INPUT]: EndingScreen.h的纹理/brush/style所有权；Slate设计画布与FPlatformTime；GameMode正式按钮回调
 * [OUTPUT]: 黑底上1秒真实时钟渐入完整源图/独立文字，按钮同期禁用并回调复验；秋招实践反馈与确认邮箱、UIOnly焦点；近门E纯提示不截获输入
 * [POS]: UI/Ending展示实现；不推进战斗、不拥有奖励账本，验收桥复用按钮回调
 * [PROTOCOL]: 变更时更新本头部，然后检查UI/AGENTS.md和模块AGENTS.md
 */
#include "EndingScreen.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
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
	const FLinearColor Gold(1.f, .84f, .49f);
	const FLinearColor Cream(.98f, .96f, .90f);
	const FLinearColor Soft(.76f, .77f, .83f);
	FSlateFontInfo EndingFont(float Size, FName Weight = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
	FButtonStyle SolidButton(const FLinearColor& Normal, const FLinearColor& Hover)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateColorBrush(Normal)).SetHovered(FSlateColorBrush(Hover))
			.SetPressed(FSlateColorBrush(Normal * .8f))
			.SetDisabled(FSlateColorBrush(FLinearColor(.17f, .16f, .23f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		return Style;
	}
	class SEndingBackdrop : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEndingBackdrop) : _ArtBrush(nullptr) {}
			SLATE_ARGUMENT(const FSlateBrush*, ArtBrush)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args) { ArtBrush = Args._ArtBrush; SetVisibility(EVisibility::HitTestInvisible); }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const FVector2f Size(Geometry.GetLocalSize());
			if (ArtBrush && ArtBrush->GetResourceObject())
			{
				const FVector2f Source(ArtBrush->ImageSize);
				const FVector2f DrawSize = Source * FMath::Min(Size.X / Source.X, Size.Y / Source.Y);
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(DrawSize,
					FSlateLayoutTransform((Size - DrawSize) * .5f)), ArtBrush, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
			}
			TArray<FSlateGradientStop> Shade;
			// 自绘渐变也沿父层透明度，否则图渐入时暗幕仍瞬间出现。
			const float Alpha = Style.GetColorAndOpacityTint().A;
			Shade.Emplace(FVector2f(0.f, 0.f), FLinearColor(.018f, .013f, .035f, .97f * Alpha));
			Shade.Emplace(FVector2f(Size.X * .27f, 0.f), FLinearColor(.018f, .013f, .035f, .86f * Alpha));
			Shade.Emplace(FVector2f(Size.X * .56f, 0.f), FLinearColor(.018f, .013f, .035f, .10f * Alpha));
			Shade.Emplace(FVector2f(Size.X, 0.f), FLinearColor(.018f, .013f, .035f, .03f * Alpha));
			FSlateDrawElement::MakeGradient(Elements, Layer + 1, Geometry.ToPaintGeometry(), Shade, Orient_Vertical);
			return Layer + 1;
		}
	private:
		const FSlateBrush* ArtBrush = nullptr;
	};
}

UEndingScreen::UEndingScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SObjectWidget::SupportsKeyboardFocus经NativeSupportsKeyboardFocus读取此旗，必须在装配前设置。
	SetIsFocusable(true);
}

void UEndingScreen::Setup(AAICompanionDemoGameMode* InHost, float Seconds, int32 Kills, int32 Level)
{
	Host = InHost;
	SurvivalSeconds = Seconds;
	KillCount = Kills;
	FinalLevel = Level;
}

TSharedRef<SWidget> UEndingScreen::RebuildWidget()
{
	if (!CoverTexture)
	{
		CoverTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/EndingScreen/T_Ending_Coop_v1.T_Ending_Coop_v1"));
		if (!CoverTexture) { UE_LOG(LogAICompanionDemo, Warning, TEXT("[Ending] 无字图尚未加载，保留感谢文字与菜单按钮。")); }
	}
	CoverBrush.SetResourceObject(CoverTexture);
	// 使用实际源图比例；最终资源尺寸由导入证据固定，防异步32px占位影响构图。
	CoverBrush.SetImageSize(FVector2D(1672.f, 941.f));
	CoverBrush.DrawAs = ESlateBrushDrawType::Image;
	CoverBrush.ImageType = ESlateBrushImageType::FullColor;
	MenuStyle = SolidButton(FLinearColor(.91f, .73f, .38f), FLinearColor(1.f, .88f, .62f));
	QuitStyle = SolidButton(FLinearColor(.09f, .065f, .14f, .94f), FLinearColor(.20f, .14f, .29f));
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const int32 Seconds = FMath::Max(0, FMath::FloorToInt(SurvivalSeconds));
	OpenRealSeconds = FPlatformTime::Seconds();
	return SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(.015f, .012f, .026f)).Padding(0.f)
	[
		SAssignNew(RevealLayer, SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both).RenderOpacity(0.f)
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(1920.f).HeightOverride(1080.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SEndingBackdrop).ArtBrush(&CoverBrush)]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(120.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(660.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[SNew(STextBlock).Text(FText::FromString(TEXT("归途已至  ·  并肩而归"))).Font(EndingFont(21.f, TEXT("Medium"))).ColorAndOpacity(Gold)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 23.f, 0.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("感谢游玩"))).Font(EndingFont(78.f, TEXT("Bold"))).ColorAndOpacity(Cream)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 30.f, 0.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("沙狱蠕虫已被击倒。\n谢谢你，与 AI 队友一起走到这里。"))).Font(EndingFont(24.f)).ColorAndOpacity(Soft)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 26.f, 0.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("存活  %d:%02d    击杀  %d    等级  Lv.%d"), Seconds / 60, Seconds % 60, KillCount, FinalLevel)))
							.Font(EndingFont(20.f, TEXT("Medium"))).ColorAndOpacity(Gold)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 28.f, 0.f, 0.f)
						[SAssignNew(FeedbackText, STextBlock)
							.Text(FText::FromString(TEXT("本作为个人秋招作品，也是我的首次 Unreal Engine 开发实践。受经验与制作时间所限，可能仍有疏漏。若你发现问题，欢迎通过邮件反馈；感谢你的试玩与帮助。")))
							.Font(EndingFont(23.f)).ColorAndOpacity(Cream).WrapTextAt(660.f)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 17.f, 0.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("反馈邮箱"))).Font(EndingFont(17.f)).ColorAndOpacity(Soft)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
						[SAssignNew(EmailText, STextBlock).Text(FText::FromString(TEXT("shijunjian2024@mail.ustc.edu.cn"))).Font(EndingFont(24.f, TEXT("Medium"))).ColorAndOpacity(Gold)]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 30.f, 0.f, 0.f)
						[
							SNew(SBox).WidthOverride(410.f).HeightOverride(76.f)
							[
								SAssignNew(MenuButton, SButton).ButtonStyle(&MenuStyle).HAlign(HAlign_Center).VAlign(VAlign_Center).IsEnabled(false)
								.OnClicked(FOnClicked::CreateUObject(this, &UEndingScreen::OnMenuClicked))
								[SNew(STextBlock).Text(FText::FromString(TEXT("返回主菜单"))).Font(EndingFont(24.f, TEXT("Medium"))).ColorAndOpacity(FLinearColor(.06f, .04f, .10f))]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 14.f, 0.f, 0.f)
						[
							SNew(SBox).WidthOverride(410.f).HeightOverride(66.f)
							[
								SAssignNew(QuitButton, SButton).ButtonStyle(&QuitStyle).HAlign(HAlign_Center).VAlign(VAlign_Center).IsEnabled(false)
								.OnClicked(FOnClicked::CreateUObject(this, &UEndingScreen::OnQuitClicked))
								[SNew(STextBlock).Text(FText::FromString(TEXT("退出游戏"))).Font(EndingFont(21.f)).ColorAndOpacity(Cream)]
							]
						]
					]
				]
			]
		]
	];
}

bool UEndingScreen::AreActionsReady() const
{
	return IsInViewport() && OpenRealSeconds > 0.0 && FPlatformTime::Seconds() - OpenRealSeconds >= RevealDuration;
}

void UEndingScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateReveal();
}

void UEndingScreen::UpdateReveal()
{
	// 世界已GamePaused，游戏DeltaTime/Timer不推进；Slate帧只读取独立真实时钟。
	const float Alpha = FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - OpenRealSeconds) / RevealDuration), 0.f, 1.f);
	if (RevealLayer.IsValid()) { RevealLayer->SetRenderOpacity(Alpha * Alpha * (3.f - 2.f * Alpha)); }
	const bool bReady = AreActionsReady();
	if (MenuButton.IsValid()) { MenuButton->SetEnabled(bReady); }
	if (QuitButton.IsValid()) { QuitButton->SetEnabled(bReady); }
}

void UEndingScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	RevealLayer.Reset(); MenuButton.Reset(); QuitButton.Reset(); FeedbackText.Reset(); EmailText.Reset();
	// 纹理和成员brush/style须继续保活，旧SObjectWidget树可能尚未释放。
}

FReply UEndingScreen::OnMenuClicked()
{
	if (!AreActionsReady()) { return FReply::Handled(); }
	if (AAICompanionDemoGameMode* GM = Host.Get()) { GM->ReturnToMainMenu(); }
	return FReply::Handled();
}

FReply UEndingScreen::OnQuitClicked()
{
	if (!AreActionsReady()) { return FReply::Handled(); }
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	return FReply::Handled();
}

FString UEndingScreen::DebugPresentationState() const
{
#if WITH_EDITOR
	auto Quote = [](FString Text)
	{
		Text.ReplaceInline(TEXT("\\"), TEXT("\\\\")); Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Text.ReplaceInline(TEXT("\n"), TEXT("\\n")); Text.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		return FString(TEXT("\"")) + Text + TEXT("\"");
	};
	return FString::Printf(TEXT("{\"realElapsed\":%.4f,\"fadeDuration\":%.2f,\"renderOpacity\":%.4f,\"actionsReady\":%s,\"menuEnabled\":%s,\"quitEnabled\":%s,\"feedbackText\":%s,\"emailText\":%s}"),
		FMath::Max(0.0, FPlatformTime::Seconds() - OpenRealSeconds), RevealDuration,
		RevealLayer.IsValid() ? RevealLayer->GetRenderOpacity() : 0.f, AreActionsReady() ? TEXT("true") : TEXT("false"),
		MenuButton.IsValid() && MenuButton->IsEnabled() ? TEXT("true") : TEXT("false"),
		QuitButton.IsValid() && QuitButton->IsEnabled() ? TEXT("true") : TEXT("false"),
		*Quote(FeedbackText.IsValid() ? FeedbackText->GetText().ToString() : FString()),
		*Quote(EmailText.IsValid() ? EmailText->GetText().ToString() : FString()));
#else
	return TEXT("{}");
#endif
}

void UEndingScreen::DebugActivateEndingAction(FName Action)
{
#if WITH_EDITOR
	if (!IsInViewport() || !Host.IsValid()) { return; }
	if (Action == TEXT("menu")) { OnMenuClicked(); }
	else if (Action == TEXT("quit")) { OnQuitClicked(); }
#endif
}

void UEndingPortalPrompt::SetCanEnter(bool bAllowed)
{
	if (bCanEnter == bAllowed) { return; }
	bCanEnter = bAllowed;
	if (PromptText.IsValid())
	{
		PromptText->SetText(FText::FromString(bAllowed ? TEXT("E  进入传送门") : TEXT("先领取完当前奖励")));
	}
}

TSharedRef<SWidget> UEndingPortalPrompt::RebuildWidget()
{
	return SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 100.f)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Gold).Padding(2.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(.17f, .08f, .26f, .96f)).Padding(FMargin(30.f, 14.f))
				[
					SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[SAssignNew(PromptText, STextBlock).Text(FText::FromString(bCanEnter ? TEXT("E  进入传送门") : TEXT("先领取完当前奖励"))).Font(EndingFont(23.f, TEXT("Medium"))).ColorAndOpacity(Gold)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f, 0.f, 0.f)
					[SNew(STextBlock).Text(FText::FromString(TEXT("WASD 继续移动  ·  奖励领取优先"))).Font(EndingFont(13.f)).ColorAndOpacity(Soft)]
				]
			]
		];
}

