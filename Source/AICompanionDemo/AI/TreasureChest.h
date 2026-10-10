/**
 * [INPUT]: Engine的Actor/静态网格/贴地Decal/世界厘米MaterialBillboard、原创M_StarterHalo/M_StarterMote，世界Tick与真实暂停、Health/导演终局和开箱账本；固定Niagara衍生资产仅用于离线Editor制作桥。
 * [OUTPUT]: 普通收费箱及初始免费箱；初始箱柔和紫色贴地光环+六个稀疏紫色上浮光点，Actor Tick驱动位置/呼吸/透明度；领取/死亡/终局/EndPlay清理，暂停冻结；免费箱不改变钱包/收费计数，运行宝箱不再创建Niagara火焰。
 * [POS]: AI/ 的道具交互代表件（宝箱经济的消费终端）；被 GameMode BeginPlay 代码生成于固定点（D6 范式），交互输入在玩家侧（Character Tick E 键边沿），开箱账本在 WaveDirector——三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreasureChest.generated.h"

class UStaticMeshComponent;
class AWaveDirector;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UDecalComponent;
class UMaterialBillboardComponent;

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
	/** 离线资产制作桥：仅编辑器固定专用系统路径可用，正式Runtime返回false。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Chest")
	bool DebugConfigureColoredFlameAsset(UNiagaraSystem* System);

	// ---------- 外观换装（Fab 素材，2026-09-18；语义见 XpPickup 同名块） ----------

	/** 外观覆盖：换装后盖条退役（原木宝箱自带完整剪影），开箱染灰对无 "Color" 参数材质 no-op（表现退化为仅盖藏/爆闪） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装均匀缩放（原木箱 90cm 宽与现役箱体恰好同尺寸配 1.0） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 开箱表现：箱体染灰 + 盖条抬起 + 爆闪——"这个箱子用掉了"的状态语言 */
	void PlayOpenFx();

	/** 应用换装：箱体替换 + 盖条隐藏；返回是否已换装（换装=原生材质说话，跳过染色） */
	bool ApplyMeshOverride();
	bool BuildStarterAura();
	void TickStarterAura(float DeltaSeconds);
	void ClearStarterAura();

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> LidMesh;

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UPointLightComponent> StarterLight;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StarterFlameBaseMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StarterHaloBaseMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StarterMoteBaseMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> StarterHalo;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StarterHaloMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialBillboardComponent>> StarterMotes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StarterMoteMaterials;
	bool bStarterChest = false;
	bool bStarterAuraRetired = false;
	float StarterAuraAge = 0.f;
	float StarterHaloOpacity = 0.f;
	FVector StarterGroundLocation = FVector::ZeroVector;
	FVector StarterGroundNormal = FVector::UpVector;

	/** 已开：不可重复交互 */
	bool bOpened = false;
};
