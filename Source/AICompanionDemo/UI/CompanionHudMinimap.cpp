/**
 * [INPUT]: CompanionHud私有小图状态、WaveDirector同一64²揭示场/IsExplored、Boss/祭坛生命周期、Landscape高度碰撞
 * [OUTPUT]: 0.15s小图喂数；活Boss和存在的祭坛全图显示，箱子使用导演同场探索门；33²真实高度分帧只读缓存及DebugMinimapState
 * [POS]: UI的HUD小图数据适配层；不修改地形/导航/迷雾账本、不在Slate绘制阶段发射线。与CompanionHud.cpp共享同一类
 * [PROTOCOL]: 变更时更新此头部，然后检查UI/AGENTS.md；成员地图由主agent统一维护
 */
#include "CompanionHud.h"
#include "MinimapCanvas.h"
#include "AI/WaveDirector.h"
#include "AI/EnemyGrunt.h"
#include "AI/BossBase.h"
#include "AI/BossAltar.h"
#include "AI/CompanionCharacter.h"
#include "AI/KeyPickup.h"
#include "AI/TreasureChest.h"
#include "AI/LockedChest.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SBox.h"

void UCompanionHud::RefreshMinimap()
{
	if (!Minimap.IsValid() || !GetWorld()) { return; }
	const AWaveDirector* Dir = Director.Get();
	TArray<FVector2D> Enemies, Keys, Chests, Lockeds, Bosses, Altars;
	for (TActorIterator<AEnemyGrunt> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead()) { Enemies.Add(FVector2D(It->GetActorLocation())); }
	}
	for (TActorIterator<ABossBase> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead()) { Bosses.Add(FVector2D(It->GetActorLocation())); }
	}
	for (TActorIterator<ABossAltar> It(GetWorld()); It; ++It)
	{
		// 激活成功即Destroy；只画实际在场待激活祭坛，不按时间表推造图标。
		Altars.Add(FVector2D(It->GetActorLocation()));
	}
	for (TActorIterator<AKeyPickup> It(GetWorld()); It; ++It) { Keys.Add(FVector2D(It->GetActorLocation())); }
	auto VisibleChest = [&](const AActor* Actor)
	{
		const FVector P = Actor->GetActorLocation();
		return !Dir || Dir->IsExplored(P.X, P.Y);
	};
	for (TActorIterator<ATreasureChest> It(GetWorld()); It; ++It)
	{
		if (VisibleChest(*It)) { Chests.Add(FVector2D(It->GetActorLocation())); }
	}
	for (TActorIterator<ALockedChest> It(GetWorld()); It; ++It)
	{
		if (VisibleChest(*It)) { Lockeds.Add(FVector2D(It->GetActorLocation())); }
	}
	FVector2D PlayerXY = FVector2D::ZeroVector, BuddyXY = FVector2D::ZeroVector, MoveDir = FVector2D::ZeroVector;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PlayerXY = FVector2D(Player->GetActorLocation());
		if (Player->GetVelocity().Size2D() > 50.f) { MoveDir = FVector2D(Player->GetVelocity().GetSafeNormal2D()); }
	}
	if (const AActor* Buddy = UGameplayStatics::GetActorOfClass(GetWorld(), ACompanionCharacter::StaticClass()))
	{
		BuddyXY = FVector2D(Buddy->GetActorLocation());
	}
	Minimap->SetWorldData(PlayerXY, BuddyXY, Enemies, Keys, Chests, Lockeds, Bosses, Altars,
		Dir ? Dir->GetFogReveal() : nullptr, AWaveDirector::FogGridN, AWaveDirector::FogWorldHalfExtent, MoveDir);
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		const bool bTab = PC->IsInputKeyDown(EKeys::Tab);
		if (bTab != bMinimapZoomed)
		{
			bMinimapZoomed = bTab; Minimap->SetZoomed(bTab);
			if (MinimapBox.IsValid())
			{
				MinimapBox->SetWidthOverride(bTab ? 400.f : 150.f);
				MinimapBox->SetHeightOverride(bTab ? 400.f : 150.f);
			}
		}
	}
	// 高度只读抽样，明确仅接受真实Landscape首击；每轮最多48条，不冻结一帧扫描全图。
	constexpr int32 Total = MinimapTerrainN * MinimapTerrainN;
	if (MinimapTerrainHeights.Num() != Total) { MinimapTerrainHeights.Init(MAX_flt, Total); }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MinimapTerrain), false);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	int32 TraceBudget = 48;
	for (; MinimapTerrainCursor < Total && TraceBudget > 0; ++MinimapTerrainCursor)
	{
		if (MinimapTerrainHeights[MinimapTerrainCursor] != MAX_flt) { continue; }
		--TraceBudget;
		const int32 X = MinimapTerrainCursor % MinimapTerrainN, Y = MinimapTerrainCursor / MinimapTerrainN;
		const float Wx = -AWaveDirector::FogWorldHalfExtent + X * (2.f * AWaveDirector::FogWorldHalfExtent / (MinimapTerrainN - 1));
		const float Wy = -AWaveDirector::FogWorldHalfExtent + Y * (2.f * AWaveDirector::FogWorldHalfExtent / (MinimapTerrainN - 1));
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(Wx, Wy, 20000.f), FVector(Wx, Wy, -20000.f), Objects, Params)
			&& Cast<ULandscapeHeightfieldCollisionComponent>(Hit.GetComponent()))
		{
			MinimapTerrainHeights[MinimapTerrainCursor] = Hit.ImpactPoint.Z;
			++MinimapTerrainHits;
		}
	}
	if (MinimapTerrainCursor == Total && !bMinimapTerrainUploaded)
	{
		// 只对缺样补采，最多两轮；永久缺样继续MAX_flt，不能拿任意障碍高度补造地形。
		if (MinimapTerrainHits < Total && MinimapTerrainRetryPass < 2)
		{
			++MinimapTerrainRetryPass;
			MinimapTerrainCursor = 0;
			return;
		}
		Minimap->SetTerrainData(MinimapTerrainHeights, MinimapTerrainN);
		bMinimapTerrainUploaded = true;
	}
}

FString UCompanionHud::DebugMinimapState() const
{
#if WITH_EDITOR
	const AWaveDirector* Dir = Director.Get();
	const TArray<float>* Reveal = Dir ? Dir->GetFogReveal() : nullptr;
	int32 Full = 0, Partial = 0;
	if (Reveal)
	{
		for (const float R : *Reveal) { if (R >= 0.999f) { ++Full; } else if (R > 0.f) { ++Partial; } }
	}
	float Low = MAX_flt, High = -MAX_flt;
	for (const float H : MinimapTerrainHeights)
	{
		if (H != MAX_flt) { Low = FMath::Min(Low, H); High = FMath::Max(High, H); }
	}
	return FString::Printf(TEXT("{\"slate_ready\":%s,\"zoomed\":%s,\"fog_n\":%d,\"fog_samples\":%d,"
		"\"fully_revealed_samples\":%d,\"soft_edge_samples\":%d,\"canvas_reveal_samples\":%d,"
		"\"terrain_n\":%d,\"terrain_sample_cursor\":%d,\"terrain_sample_total\":%d,\"terrain_landscape_hits\":%d,"
		"\"terrain_retry_pass\":%d,\"terrain_missing_samples\":%d,\"terrain_uploaded\":%s,\"terrain_min_cm\":%.3f,\"terrain_max_cm\":%.3f,\"contour_segments\":%d,"
		"\"boss_markers_at_last_poll\":%d,\"altar_markers_at_last_poll\":%d,\"visible_chests_at_last_poll\":%d}"),
		Minimap.IsValid() ? TEXT("true") : TEXT("false"), bMinimapZoomed ? TEXT("true") : TEXT("false"),
		AWaveDirector::FogGridN, Reveal ? Reveal->Num() : 0, Full, Partial, Minimap.IsValid() ? Minimap->GetRevealSampleCount() : 0,
		MinimapTerrainN, MinimapTerrainCursor, MinimapTerrainN * MinimapTerrainN, MinimapTerrainHits,
		MinimapTerrainRetryPass, MinimapTerrainN * MinimapTerrainN - MinimapTerrainHits,
		bMinimapTerrainUploaded ? TEXT("true") : TEXT("false"), Low == MAX_flt ? 0.f : Low, High == -MAX_flt ? 0.f : High,
		Minimap.IsValid() ? Minimap->GetContourSegmentCount() : 0, Minimap.IsValid() ? Minimap->GetBossMarkerCount() : 0,
		Minimap.IsValid() ? Minimap->GetAltarMarkerCount() : 0, Minimap.IsValid() ? Minimap->GetChestMarkerCount() : 0);
#else
	return FString();
#endif
}
