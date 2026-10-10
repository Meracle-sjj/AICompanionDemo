/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent（/Engine 基础立方体）与 Tick、Kismet 的 GetPlayerPawn/GetGameMode（金币钱包与升级中枢查询）、本模块 WaveDirector（TrySpendGoldForUpgrade——开箱进三选一暂停）、DrawDebugString（交互提示）
 * [OUTPUT]: ATreasureChest普通收费箱及ConfigureStarterChest初始免费箱；免费提示/自有金光球与灯光闪烁，不修改原材质。两类均经导演三选一，已开不可重复；初始箱不改变金币/收费计数。编辑器DebugTryOpen复验实际距离后调用正式TryOpen，DebugChestState只读身份/钱包/附近状态。
 * [POS]: AI/ 的道具交互代表件（宝箱经济的消费终端）；被 GameMode BeginPlay 代码生成于固定点（D6 范式），交互输入在玩家侧（Character Tick E 键边沿），开箱账本在 WaveDirector——三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreasureChest.generated.h"

class UStaticMeshComponent;
class AWaveDirector;
class UPointLightComponent;
class UMaterialInstanceDynamic;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API ATreasureChest : public AActor
{
	GENERATED_BODY()

public:
	ATreasureChest();

	virtual void Tick(float DeltaSeconds) override;

	/** 开箱消耗（金币）：钱包够才开，不够头顶飘提示——"攒钱"的微型经济决策 */
	UPROPERTY(EditAnywhere, Category = "Chest", meta = (ClampMin = "1"))
	int32 OpenCost = 3;

	/** 交互半径（厘米）：玩家进入即显示提示，按 E 开箱 */
	UPROPERTY(EditAnywhere, Category = "Chest", meta = (ClampMin = "50.0"))
	float InteractRadius = 250.f;

	/** 尝试开箱（玩家 E 键调用）：钱够即扣费并触发三选一暂停，返回是否成功（失败时头顶飘原因） */
	bool TryOpen();

	/** 是否已被打开 */
	bool IsOpened() const { return bOpened; }

	/** 组合根正常局装配口，可在SpawnActor之后调用；只改变此箱的奖励身份与自有表现。 */
	UFUNCTION(BlueprintCallable, Category = "Chest")
	void ConfigureStarterChest();
	UFUNCTION(BlueprintPure, Category = "Debug|Chest")
	FString DebugChestState() const;
	/** 编辑器验收桥：复验玩家距离，之后调用正式TryOpen；非编辑器不执行。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Chest")
	bool DebugTryOpen();

	// ---------- 外观换装（Fab 素材，2026-09-18；语义见 XpPickup 同名块） ----------

	/** 外观覆盖：换装后盖条退役（原木宝箱自带完整剪影），开箱染灰对无 "Color" 参数材质 no-op（表现退化为仅盖藏/爆闪） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装均匀缩放（原木箱 90cm 宽与现役箱体恰好同尺寸配 1.0） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 开箱表现：箱体染灰 + 盖条抬起 + 爆闪——"这个箱子用掉了"的状态语言 */
	void PlayOpenFx();

	/** 应用换装：箱体替换 + 盖条隐藏；返回是否已换装（换装=原生材质说话，跳过染色） */
	bool ApplyMeshOverride();

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> LidMesh;

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> StarterGlowMesh;
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UPointLightComponent> StarterLight;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StarterGlowMaterial;
	bool bStarterChest = false;
	float StarterGlowPhase = 0.f;

	/** 已开：不可重复交互 */
	bool bOpened = false;
};
