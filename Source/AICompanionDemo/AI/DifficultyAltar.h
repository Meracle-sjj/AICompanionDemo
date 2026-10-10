/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent/UPointLightComponent（基础几何 + BasicShapeMaterial 染色兜底）与 Config ini 换装、DrawDebugHelpers（献祭光柱）、Kismet 的 GetPlayerPawn、本模块 WaveDirector（TrySpendGold 扣费/IncreaseDifficulty 加档）
 * [OUTPUT]: 对外提供 ADifficultyAltar——难度祭坛（09-25 深度扩展 B，用户定调"花钱手动加压"）：
 *   地图交互建筑，250cm 内按 E 花 8 金献祭 → 难度档 +1（敌 Hp ×1.3/经验 ×1.25 上浮，本局后刷的怪全部生效）→ 光柱谢幕后消失；
 *   每档效果叠加、档数无上限——强者主动加压换成长速度（Megabonk"难度即资源"的交互化）
 * [POS]: AI/ 交互物家族第三件（宝箱"买成长"/信标"买位置"之外的第三极"买压力"）；Ritual_Stone 换装 ini 配置
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DifficultyAltar.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API ADifficultyAltar : public AActor
{
	GENERATED_BODY()

public:
	ADifficultyAltar();

	virtual void Tick(float DeltaSeconds) override;

	/** 献祭代价（金币） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Altar", meta = (ClampMin = "0"))
	int32 SacrificeCost = 8;

	/** 交互半径（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Altar", meta = (ClampMin = "50.0"))
	float InteractRadius = 250.f;

	/** 外观覆盖：Fab 仪式石（Ritual_Stone）软引用 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装缩放（仪式石原尺寸待实测定标） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

	/** 是否已献祭 */
	UFUNCTION(BlueprintPure, Category = "Altar")
	bool IsSacrificed() const { return bSacrificed; }

	/** 玩家侧 E 键入口（Character Tick 边沿调用） */
	bool TrySacrifice();

protected:
	virtual void BeginPlay() override;

private:
	/** 接近提示（头顶浮字，宝箱同款） */
	void DrawInteractHint();

	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 猩红点光：与信标青光/宝箱金光的色语言分家——"危险交易" */
	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<UPointLightComponent> AltarLight;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	bool bSacrificed = false;
};
