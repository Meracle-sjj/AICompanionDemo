/**
 * [INPUT]: UpgradeCardView.h 展示契约、UpgradeCardPresentation 的41项元数据/既有纹理与四类调色、Slate图文布局/原生几何、宿主真实动态描述
 * [OUTPUT]: 三张真实强化卡与可选独立Flip页脚（F快捷提示/双箭头），锁箱六卡与Boss遗物三卡不带Flip；304卡面、ScaleToFit/DownOnly、成员style和弱委托生命周期保持
 * [POS]: UI/ 纯展示实现；Build 只消费 FUpgradeCardOption，OnClicked 只执行 OnSelected。UObject/纹理/brush/style 的生命周期由持有此视图的宿主保证
 * [PROTOCOL]: 变更时更新此头部，然后检查 UI/AGENTS.md 与父级 AGENTS.md
 */

#include "UpgradeCardView.h"

#include "AICompanionDemo.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateResourceHandle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	int32 RarityStyleIndex(const EUpgradeCardRarity Rarity)
	{
		return static_cast<int32>(Rarity);
	}

	/** 额外Flip按钮的纯几何双箭头，不持有转换/奖励状态。 */
	class SRewardPoolSwitchSymbol : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRewardPoolSwitchSymbol) {}
		SLATE_END_ARGS()
		void Construct(const FArguments&) { SetCanTick(false); }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(256.f, 256.f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FVector2f Scale = FVector2f(Geometry.GetLocalSize()) / 64.f;
			const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
			auto DrawStroke = [&](TArray<FVector2f> Points, const FLinearColor& Color)
			{
				for (FVector2f& Point : Points) { Point = Point * Scale; }
				FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
					Effects, Color * Style.GetColorAndOpacityTint(), true, 5.f);
			};
			DrawStroke({ {12.f,24.f}, {49.f,24.f}, {40.f,15.f} }, FLinearColor(0.24f,0.75f,1.f));
			DrawStroke({ {49.f,24.f}, {40.f,33.f} }, FLinearColor(0.24f,0.75f,1.f));
			DrawStroke({ {52.f,43.f}, {15.f,43.f}, {24.f,52.f} }, FLinearColor(0.78f,0.45f,1.f));
			DrawStroke({ {15.f,43.f}, {24.f,34.f} }, FLinearColor(0.78f,0.45f,1.f));
			return Layer;
		}
	};

	/** 原生几何印记；不依赖 emoji 字体/外部贴图，也不接收点击。 */
	class SFusionRecommendationSeal : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFusionRecommendationSeal) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&)
		{
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.f, 64.f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FVector2f Scale = FVector2f(Geometry.GetLocalSize()) / 64.f;
			const FLinearColor Gold = UpgradeCardPresentation::GetRarityColor(EUpgradeCardRarity::Legendary);
			const FLinearColor Red(0.60f, 0.035f, 0.055f, 1.f);
			const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
			// CustomVerts 的空资源句柄由 Slate RHI 使用白纹理；颜色直接来自顶点。
			auto DrawConvex = [&](const TArray<FVector2f>& Points, const FLinearColor& Color, int32 DrawLayer)
			{
				TArray<FSlateVertex> Vertices;
				TArray<SlateIndex> Indices;
				for (const FVector2f& Point : Points)
				{
					FSlateVertex Vertex = FSlateVertex::Make<ESlateVertexRounding::Disabled>(
						Geometry.GetAccumulatedRenderTransform(), Point * Scale, FVector2f(0.5f, 0.5f),
						(Color * Style.GetColorAndOpacityTint()).ToFColor(true));
					Vertex.MaterialTexCoords = FVector2f::ZeroVector;
					Vertex.PixelSize[0] = Vertex.PixelSize[1] = 0;
					Vertices.Add(Vertex);
				}
				for (int32 Index = 1; Index + 1 < Vertices.Num(); ++Index)
				{
					Indices.Add(0); Indices.Add(static_cast<SlateIndex>(Index)); Indices.Add(static_cast<SlateIndex>(Index + 1));
				}
				FSlateDrawElement::MakeCustomVerts(Elements, DrawLayer, FSlateResourceHandle(), Vertices, Indices,
					nullptr, 0, 0, Effects);
			};
			auto StampOutline = [](float Radius)
			{
				TArray<FVector2f> Points;
				for (int32 Index = 0; Index < 12; ++Index)
				{
					const float Angle = (Index + 0.5f) * 2.f * PI / 12.f;
					Points.Add(FVector2f(32.f + FMath::Cos(Angle) * Radius, 32.f + FMath::Sin(Angle) * Radius));
				}
				return Points;
			};
			DrawConvex(StampOutline(31.f), Gold, Layer);
			DrawConvex(StampOutline(27.5f), Red, Layer + 1);
			// 腕口、竖起拇指、握拳掌部均为凸多边形，互相搭接组成清晰手势。
			DrawConvex({ {12.f, 31.f}, {21.f, 31.f}, {21.f, 49.f}, {12.f, 49.f} }, Gold, Layer + 2);
			DrawConvex({ {26.f, 34.f}, {28.f, 14.f}, {32.f, 10.f}, {36.f, 14.f}, {34.f, 34.f} }, Gold, Layer + 2);
			DrawConvex({ {23.f, 31.f}, {33.f, 27.f}, {51.f, 27.f}, {54.f, 31.f},
				{51.f, 48.f}, {47.f, 51.f}, {29.f, 51.f}, {23.f, 47.f} }, Gold, Layer + 2);
			for (const float Y : {34.f, 40.f, 46.f})
			{
				TArray<FVector2f> Seam = { FVector2f(41.f, Y) * Scale, FVector2f(51.f, Y) * Scale };
				FSlateDrawElement::MakeLines(Elements, Layer + 3, Geometry.ToPaintGeometry(), MoveTemp(Seam),
					Effects, Red * Style.GetColorAndOpacityTint(), true, 1.6f);
			}
			return Layer + 3;
		}
	};
}

void UUpgradeCardView::PrepareButtonStyle()
{
	if (bButtonStyleReady)
	{
		return;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FLinearColor Color = Index == 3 ? FLinearColor(0.85f, 0.045f, 0.075f, 1.f)
			: UpgradeCardPresentation::GetRarityColor(static_cast<EUpgradeCardRarity>(Index));
		auto Shade = [&Color](float Strength)
		{
			return FLinearColor(0.018f + Color.R * Strength, 0.022f + Color.G * Strength,
				0.032f + Color.B * Strength, 1.f);
		};
		CardButtonStyles[Index].SetNormal(FSlateColorBrush(Shade(0.14f)))
			.SetHovered(FSlateColorBrush(Shade(0.26f)))
			.SetPressed(FSlateColorBrush(Shade(0.34f)))
			.SetDisabled(FSlateColorBrush(FLinearColor(0.025f, 0.03f, 0.045f, 1.f)))
			.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
	}
	bButtonStyleReady = true;
}

const FSlateBrush* UUpgradeCardView::ResolveBrush(FName UpgradeId)
{
	if (const TSharedPtr<FSlateBrush>* Existing = ArtBrushes.Find(UpgradeId))
	{
		return Existing->Get();
	}
	const TCHAR* TexturePath = UpgradeCardPresentation::GetCardTextureAssetPath(UpgradeId);
	UTexture2D* Texture = TexturePath ? LoadObject<UTexture2D>(nullptr, TexturePath) : nullptr;
	TSharedPtr<FSlateBrush> Brush;
	if (Texture)
	{
		LoadedTextures.Add(UpgradeId, Texture);
		Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Texture);
		Brush->SetImageSize(FVector2D(Texture->GetSizeX(), Texture->GetSizeY()));
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->ImageType = ESlateBrushImageType::FullColor;
		Brush->TintColor = FSlateColor(FLinearColor::White);
	}
	else
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[UpgradeUI] 升级插图未载入：%s；使用纯色底。"), *UpgradeId.ToString());
		Brush = MakeShared<FSlateColorBrush>(FLinearColor(0.025f, 0.065f, 0.095f, 1.f));
		Brush->SetImageSize(FVector2D(1.f, 1.f));
	}
	ArtBrushes.Add(UpgradeId, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> UUpgradeCardView::MakeCard(const FUpgradeCardOption& Option, const bool bChest)
{
	const UpgradeCardPresentation::FCardMetadata* Metadata = UpgradeCardPresentation::FindCardMetadata(Option.UpgradeId);
	const FString Title = Metadata ? Metadata->Title : TEXT("未知强化");
	const FString Category = Metadata ? Metadata->Category : TEXT("强化");
	const FString Description = !Option.ShortDescriptionOverride.IsEmpty() ? Option.ShortDescriptionOverride
		: (Metadata ? FString(Metadata->ShortDescription) : Option.FullDescription);
	const float ArtSize = bChest ? 220.f : 272.f;
	const bool bFlipArt = Option.UpgradeId == FName(TEXT("Flip"));
	const FSlateBrush* ArtBrush = bFlipArt ? nullptr : ResolveBrush(Option.UpgradeId);
	TSharedRef<SWidget> Artwork = SNew(SImage).Image(ArtBrush);
	if (bFlipArt) { Artwork = SNew(SRewardPoolSwitchSymbol); }
	const FSimpleDelegate OnSelected = Option.OnSelected;
	const FLinearColor RarityColor = UpgradeCardPresentation::GetRarityColor(Option.Rarity);
	const FLinearColor BorderColor = RarityColor * (Option.bTaken ? 0.28f : 0.90f);
	const bool bShowFusionSeal = Option.Rarity == EUpgradeCardRarity::Legendary && Option.bFusionLegendary;

	return SNew(SBox).WidthOverride(304.f).HeightOverride(bChest ? 410.f : 488.f)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.Padding(1.f).BorderBackgroundColor(FLinearColor(BorderColor.R, BorderColor.G, BorderColor.B, 1.f))
			[
				SNew(SButton).ButtonStyle(&CardButtonStyles[RarityStyleIndex(Option.Rarity)]).ContentPadding(FMargin(14.f))
				.IsFocusable(false).IsEnabled(!Option.bTaken)
				.ToolTipText(FText::FromString(Option.FullDescription))
				.OnClicked_Lambda([OnSelected]() { OnSelected.ExecuteIfBound(); return FReply::Handled(); })
				[
					SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SBox).WidthOverride(ArtSize).HeightOverride(ArtSize)
						[
							SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both)
							[
								Artwork
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock).Text(FText::FromString(Category))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(RarityColor)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Text(FText::FromString(UpgradeCardPresentation::GetRarityLabel(Option.Rarity)))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(RarityColor)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(Title))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", bChest ? 22 : 24))
						.ColorAndOpacity(FMath::Lerp(RarityColor, FLinearColor::White, 0.55f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(Description))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", bChest ? 16 : 18))
						.ColorAndOpacity(FLinearColor(0.78f, 0.86f, 0.92f))
						.AutoWrapText(false).WrapTextAt(272.f)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBox)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("[%d]"), Option.Slot + 1)))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
							.ColorAndOpacity(RarityColor)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Text(FText::FromString(Option.bTaken ? TEXT("✓ 已领取") : TEXT("点击选择")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
							.ColorAndOpacity(Option.bTaken ? FLinearColor(0.46f, 0.48f, 0.52f) : RarityColor)
						]
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SFusionRecommendationSeal)
			.Visibility(bShowFusionSeal ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
			.IsEnabled(!Option.bTaken)
		]
	];
}

TSharedRef<SWidget> UUpgradeCardView::Build(const TArray<FUpgradeCardOption>& Options,
	const FString& Heading, const FString& Hint, const bool bChest, const FSimpleDelegate& FlipAction)
{
	PrepareButtonStyle();
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(8.f));
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		Grid->AddSlot(Index % 3, Index / 3)[MakeCard(Options[Index], bChest)];
	}

	return SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.015f, 0.035f, 0.06f, 0.92f))
			.Padding(0.f)
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(24.f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(960.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Heading))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
						.ColorAndOpacity(FLinearColor(0.93f, 0.98f, 1.f))
						.AutoWrapText(false).WrapTextAt(936.f).Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 7.f, 0.f, 10.f)
					[
						SNew(STextBlock).Text(FText::FromString(Hint))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
						.ColorAndOpacity(FLinearColor(0.56f, 0.71f, 0.79f))
						.AutoWrapText(false).WrapTextAt(936.f).Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot().AutoHeight()[Grid]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(480.f)
						.Visibility(FlipAction.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(SButton).ButtonStyle(&CardButtonStyles[1]).IsFocusable(false)
							.ContentPadding(FMargin(16.f, 10.f))
							.ToolTipText(FText::FromString(TEXT("将本次玩家奖励改为队友三选一；不占强化槽，不消耗排队奖励。")))
							.OnClicked_Lambda([FlipAction]() { FlipAction.ExecuteIfBound(); return FReply::Handled(); })
							[
								SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[ SNew(SBox).WidthOverride(32.f).HeightOverride(32.f)[SNew(SRewardPoolSwitchSymbol)] ]
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12.f, 0.f)
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("[F] Flip · 转交队友")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
									.ColorAndOpacity(FLinearColor(0.83f, 0.70f, 1.f)).AutoWrapText(false)
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("额外操作")))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
									.ColorAndOpacity(FLinearColor(0.62f, 0.63f, 0.75f))
								]
							]
						]
					]
				]
			]
		];
}
