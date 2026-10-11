/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent/UPointLightComponent（基础几何 + BasicShapeMaterial 染色兜底）与 Config ini 换装（Monster_Chest 锁箱）、DrawDebugHelpers（大奖爆闪）、Kismet 的 GetPlayerPawn/GetGameMode、本模块 WaveDirector（RefundGold/EnterUpgradePause 奖励口——经组合根）、本模块 AICompanionDemoCharacter 的持钥查询（HasTreasureKey）
 * [OUTPUT]: 九点实际网格贴坡/轻埋沙的大锁箱；接近平滑显现金色钥匙无数字，持钥E标记；原15金+免费六选二/半径保持，成功统一前三台词，开启/死亡/终局/EndPlay清理。
 * [POS]: AI/ 交互物家族第四件：寻宝玩法的兑现端——与 AKeyPickup 构成"找钥匙→开大箱"的探索回路，大奖强度对冲跨图找钥匙的路程成本
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vfx/ChestGrounding.h"
#include "LockedChest.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UChestMarkerVisual;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API ALockedChest : public AActor
{
	GENERATED_BODY()

public:
	ALockedChest();

	virtual void Tick(float DeltaSeconds) override;

	/** 交互半径（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest", meta = (ClampMin = "50.0"))
	float InteractRadius = 250.f;

	/** 开箱金币奖励（免费三选一之外的真金） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest", meta = (ClampMin = "0"))
	int32 GoldReward = 15;

	/** 外观覆盖：Fab 带锁巨箱（Monster_Chest）软引用 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装缩放（原 185cm 高，1.0 直接可用） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

	/** 是否已开启 */
	UFUNCTION(BlueprintPure, Category = "Chest")
	bool IsOpened() const { return bOpened; }

	/** 玩家侧 E 键入口（Character Tick 边沿调用）：持钥才开，返回是否成功 */
	bool TryOpen();
	UFUNCTION(BlueprintPure, Category = "Debug|Chest")
	FString DebugChestState() const;
	/** 编辑器验收桥复验原距离与持钥，再调用真实业务。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Chest")
	bool DebugTryOpen();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/** 接近提示（有无钥匙两种文案） */
	void DrawInteractHint();

	/** 大奖表现：金色三层爆闪 + 光柱 */
	void PlayRewardFx();

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UChestMarkerVisual> ChestMarker;
	FChestGroundContact GroundContact;

	/** 金光点光（锁箱的远距离辨识——比普通宝箱更亮的金） */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UPointLightComponent> ChestLight;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	bool bOpened = false;
};
