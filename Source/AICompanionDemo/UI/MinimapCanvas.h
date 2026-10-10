/**
 * [INPUT]: HUD注入实体位置、导演64×64持久软边揭示场、Landscape只读高度采样；Slate自绘API
 * [OUTPUT]: SMinimapCanvas连续迷雾地图；真实高度明暗/等高线、边界、玩家/伙伴/两箱/钥匙及全图Boss/祭坛骷髅；不持玩法账本
 * [POS]: UI纯绘制叶控件；HUD负责世界查询与箱子探索门槛，导演负责探索持久化。150px常驻/400px Tab放大
 * [PROTOCOL]: 变更时更新此头部，然后检查UI/AGENTS.md；成员地图由主agent统一维护
 */
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/RenderingCommon.h"

class SMinimapCanvas final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMinimapCanvas) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	/** 仅复制展示快照；箱子数组已由HUD用导演同场IsExplored过滤。 */
	void SetWorldData(const FVector2D& InPlayerPos, const FVector2D& InCompanionPos,
		const TArray<FVector2D>& InEnemies, const TArray<FVector2D>& InKeys,
		const TArray<FVector2D>& InChests, const TArray<FVector2D>& InLockedChests,
		const TArray<FVector2D>& InBosses, const TArray<FVector2D>& InAltars,
		const TArray<float>* InReveal, int32 InRevealN, float InWorldHalfExtent, const FVector2D& InMoveDir);
	/** N×N包含场地两端；MAX_flt为未命中Landscape，不推测缺失高度。 */
	void SetTerrainData(const TArray<float>& InHeights, int32 InTerrainN);
	void SetZoomed(bool bZoomed);
	virtual FVector2D ComputeDesiredSize(float InScale) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	int32 GetBossMarkerCount() const { return BossPositions.Num(); }
	int32 GetAltarMarkerCount() const { return AltarPositions.Num(); }
	int32 GetRevealSampleCount() const { return RevealSamples.Num(); }
	int32 GetContourSegmentCount() const { return ContourSegments.Num() / 2; }
	int32 GetChestMarkerCount() const { return ChestPositions.Num() + LockedChestPositions.Num(); }

private:
	FVector2D WorldToLocal(const FVector2D& WorldPos, const FVector2D& Size) const;
	float SampleReveal(const FVector2D& WorldPos) const;
	float SampleHeight(const FVector2D& WorldPos) const;
	void BuildContours();
	float WorldHalfExtent = 25200.f;
	float CanvasSize = 150.f;
	int32 RevealN = 0;
	int32 TerrainN = 0;
	float TerrainMin = 0.f;
	float TerrainMax = 1.f;
	FVector2D PlayerPos = FVector2D::ZeroVector;
	FVector2D CompanionPos = FVector2D::ZeroVector;
	FVector2D MoveDir = FVector2D::ZeroVector;
	TArray<FVector2D> EnemyPositions, KeyPositions, ChestPositions, LockedChestPositions, BossPositions, AltarPositions;
	TArray<float> RevealSamples, TerrainHeights;
	TArray<FVector2D> ContourSegments;
	TArray<SlateIndex> RevealIndices;
	const FSlateBrush* WhiteBrush = nullptr;
};