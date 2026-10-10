/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent/UPointLightComponent（基础几何 + BasicShapeMaterial 染色兜底）与 Config ini 换装、Kismet 的 GetPlayerPawn、本模块 AICompanionDemoCharacter 的持钥口（GiveTreasureKey）
 * [OUTPUT]: 对外提供 AKeyPickup——宝箱钥匙拾取物（09-25 深度扩展 C，用户定调"捡钥匙开带锁大箱"）：
 *   地图摆点，走近 120cm 自动拾取 → 玩家获得持钥状态（开锁箱的条件）+ 教学飘字 → 自毁；
 *   无磁吸（宝箱同张力——钥匙要专门绕路去找）；**放大+自发光辨识（09-30 三代目：金球 100cm/点光 14-1400/自发光材质 M_KeyGlow（Emissive 不受日光影响+Bloom 泛光晕，/Game/Materials 由 tools/gen_emissive_material.py 生成，参数化 KeyColor；缺失时 BasicShapeMaterial 哑光兜底）——前史：信标黑柱案（Cylinder 无参材质染色失效）/Debug 光柱案（违和已删）**
 * [POS]: AI/ 拾取物家族第五系（蛋糕"即时消耗"/信标"解锁技能"之外的第三语义：**持有型钥匙**——拾取不立即生效，为锁箱埋钩子）
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KeyPickup.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USphereComponent;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API AKeyPickup : public AActor
{
	GENERATED_BODY()

public:
	AKeyPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 拾取半径（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key", meta = (ClampMin = "50.0"))
	float CollectRadius = 120.f;

	/** 外观覆盖：Fab 锈钥匙（Old_Rusty_Key）软引用 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装缩放（钥匙 13cm 实尺——09-30 终局定罪：本值被 ini [/Script/AICompanionDemo.KeyPickup] 覆盖，
	 *  旧值 3.0=39cm 四轮放大全部被 BeginPlay 换装覆盖回真实尺寸=隐身终局真相；现 ini 20.0=2.6m 大钥匙） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 20.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 拾取：玩家获得持钥状态 + 教学飘字 + 自毁 */
	void Collect();

	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 金光点光（与宝箱金同族——"钥匙开金箱"的暗示） */
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UPointLightComponent> KeyLight;

	/** 常驻 Magic 光效 Actor（BeginPlay 挂，Collect 销毁——随钥匙生死） */
	UPROPERTY(Transient)
	TObjectPtr<AActor> MagicVfx;

	/** 环绕卫星×3（09-30 特效升级：程序化 Mesh 粒子——小金球绕钥匙公转，替代 Debug 线条的手搓感） */
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<USphereComponent> Orbiters[3];

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	float BobPhase = 0.f;
	float OrbitalPhase = 0.f;
	float BaseScale = 2.f;   /** 中心球基准缩放（换装覆写后记录——呼吸脉动乘算基准） */
	FVector RestLocation = FVector::ZeroVector;
};
