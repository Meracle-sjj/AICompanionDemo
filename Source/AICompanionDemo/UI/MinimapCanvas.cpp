/**
 * [INPUT]: MinimapCanvas快照；Slate MakeCustomVerts/MakeLines；导演软揭示场及实测Landscape高度
 * [OUTPUT]: 连续三角插值迷雾、真实粗采样地形阴影与等高线；全图红骷髅区分活Boss/待激活祭坛，其他实体形状独立
 * [POS]: UI纯绘制实现；不查询世界、不推断未采样高度、不改变探索或导航。等高线只表达33×33高度采样
 * [PROTOCOL]: 变更时更新此头部，然后检查UI/AGENTS.md；成员地图由主agent统一维护
 */
#include "MinimapCanvas.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	float SampleGrid(const TArray<float>& Values, int32 N, const FVector2D& P, float Extent, float Fallback)
	{
		if (N < 2 || Values.Num() != N * N) { return Fallback; }
		const double X = FMath::Clamp((P.X + Extent) / (2.0 * Extent) * (N - 1), 0.0, double(N - 1));
		const double Y = FMath::Clamp((P.Y + Extent) / (2.0 * Extent) * (N - 1), 0.0, double(N - 1));
		const int32 Ix = FMath::Min(FMath::FloorToInt(X), N - 2), Iy = FMath::Min(FMath::FloorToInt(Y), N - 2);
		const float A = Values[Iy * N + Ix], B = Values[Iy * N + Ix + 1];
		const float C = Values[(Iy + 1) * N + Ix], D = Values[(Iy + 1) * N + Ix + 1];
		if (A == MAX_flt || B == MAX_flt || C == MAX_flt || D == MAX_flt) { return Fallback; }
		return FMath::Lerp(FMath::Lerp(A, B, float(X - Ix)), FMath::Lerp(C, D, float(X - Ix)), float(Y - Iy));
	}
}

void SMinimapCanvas::Construct(const FArguments& InArgs)
{
	WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	SetCanTick(false);
	SetClipping(EWidgetClipping::ClipToBounds);
}

void SMinimapCanvas::SetWorldData(const FVector2D& InPlayerPos, const FVector2D& InCompanionPos,
	const TArray<FVector2D>& InEnemies, const TArray<FVector2D>& InKeys,
	const TArray<FVector2D>& InChests, const TArray<FVector2D>& InLockedChests,
	const TArray<FVector2D>& InBosses, const TArray<FVector2D>& InAltars,
	const TArray<float>* InReveal, int32 InRevealN, float InWorldHalfExtent, const FVector2D& InMoveDir)
{
	PlayerPos = InPlayerPos; CompanionPos = InCompanionPos; MoveDir = InMoveDir;
	EnemyPositions = InEnemies; KeyPositions = InKeys;
	ChestPositions = InChests; LockedChestPositions = InLockedChests;
	BossPositions = InBosses; AltarPositions = InAltars;
	WorldHalfExtent = FMath::Max(InWorldHalfExtent, 1.f);
	RevealSamples = InReveal ? *InReveal : TArray<float>();
	if (RevealN != InRevealN)
	{
		RevealN = FMath::Clamp(InRevealN, 2, 128);
		RevealIndices.Reset((RevealN - 1) * (RevealN - 1) * 6);
		for (int32 Y = 0; Y < RevealN - 1; ++Y)
		{
			for (int32 X = 0; X < RevealN - 1; ++X)
			{
				const int32 A = Y * RevealN + X, B = A + 1, C = A + RevealN, D = C + 1;
				RevealIndices.Append({ SlateIndex(A), SlateIndex(B), SlateIndex(C), SlateIndex(B), SlateIndex(D), SlateIndex(C) });
			}
		}
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

float SMinimapCanvas::SampleReveal(const FVector2D& P) const
{
	if (RevealN < 2 || RevealSamples.Num() != RevealN * RevealN) { return 0.f; }
	const double X = FMath::Clamp((P.X + WorldHalfExtent) / (2.0 * WorldHalfExtent) * (RevealN - 1), 0.0, double(RevealN - 1));
	const double Y = FMath::Clamp((P.Y + WorldHalfExtent) / (2.0 * WorldHalfExtent) * (RevealN - 1), 0.0, double(RevealN - 1));
	const int32 Ix = FMath::Min(FMath::FloorToInt(X), RevealN - 2), Iy = FMath::Min(FMath::FloorToInt(Y), RevealN - 2);
	const float Tx = float(X - Ix), Ty = float(Y - Iy);
	const float A = RevealSamples[Iy * RevealN + Ix], B = RevealSamples[Iy * RevealN + Ix + 1];
	const float C = RevealSamples[(Iy + 1) * RevealN + Ix], D = RevealSamples[(Iy + 1) * RevealN + Ix + 1];
	// 与MakeCustomVerts的B-C对角、导演IsExplored一致，避免查询亮而实际色场黑的鞍点。
	const float R = Tx + Ty <= 1.f ? A * (1.f - Tx - Ty) + B * Tx + C * Ty
		: D * (Tx + Ty - 1.f) + B * (1.f - Ty) + C * (1.f - Tx);
	return FMath::Clamp(R, 0.f, 1.f);
}

float SMinimapCanvas::SampleHeight(const FVector2D& P) const
{
	return SampleGrid(TerrainHeights, TerrainN, P, WorldHalfExtent, MAX_flt);
}

void SMinimapCanvas::SetTerrainData(const TArray<float>& InHeights, int32 InTerrainN)
{
	TerrainHeights = InHeights; TerrainN = InTerrainN;
	TerrainMin = MAX_flt; TerrainMax = -MAX_flt;
	for (const float H : TerrainHeights)
	{
		if (H != MAX_flt) { TerrainMin = FMath::Min(TerrainMin, H); TerrainMax = FMath::Max(TerrainMax, H); }
	}
	if (TerrainMin == MAX_flt) { TerrainMin = 0.f; TerrainMax = 1.f; }
	BuildContours();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SMinimapCanvas::BuildContours()
{
	ContourSegments.Reset();
	if (TerrainN < 2 || TerrainHeights.Num() != TerrainN * TerrainN) { return; }
	const float Step = 2.f * WorldHalfExtent / (TerrainN - 1);
	constexpr float ContourStep = 800.f;
	for (int32 Y = 0; Y < TerrainN - 1; ++Y)
	{
		for (int32 X = 0; X < TerrainN - 1; ++X)
		{
			const float H[4] = { TerrainHeights[Y * TerrainN + X], TerrainHeights[Y * TerrainN + X + 1],
				TerrainHeights[(Y + 1) * TerrainN + X + 1], TerrainHeights[(Y + 1) * TerrainN + X] };
			if (H[0] == MAX_flt || H[1] == MAX_flt || H[2] == MAX_flt || H[3] == MAX_flt) { continue; }
			const FVector2D P[4] = { FVector2D(-WorldHalfExtent + X * Step, -WorldHalfExtent + Y * Step),
				FVector2D(-WorldHalfExtent + (X + 1) * Step, -WorldHalfExtent + Y * Step),
				FVector2D(-WorldHalfExtent + (X + 1) * Step, -WorldHalfExtent + (Y + 1) * Step),
				FVector2D(-WorldHalfExtent + X * Step, -WorldHalfExtent + (Y + 1) * Step) };
			const float Lo = FMath::Min(FMath::Min(H[0], H[1]), FMath::Min(H[2], H[3]));
			const float Hi = FMath::Max(FMath::Max(H[0], H[1]), FMath::Max(H[2], H[3]));
			for (float Level = FMath::CeilToFloat(Lo / ContourStep) * ContourStep; Level < Hi; Level += ContourStep)
			{
				TArray<FVector2D, TInlineAllocator<4>> Crossings;
				for (int32 Edge = 0; Edge < 4; ++Edge)
				{
					const int32 Next = (Edge + 1) % 4;
					if ((H[Edge] < Level) != (H[Next] < Level))
					{
						Crossings.Add(FMath::Lerp(P[Edge], P[Next], (Level - H[Edge]) / (H[Next] - H[Edge])));
					}
				}
				for (int32 I = 0; I + 1 < Crossings.Num(); I += 2)
				{
					ContourSegments.Add(Crossings[I]); ContourSegments.Add(Crossings[I + 1]);
				}
			}
		}
	}
}

void SMinimapCanvas::SetZoomed(bool bZoomed)
{
	const float NewSize = bZoomed ? 400.f : 150.f;
	if (CanvasSize != NewSize) { CanvasSize = NewSize; Invalidate(EInvalidateWidgetReason::Layout); }
}

FVector2D SMinimapCanvas::ComputeDesiredSize(float InScale) const { return FVector2D(CanvasSize); }

FVector2D SMinimapCanvas::WorldToLocal(const FVector2D& P, const FVector2D& Size) const
{
	return FVector2D((P.X + WorldHalfExtent) / (2.f * WorldHalfExtent) * Size.X,
		(P.Y + WorldHalfExtent) / (2.f * WorldHalfExtent) * Size.Y);
}

int32 SMinimapCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Size = G.GetLocalSize();
	const FLinearColor Tint = Style.GetColorAndOpacityTint();
	const FSlateResourceHandle Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	auto Box = [&](FVector2D P, FVector2D S, FLinearColor C, int32 Z)
	{
		FSlateDrawElement::MakeBox(Out, Z, G.ToPaintGeometry(S, FSlateLayoutTransform(P)),
			WhiteBrush, ESlateDrawEffect::None, C * Tint);
	};
	auto Lines = [&](TArray<FVector2f> P, FLinearColor C, float Width, int32 Z)
	{
		FSlateDrawElement::MakeLines(Out, Z, G.ToPaintGeometry(), MoveTemp(P), ESlateDrawEffect::None, C * Tint, true, Width);
	};
	auto Polygon = [&](const TArray<FVector2f>& P, FLinearColor C, int32 Z)
	{
		if (P.Num() < 3) { return; }
		TArray<FSlateVertex> V; TArray<SlateIndex> I;
		for (const FVector2f& Point : P)
		{
			V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(), Point,
				FVector2f(0.5f), (C * Tint).ToFColor(true)));
		}
		for (int32 N = 1; N < P.Num() - 1; ++N) { I.Append({ SlateIndex(0), SlateIndex(N), SlateIndex(N + 1) }); }
		FSlateDrawElement::MakeCustomVerts(Out, Z, Resource, V, I, nullptr, 0, 0);
	};
	auto Circle = [&](FVector2f P, float R, FLinearColor C, float Width, int32 Z, bool Filled = false)
	{
		TArray<FVector2f> Points;
		for (int32 I = 0; I <= 24; ++I)
		{
			const float A = I * (2.f * PI / 24.f);
			Points.Add(P + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		if (Filled) { Polygon(Points, C, Z); } else { Lines(MoveTemp(Points), C, Width, Z); }
	};
	Box(FVector2D::ZeroVector, Size, FLinearColor(0.025f, 0.027f, 0.032f, 0.95f), L);
	if (RevealN >= 2)
	{
		TArray<FSlateVertex> V;
		V.Reserve(RevealN * RevealN);
		const float HeightRange = FMath::Max(TerrainMax - TerrainMin, 1.f);
		for (int32 Y = 0; Y < RevealN; ++Y)
		{
			for (int32 X = 0; X < RevealN; ++X)
			{
				const FVector2D World(-WorldHalfExtent + X * (2.f * WorldHalfExtent / (RevealN - 1)),
					-WorldHalfExtent + Y * (2.f * WorldHalfExtent / (RevealN - 1)));
				const float H = SampleHeight(World), Reveal = SampleReveal(World);
				FLinearColor Sand(0.50f, 0.36f, 0.18f);
				if (H != MAX_flt)
				{
					const float Altitude = FMath::Clamp((H - TerrainMin) / HeightRange, 0.f, 1.f);
					const float Neighbour = SampleHeight(World + FVector2D(400.f, 400.f));
					const float SlopeShade = Neighbour == MAX_flt ? 1.f : FMath::Clamp(1.f + (H - Neighbour) / 2600.f, 0.7f, 1.3f);
					Sand = FMath::Lerp(FLinearColor(0.38f, 0.27f, 0.13f), FLinearColor(0.80f, 0.64f, 0.37f), Altitude) * SlopeShade;
				}
				FLinearColor C = FMath::Lerp(FLinearColor(0.025f, 0.027f, 0.032f), Sand, Reveal * 0.92f);
				C.A = 0.96f;
				V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),
					FVector2f(WorldToLocal(World, Size)), FVector2f(0.5f), (C * Tint).ToFColor(true)));
			}
		}
		FSlateDrawElement::MakeCustomVerts(Out, L + 1, Resource, V, RevealIndices, nullptr, 0, 0);
	}
	// 全部等高段合为一批细四边形，防大量Slate独立绘制元素。未探索区域不泄漏高度信息。
	TArray<FSlateVertex> ContourV; TArray<SlateIndex> ContourI;
	for (int32 I = 0; I + 1 < ContourSegments.Num(); I += 2)
	{
		const float Alpha = FMath::Min(SampleReveal(ContourSegments[I]), SampleReveal(ContourSegments[I + 1])) * 0.36f;
		if (Alpha < 0.02f || ContourV.Num() > 60000) { continue; }
		const FVector2f A(WorldToLocal(ContourSegments[I], Size)), B(WorldToLocal(ContourSegments[I + 1], Size));
		const FVector2f D = B - A;
		const float Length = D.Size();
		if (Length < 0.01f) { continue; }
		const FVector2f N(-D.Y / Length * 0.38f, D.X / Length * 0.38f);
		const SlateIndex Base = SlateIndex(ContourV.Num());
		const FColor C = (FLinearColor(0.96f, 0.78f, 0.46f, Alpha) * Tint).ToFColor(true);
		for (const FVector2f P : { A + N, B + N, B - N, A - N })
		{
			ContourV.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(), P, FVector2f(0.5f), C));
		}
		ContourI.Append({ Base, SlateIndex(Base + 1), SlateIndex(Base + 2), Base, SlateIndex(Base + 2), SlateIndex(Base + 3) });
	}
	if (!ContourV.IsEmpty()) { FSlateDrawElement::MakeCustomVerts(Out, L + 2, Resource, ContourV, ContourI, nullptr, 0, 0); }
	// v15高度源山环从82%半宽起坡；细虚线是该源边界指引，不冒充逐点导航可达边界。
	const float MarginX = float(Size.X) * 0.09f, MarginY = float(Size.Y) * 0.09f;
	for (int32 I = 0; I < 12; ++I)
	{
		const float T0 = I / 12.f, T1 = (I + 0.52f) / 12.f;
		const float X0 = FMath::Lerp(MarginX, float(Size.X) - MarginX, T0), X1 = FMath::Lerp(MarginX, float(Size.X) - MarginX, T1);
		const float Y0 = FMath::Lerp(MarginY, float(Size.Y) - MarginY, T0), Y1 = FMath::Lerp(MarginY, float(Size.Y) - MarginY, T1);
		const FLinearColor Boundary(0.70f, 0.51f, 0.27f, 0.34f);
		Lines({ FVector2f(X0, MarginY), FVector2f(X1, MarginY) }, Boundary, 0.8f, L + 3);
		Lines({ FVector2f(X0, float(Size.Y) - MarginY), FVector2f(X1, float(Size.Y) - MarginY) }, Boundary, 0.8f, L + 3);
		Lines({ FVector2f(MarginX, Y0), FVector2f(MarginX, Y1) }, Boundary, 0.8f, L + 3);
		Lines({ FVector2f(float(Size.X) - MarginX, Y0), FVector2f(float(Size.X) - MarginX, Y1) }, Boundary, 0.8f, L + 3);
	}
	const float IconScale = CanvasSize > 300.f ? 1.35f : 1.f;
	auto Position = [&](const FVector2D& W) -> FVector2f
	{
		const FVector2D P = WorldToLocal(W, Size);
		return FVector2f(float(FMath::Clamp(P.X, 10.0, Size.X - 10.0)), float(FMath::Clamp(P.Y, 10.0, Size.Y - 10.0)));
	};
	const FLinearColor Gold(1.f, 0.80f, 0.27f), Purple(0.80f, 0.48f, 1.f), Ink(0.02f, 0.025f, 0.035f);
	for (const FVector2D& W : EnemyPositions)
	{
		Circle(Position(W), 2.2f * IconScale, FLinearColor(1.f, 0.18f, 0.49f), 1.f, L + 4, true);
	}
	auto Chest = [&](const FVector2D& W, FLinearColor C, bool Locked)
	{
		const FVector2f P = Position(W);
		const float R = 3.5f * IconScale;
		Box(FVector2D(P - FVector2f(R)), FVector2D(R * 2.f), Ink, L + 5);
		Lines({ P + FVector2f(-R, -R), P + FVector2f(R, -R), P + FVector2f(R, R),
			P + FVector2f(-R, R), P + FVector2f(-R, -R) }, C, 1.3f, L + 6);
		Lines({ P + FVector2f(-R, -R * 0.25f), P + FVector2f(R, -R * 0.25f) }, C, 1.f, L + 6);
		if (Locked) { Circle(P + FVector2f(0.f, -R), R * 0.55f, C, 1.1f, L + 6); }
	};
	for (const FVector2D& W : ChestPositions) { Chest(W, Gold, false); }
	for (const FVector2D& W : LockedChestPositions) { Chest(W, Purple, true); }
	for (const FVector2D& W : KeyPositions)
	{
		const FVector2f P = Position(W);
		Circle(P + FVector2f(-2.f, -2.f) * IconScale, 2.8f * IconScale, Gold, 1.5f, L + 7);
		Lines({ P, P + FVector2f(5.f, 5.f) * IconScale, P + FVector2f(7.f, 3.f) * IconScale }, Gold, 1.6f, L + 7);
	}
	auto Skull = [&](const FVector2D& W, bool Active)
	{
		const FVector2f Actual = Position(W), PlayerAt = Position(PlayerPos);
		FVector2f P = Actual;
		const float Clearance = 16.f * IconScale;
		if ((P - PlayerAt).Size() < Clearance)
		{
			const FVector2f Delta = P - PlayerAt;
			const FVector2f Direction = Delta.Size() > 0.1f ? Delta / Delta.Size() : FVector2f(0.7071f, -0.7071f);
			auto ClampGlyph = [&](FVector2f Candidate)
			{
				const float Margin = 10.f * IconScale;
				return FVector2f(FMath::Clamp(Candidate.X, Margin, float(Size.X) - Margin),
					FMath::Clamp(Candidate.Y, Margin, float(Size.Y) - Margin));
			};
			P = ClampGlyph(PlayerAt + Direction * (Clearance + 2.f));
			// 边界处向外移会被裁回玩家：选仍在画布内的空侧，以连线保留真实位置含义。
			if ((P - PlayerAt).Size() < Clearance)
			{
				for (const FVector2f Side : { FVector2f(1.f, 0.f), FVector2f(-1.f, 0.f), FVector2f(0.f, 1.f), FVector2f(0.f, -1.f) })
				{
					const FVector2f Candidate = ClampGlyph(PlayerAt + Side * (Clearance + 2.f));
					if ((Candidate - PlayerAt).Size() > (P - PlayerAt).Size()) { P = Candidate; }
				}
			}
			Lines({ Actual, P }, FLinearColor(0.94f, 0.18f, 0.13f, 0.7f), 1.f, L + 8);
		}
		const float R = 5.3f * IconScale;
		const FLinearColor Red = Active ? FLinearColor(1.f, 0.12f, 0.10f) : FLinearColor(0.72f, 0.15f, 0.12f);
		Circle(P, R + 2.f, Ink, 1.f, L + 8, true);
		Circle(P + FVector2f(0.f, -1.f), R, Red, 1.7f, L + 9, Active);
		if (Active) { Box(FVector2D(P + FVector2f(-R * 0.60f, R * 0.45f)), FVector2D(R * 1.2f, R * 0.75f), Red, L + 9); }
		else { Lines({ P + FVector2f(-R * 0.6f, R * 0.45f), P + FVector2f(-R * 0.6f, R * 1.2f),
			P + FVector2f(R * 0.6f, R * 1.2f), P + FVector2f(R * 0.6f, R * 0.45f) }, Red, 1.5f, L + 9); }
		for (float Side : { -1.f, 1.f })
		{
			const FVector2D Eye = FVector2D(P + FVector2f(Side * R * 0.42f - R * 0.16f, -R * 0.35f));
			Box(Eye, FVector2D(R * 0.33f), Active ? Ink : Red, L + 10);
		}
		for (int32 Tooth = -1; Tooth <= 1; ++Tooth)
		{
			Lines({ P + FVector2f(Tooth * R * 0.35f, R * 0.72f), P + FVector2f(Tooth * R * 0.35f, R * 1.1f) },
				Active ? Ink : Red, 0.9f, L + 10);
		}
	};
	for (const FVector2D& W : AltarPositions) { Skull(W, false); }
	for (const FVector2D& W : BossPositions) { Skull(W, true); }
	const FVector2f Buddy = Position(CompanionPos);
	const float BuddyR = 4.f * IconScale;
	Polygon({ Buddy + FVector2f(0.f, -BuddyR), Buddy + FVector2f(BuddyR, 0.f), Buddy + FVector2f(0.f, BuddyR),
		Buddy + FVector2f(-BuddyR, 0.f) }, FLinearColor(0.34f, 0.9f, 1.f), L + 11);
	const FVector2f Player = Position(PlayerPos);
	const FVector2f Direction = MoveDir.IsNearlyZero() ? FVector2f(0.f, -1.f) : FVector2f(MoveDir.GetSafeNormal());
	const FVector2f Right(-Direction.Y, Direction.X);
	const float PlayerR = 5.5f * IconScale;
	Circle(Player, PlayerR + 2.f, Ink, 1.f, L + 11, true);
	Polygon({ Player + Direction * PlayerR * 1.4f, Player - Direction * PlayerR * 0.7f + Right * PlayerR * 0.8f,
		Player - Direction * PlayerR * 0.3f, Player - Direction * PlayerR * 0.7f - Right * PlayerR * 0.8f },
		FLinearColor::White, L + 12);
	Lines({ FVector2f(1.f, 1.f), FVector2f(float(Size.X) - 1.f, 1.f), FVector2f(float(Size.X) - 1.f, float(Size.Y) - 1.f),
		FVector2f(1.f, float(Size.Y) - 1.f), FVector2f(1.f, 1.f) }, FLinearColor(0.85f, 0.74f, 0.53f, 0.8f), 1.2f, L + 13);
	return L + 14;
}
