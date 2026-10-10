/**
 * [INPUT]: HuntMarkVisual.h、UE5.6 WidgetComponent 世界平面/资源释放、SLeafWidget/MakeLines、PlayerController 相机视点
 * [OUTPUT]: 惰性创建稳定 Slate 猎印；1–2层金黄/≥3层红环与括角，中心七段数字显示真实层数，叠层重画、零层停 Tick
 * [POS]: Vfx/ 原创猎杀标记显示实现；透明断环/猎印/数字/层数点均程序化，无 Niagara、字体或项目资产依赖，释放时先拆引擎资源再释放共享树
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md；成员地图由主 agent 统一维护
 */

#include "HuntMarkVisual.h"

#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	constexpr float HuntMarkCanvasSize = 192.f;
	constexpr float HuntMarkWorldScale = 0.42f;
	constexpr int32 HuntMarkMaxIndividualDots = 24;
	constexpr int32 HuntMarkRedThreshold = 3;
	const FLinearColor HuntMarkGold(1.f, 0.73f, 0.12f, 1.f);
	const FLinearColor HuntMarkLightGold(1.f, 0.94f, 0.60f, 1.f);
	const FLinearColor HuntMarkRed(1.f, 0.075f, 0.035f, 1.f);
	const FLinearColor HuntMarkLightRed(1.f, 0.48f, 0.34f, 1.f);
}

/** 不引用 UObject 或外部 brush；旧渲染树在清理期间存活也不悬空。 */
class SHuntMarkGlyph final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHuntMarkGlyph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetCanTick(false);
		SetVisibility(EVisibility::HitTestInvisible);
	}

	void SetStacks(int32 InStacks)
	{
		if (Stacks != InStacks)
		{
			Stacks = InStacks;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	int32 GetStacks() const { return Stacks; }
	int32 GetIndividualDotCount() const { return FMath::Min(Stacks, HuntMarkMaxIndividualDots); }
	bool IsRedState() const { return Stacks >= HuntMarkRedThreshold; }
	FString GetNumberText() const { return Stacks > 0 ? FString::FromInt(Stacks) : FString(); }
	FLinearColor GetMarkColor() const { return IsRedState() ? HuntMarkRed : HuntMarkGold; }
	FLinearColor GetAccentColor() const { return IsRedState() ? HuntMarkLightRed : HuntMarkLightGold; }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		return FVector2D(HuntMarkCanvasSize, HuntMarkCanvasSize);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (Stacks <= 0)
		{
			return LayerId;
		}
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float Scale = static_cast<float>(FMath::Min(Size.X, Size.Y)) / HuntMarkCanvasSize;
		const FVector2f Offset(static_cast<float>((Size.X - HuntMarkCanvasSize * Scale) * 0.5),
			static_cast<float>((Size.Y - HuntMarkCanvasSize * Scale) * 0.5));
		const FLinearColor StyleTint = InWidgetStyle.GetColorAndOpacityTint();
		const FLinearColor MarkColor = GetMarkColor();
		const FLinearColor AccentColor = GetAccentColor();
		auto Point = [&](float X, float Y) { return Offset + FVector2f(X, Y) * Scale; };
		auto Stroke = [&](TArray<FVector2f> Points, const FLinearColor& Color, float Thickness)
		{
			// 两遍同路径：暗衬在沙地上保住轮廓，金/红线作为主体；不需要外部 brush。
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, FLinearColor(0.025f, 0.015f, 0.f, 0.80f) * StyleTint,
				true, (Thickness + 3.2f) * Scale);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Points), ESlateDrawEffect::None, Color * StyleTint, true, Thickness * Scale);
		};
		auto Arc = [&](float CenterX, float CenterY, float Radius, float StartAngle,
			float EndAngle, int32 Segments, const FLinearColor& Color, float Thickness)
		{
			TArray<FVector2f> Points;
			Points.Reserve(Segments + 1);
			for (int32 I = 0; I <= Segments; ++I)
			{
				const float Angle = FMath::Lerp(StartAngle, EndAngle,
					static_cast<float>(I) / static_cast<float>(Segments));
				Points.Add(Point(CenterX + FMath::Cos(Angle) * Radius,
					CenterY + FMath::Sin(Angle) * Radius));
			}
			Stroke(MoveTemp(Points), Color, Thickness);
		};

		// 1–2层金黄，≥3层红环与外侧括角；除颜色外仍有独立的形状变化。
		for (int32 Quarter = 0; Quarter < 4; ++Quarter)
		{
			const float Start = static_cast<float>(Quarter) * PI * 0.5f + 0.10f;
			Arc(96.f, 80.f, 59.f, Start, Start + PI * 0.5f - 0.20f, 18,
				MarkColor, IsRedState() ? 5.f : 3.6f);
		}
		Arc(96.f, 80.f, 49.f, 0.f, 2.f * PI, 60,
			FLinearColor(MarkColor.R, MarkColor.G, MarkColor.B, 0.80f), 1.3f);
		// 顶端小菱与向心双叉保留猎印轮廓，中心留给大号数字。
		Stroke({Point(96.f, 31.f), Point(102.f, 39.f), Point(96.f, 47.f),
			Point(90.f, 39.f), Point(96.f, 31.f)}, AccentColor, 2.5f);
		Stroke({Point(59.f, 60.f), Point(72.f, 73.f), Point(63.f, 91.f)}, MarkColor, 3.f);
		Stroke({Point(133.f, 60.f), Point(120.f, 73.f), Point(129.f, 91.f)}, MarkColor, 3.f);
		if (IsRedState())
		{
			Stroke({Point(38.f, 49.f), Point(28.f, 49.f), Point(28.f, 65.f)}, MarkColor, 4.f);
			Stroke({Point(154.f, 49.f), Point(164.f, 49.f), Point(164.f, 65.f)}, MarkColor, 4.f);
			Stroke({Point(38.f, 111.f), Point(28.f, 111.f), Point(28.f, 95.f)}, MarkColor, 4.f);
			Stroke({Point(154.f, 111.f), Point(164.f, 111.f), Point(164.f, 95.f)}, MarkColor, 4.f);
		}

		// 七段数字是当前真实层数，不依赖字体或固定三档图；两位及以上居中缩放。
		// 段顺序：顶/右上/右下/底/左下/左上/中。
		static constexpr uint8 DigitSegments[10] = {
			0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
		};
		static constexpr float SegmentPoints[7][4] = {
			{3.f, 0.f, 25.f, 0.f}, {28.f, 3.f, 28.f, 21.f},
			{28.f, 27.f, 28.f, 45.f}, {3.f, 48.f, 25.f, 48.f},
			{0.f, 27.f, 0.f, 45.f}, {0.f, 3.f, 0.f, 21.f},
			{3.f, 24.f, 25.f, 24.f}
		};
		const FString NumberText = GetNumberText();
		const float NumberWidth = NumberText.Len() * 28.f + (NumberText.Len() - 1) * 8.f;
		const float NumberScale = FMath::Min(1.f, 92.f / NumberWidth);
		const float NumberLeft = 96.f - NumberWidth * NumberScale * 0.5f;
		const float NumberTop = 84.f - 24.f * NumberScale;
		for (int32 Digit = 0; Digit < NumberText.Len(); ++Digit)
		{
			const uint8 Mask = DigitSegments[NumberText[Digit] - TEXT('0')];
			const float DigitLeft = NumberLeft + Digit * 36.f * NumberScale;
			for (int32 Segment = 0; Segment < 7; ++Segment)
			{
				if ((Mask & (1 << Segment)) != 0)
				{
					const float* Ends = SegmentPoints[Segment];
					Stroke({Point(DigitLeft + Ends[0] * NumberScale, NumberTop + Ends[1] * NumberScale),
						Point(DigitLeft + Ends[2] * NumberScale, NumberTop + Ends[3] * NumberScale)},
						FLinearColor(1.f, 0.97f, 0.88f, 1.f), 5.2f * NumberScale);
				}
			}
		}

		const int32 DotCount = GetIndividualDotCount();
		for (int32 Dot = 0; Dot < DotCount; ++Dot)
		{
			const int32 Row = Dot / 8;
			const int32 InRow = FMath::Min(8, DotCount - Row * 8);
			const float X = 96.f + (static_cast<float>(Dot % 8) - (InRow - 1) * 0.5f) * 12.f;
			const float Y = 151.f + static_cast<float>(Row) * 12.f;
			Arc(X, Y, 2.2f, 0.f, 2.f * PI, 12, AccentColor, 3.2f);
		}
		// 重复强化上限无界；极高层数保留真值，24 点后显示原创“+”而非静默截断含义。
		if (Stacks > HuntMarkMaxIndividualDots)
		{
			Stroke({Point(151.f, 173.f), Point(165.f, 173.f)}, AccentColor, 2.5f);
			Stroke({Point(158.f, 166.f), Point(158.f, 180.f)}, AccentColor, 2.5f);
		}
		return LayerId + 1;
	}

private:
	int32 Stacks = 0;
};

UHuntMarkVisual::UHuntMarkVisual(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetWidgetSpace(EWidgetSpace::World);
	SetGeometryMode(EWidgetGeometryMode::Plane);
	SetDrawSize(FVector2D(HuntMarkCanvasSize, HuntMarkCanvasSize));
	SetPivot(FVector2D(0.5f, 0.5f));
	SetDrawAtDesiredSize(false);
	SetBlendMode(EWidgetBlendMode::Transparent);
	SetBackgroundColor(FLinearColor::Transparent);
	SetTwoSided(true);
	SetWindowFocusable(false);
	SetManuallyRedraw(true);
	SetTickMode(ETickMode::Enabled);
	SetTickWhenOffscreen(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	SetReceivesDecals(false);
	// 位置随敌人胶囊、世界旋转/尺寸独立：不会继承 0.9/1.25/1.6 倍变体体型。
	SetAbsolute(false, true, true);
	SetRelativeScale3D(FVector(HuntMarkWorldScale));
	SetVisibility(false);
	SetHiddenInGame(true);
	// LevelTick.cpp 先 UpdateCameraManager 再 PostUpdateWork，使用本帧最新视点。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bTickInEditor = false;
}

void UHuntMarkVisual::BeginPlay()
{
	Super::BeginPlay();
	SetStacks(DisplayedStacks);
}

void UHuntMarkVisual::EnsureGlyphWidget()
{
	if (!GlyphWidget.IsValid())
	{
		GlyphWidget = SNew(SHuntMarkGlyph);
	}
	if (GetSlateWidget() != GlyphWidget)
	{
		SetSlateWidget(GlyphWidget);
	}
}

void UHuntMarkVisual::SetStacks(int32 InStacks)
{
	const int32 NewStacks = FMath::Max(0, InStacks);
	const bool bStacksChanged = DisplayedStacks != NewStacks;
	DisplayedStacks = NewStacks;
	if (DisplayedStacks > 0)
	{
		const bool bNeedsWidget = !IsHuntMarkWidgetReady();
		const bool bWasHidden = !IsVisible() || bHiddenInGame;
		EnsureGlyphWidget();
		GlyphWidget->SetStacks(DisplayedStacks);
		SetVisibility(true);
		SetHiddenInGame(false);
		SetComponentTickEnabled(true);
		if (bStacksChanged || bNeedsWidget || bWasHidden)
		{
			FaceLocalCamera();
		}
		if (bStacksChanged || bNeedsWidget)
		{
			RequestRedraw();
		}
	}
	else
	{
		if (GlyphWidget.IsValid())
		{
			GlyphWidget->SetStacks(0);
		}
		SetVisibility(false);
		SetHiddenInGame(true);
		SetComponentTickEnabled(false);
	}
}

void UHuntMarkVisual::FaceLocalCamera()
{
	if (const APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		Player->GetPlayerViewPoint(CameraLocation, CameraRotation);
		const FVector TowardCamera = CameraLocation - GetComponentLocation();
		if (!TowardCamera.IsNearlyZero())
		{
			// WidgetComponent.cpp 平面顶点位于 Y/Z，法线 +X；像素向右为 -Y、向上为 +Z。
			SetWorldRotation(FRotationMatrix::MakeFromXZ(TowardCamera,
				CameraRotation.RotateVector(FVector::UpVector)).Rotator());
		}
	}
}

void UHuntMarkVisual::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	if (DisplayedStacks > 0)
	{
		FaceLocalCamera();
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool UHuntMarkVisual::IsHuntMarkWidgetReady() const
{
	return GlyphWidget.IsValid() && GetSlateWidget().IsValid() && GetSlateWidget() == GlyphWidget;
}

void UHuntMarkVisual::ReleaseResources()
{
	// 引擎先释放 SlateWidget/虚拟窗口/renderer；leaf 不持有 UObject 或跨帧外部 brush。
	Super::ReleaseResources();
	GlyphWidget.Reset();
}

FString UHuntMarkVisual::DebugVisualState() const
{
	const FVector Location = GetComponentLocation();
	const FVector Scale = GetComponentScale();
	const FVector2D Draw = GetDrawSize();
	const FRotator Rotation = GetComponentRotation();
	const bool bReady = IsHuntMarkWidgetReady();
	const TCHAR* ReadyText = bReady ? TEXT("true") : TEXT("false");
	const bool bRedState = GlyphWidget.IsValid() && GlyphWidget->IsRedState();
	const FString NumberText = GlyphWidget.IsValid() ? GlyphWidget->GetNumberText() : FString();
	const FLinearColor MarkColor = GlyphWidget.IsValid() ? GlyphWidget->GetMarkColor() : FLinearColor::Transparent;
	bool bHasCamera = false;
	float CameraAlignment = 0.f;
	if (const APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		Player->GetPlayerViewPoint(CameraLocation, CameraRotation);
		const FVector TowardCamera = CameraLocation - Location;
		bHasCamera = !TowardCamera.IsNearlyZero();
		CameraAlignment = bHasCamera
			? static_cast<float>(FVector::DotProduct(GetForwardVector(), TowardCamera.GetSafeNormal())) : 0.f;
	}
	return FString::Printf(
		TEXT("{\"displayed_stacks\":%d,\"glyph_stacks\":%d,\"individual_dots\":%d,\"overflow_plus\":%s,")
		TEXT("\"number_text\":\"%s\",\"red_threshold\":%d,\"red_state\":%s,\"warning_brackets\":%s,\"mark_color\":[%.4f,%.4f,%.4f,%.4f],")
		TEXT("\"slate_valid\":%s,\"widget_ready\":%s,\"visible\":%s,\"hidden_in_game\":%s,")
		TEXT("\"registered\":%s,\"tick_enabled\":%s,\"world_space\":%s,\"plane_geometry\":%s,")
		TEXT("\"collision_enabled\":%d,\"affects_navigation\":%s,\"two_sided\":%s,")
		TEXT("\"render_target_ready\":%s,\"slate_window_valid\":%s,\"has_camera\":%s,\"camera_alignment\":%.6f,")
		TEXT("\"draw_size\":[%.0f,%.0f],\"location\":[%.3f,%.3f,%.3f],")
		TEXT("\"world_scale\":[%.3f,%.3f,%.3f],\"rotation\":[%.3f,%.3f,%.3f]}"),
		DisplayedStacks, GlyphWidget.IsValid() ? GlyphWidget->GetStacks() : 0,
		GlyphWidget.IsValid() ? GlyphWidget->GetIndividualDotCount() : 0,
		DisplayedStacks > HuntMarkMaxIndividualDots ? TEXT("true") : TEXT("false"),
		*NumberText, HuntMarkRedThreshold, bRedState ? TEXT("true") : TEXT("false"),
		bRedState ? TEXT("true") : TEXT("false"), MarkColor.R, MarkColor.G, MarkColor.B, MarkColor.A,
		GetSlateWidget().IsValid() ? TEXT("true") : TEXT("false"), ReadyText,
		IsVisible() ? TEXT("true") : TEXT("false"), bHiddenInGame ? TEXT("true") : TEXT("false"),
		IsRegistered() ? TEXT("true") : TEXT("false"), IsComponentTickEnabled() ? TEXT("true") : TEXT("false"),
		GetWidgetSpace() == EWidgetSpace::World ? TEXT("true") : TEXT("false"),
		GetGeometryMode() == EWidgetGeometryMode::Plane ? TEXT("true") : TEXT("false"),
		static_cast<int32>(GetCollisionEnabled()), CanEverAffectNavigation() ? TEXT("true") : TEXT("false"),
		GetTwoSided() ? TEXT("true") : TEXT("false"), GetRenderTarget() ? TEXT("true") : TEXT("false"),
		GetSlateWindow().IsValid() ? TEXT("true") : TEXT("false"), bHasCamera ? TEXT("true") : TEXT("false"),
		CameraAlignment, Draw.X, Draw.Y, Location.X, Location.Y, Location.Z,
		Scale.X, Scale.Y, Scale.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
}
