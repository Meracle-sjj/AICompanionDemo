/**
 * [INPUT]: Slate的SLeafWidget/FSlateDrawElement、FSlateRoundedBoxBrush、宿主注入的共享图像brush与稀有度外圈/显隐数据
 * [OUTPUT]: EHudSkillGlyph与SHudSkillIcon：完整卡图与稀有度外圈、唯一遗物红内圈；召回/自动射击/钥匙原生折线；52/56逻辑像素、稳定brush共享所有权
 * [POS]: UI/纯Slate展示层；不加载资产、不查询世界、不持有玩法状态。纹理UObject的GC所有权由HUD的UPROPERTY缓存承担
 * [PROTOCOL]: 变更时更新此头部，然后检查UI/AGENTS.md；成员地图由主agent统一维护
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"

enum class EHudSkillGlyph : uint8
{
	Upgrade,
	Blink,
	Recall,
	AutoFire,
	Key
};

/** 自绘图标只消费展示数据；图像完整缩在圆圈内，不裁升级卡图，不持有世界对象。 */
class SHudSkillIcon final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHudSkillIcon)
		: _Glyph(EHudSkillGlyph::Upgrade), _Size(52.f), _RingColor(FLinearColor::White), _UniqueRelic(false) {}
		SLATE_ARGUMENT(TSharedPtr<FSlateBrush>, ArtBrush)
		SLATE_ARGUMENT(EHudSkillGlyph, Glyph)
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(FLinearColor, RingColor)
		SLATE_ARGUMENT(bool, UniqueRelic)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ArtBrush = InArgs._ArtBrush;
		Glyph = InArgs._Glyph;
		IconSize = InArgs._Size;
		RingColor = InArgs._RingColor;
		bUniqueRelic = InArgs._UniqueRelic;
		BackdropBrush = MakeUnique<FSlateRoundedBoxBrush>(
			FLinearColor(0.025f, 0.035f, 0.055f, 0.90f), IconSize * 0.5f);
		SetCanTick(false);
	}

	void SetAppearance(const FLinearColor& InRingColor, float InOpacity)
	{
		if (RingColor != InRingColor || IconOpacity != InOpacity)
		{
			RingColor = InRingColor;
			IconOpacity = InOpacity;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	const FLinearColor& GetRingColor() const { return RingColor; }
	float GetIconOpacity() const { return IconOpacity; }

	virtual FVector2D ComputeDesiredSize(float InScale) const override
	{
		return FVector2D(IconSize, IconSize);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FVector2f Center(static_cast<float>(Size.X * 0.5), static_cast<float>(Size.Y * 0.5));
		const float Diameter = static_cast<float>(FMath::Min(Size.X, Size.Y));
		const FLinearColor StyleTint = InWidgetStyle.GetColorAndOpacityTint();
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			BackdropBrush.Get(), ESlateDrawEffect::None, BackdropBrush->GetTint(InWidgetStyle) * StyleTint);
		auto Point = [&](float X, float Y) { return FVector2f(X * Diameter, Y * Diameter); };
		auto Lines = [&](TArray<FVector2f> Points, const FLinearColor& Color, float Thickness = 2.f)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2,
				AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None,
				Color * StyleTint, true, Thickness);
		};
		auto Circle = [&](const FVector2f& Origin, float Radius, const FLinearColor& Color, float Thickness)
		{
			TArray<FVector2f> Points;
			Points.Reserve(41);
			for (int32 I = 0; I <= 40; ++I)
			{
				const float Angle = 2.f * PI * static_cast<float>(I) / 40.f;
				Points.Add(Origin + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			Lines(MoveTemp(Points), Color, Thickness);
		};
		const FLinearColor GlyphColor(0.93f, 0.96f, 1.f, IconOpacity);
		if (ArtBrush.IsValid() && ArtBrush->GetResourceObject())
		{
			// 正方形对角线小于圈内径；完整卡图缩略显示，不靠圆角裁剪隐藏内容。
			const float ArtSize = Diameter * 0.64f;
			const FVector2D Offset((Size.X - ArtSize) * 0.5, (Size.Y - ArtSize) * 0.5);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(ArtSize, ArtSize), FSlateLayoutTransform(Offset)),
				ArtBrush.Get(), ESlateDrawEffect::None,
				FLinearColor(1.f, 1.f, 1.f, IconOpacity) * StyleTint);
		}
		else if (Glyph == EHudSkillGlyph::Recall)
		{
			Circle(Center, Diameter * 0.045f, GlyphColor, 2.f);
			for (const float Radius : { 0.17f, 0.28f })
			{
				for (int32 Side = 0; Side < 2; ++Side)
				{
					TArray<FVector2f> Arc;
					for (int32 I = 0; I <= 12; ++I)
					{
						const float Angle = FMath::DegreesToRadians(-55.f + I * (110.f / 12.f)) + Side * PI;
						Arc.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * (Diameter * Radius));
					}
					Lines(MoveTemp(Arc), GlyphColor);
				}
			}
		}
		else if (Glyph == EHudSkillGlyph::AutoFire)
		{
			Circle(Center, Diameter * 0.16f, GlyphColor, 2.f);
			Lines({ Point(0.5f, 0.22f), Point(0.5f, 0.38f) }, GlyphColor);
			Lines({ Point(0.5f, 0.62f), Point(0.5f, 0.78f) }, GlyphColor);
			Lines({ Point(0.22f, 0.5f), Point(0.38f, 0.5f) }, GlyphColor);
			Lines({ Point(0.62f, 0.5f), Point(0.78f, 0.5f) }, GlyphColor);
		}
		else if (Glyph == EHudSkillGlyph::Key)
		{
			Circle(Point(0.39f, 0.39f), Diameter * 0.115f, GlyphColor, 2.5f);
			Lines({ Point(0.47f, 0.47f), Point(0.70f, 0.70f), Point(0.76f, 0.64f) }, GlyphColor, 2.5f);
			Lines({ Point(0.61f, 0.61f), Point(0.67f, 0.55f) }, GlyphColor, 2.5f);
		}
		else
		{
			// 缺图的通用升级菱形；闪烁另加速度线，避免吞掉技能信息。
			Lines({ Point(0.5f, 0.25f), Point(0.75f, 0.5f), Point(0.5f, 0.75f),
				Point(0.25f, 0.5f), Point(0.5f, 0.25f) }, GlyphColor, 2.f);
			if (Glyph == EHudSkillGlyph::Blink)
			{
				Lines({ Point(0.25f, 0.36f), Point(0.43f, 0.36f) }, GlyphColor);
				Lines({ Point(0.18f, 0.5f), Point(0.39f, 0.5f) }, GlyphColor);
			}
		}
		Circle(Center, Diameter * 0.5f - 2.f, RingColor, 2.5f);
		if (bUniqueRelic) { Circle(Center, Diameter * 0.5f - 5.f, FLinearColor(0.88f, 0.12f, 0.09f), 1.5f); }
		return LayerId + 3;
	}

private:
	TSharedPtr<FSlateBrush> ArtBrush;
	TUniquePtr<FSlateRoundedBoxBrush> BackdropBrush;
	EHudSkillGlyph Glyph = EHudSkillGlyph::Upgrade;
	FLinearColor RingColor = FLinearColor::White;
	float IconSize = 52.f;
	float IconOpacity = 1.f;
	bool bUniqueRelic = false;
};
