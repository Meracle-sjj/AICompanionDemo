/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent/UPointLightComponent（/Engine 基础几何 + BasicShapeMaterial 染色兜底）与 Config ini 换装（TreasureChest 同款软引用）、Kismet 的 GetPlayerPawn、本模块 AICompanionDemoCharacter 的召回解锁口
 * [OUTPUT]: 对外提供 AEmergencyBeacon——紧急呼叫信标·拾取制（09-25 二审，用户定调"捡到信标变成新技能，而不是跑去固定点交互——太死板"）：
 *   地图四座青光方碑，走近 150cm 自动拾取 → 解锁玩家 C 键召回技能（传送队友至身边，CD 在角色侧）+ 头顶飘字教学 → 信标消失；
 *   碰撞全关纯距离判定（拾取物家族哲学）；青色常驻点光=远距离辨识
 * [POS]: AI/ 拾取物家族第四系（蛋糕"即时消耗"之外的新语义：**拾取即解锁能力**——与升级卡并行的技能获取通道）；一审固定交互点方案已废（"太死板"）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmergencyBeacon.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API AEmergencyBeacon : public AActor
{
	GENERATED_BODY()

public:
	AEmergencyBeacon();

	virtual void Tick(float DeltaSeconds) override;

	/** 拾取半径（厘米）：走近即捡（家族纯距离判定） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beacon", meta = (ClampMin = "50.0"))
	float CollectRadius = 150.f;

	/** 外观覆盖：Fab 方碑（Obelisk）软引用——ini 填路径即换装，零编译 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装缩放（方碑原高 53cm→信标建筑 2.1m 观感） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 4.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 拾取判定与解锁（Tick 调用）：玩家入圈 → Character 解锁 C 键召回 + 教学飘字 + 自毁 */
	void Collect();

	/** 碰撞全关的展示体：Fab 方碑（缺省回退基础柱染色） */
	UPROPERTY(VisibleAnywhere, Category = "Beacon")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 青色常驻点光：远距离辨识（宝箱金光/信标青光——色语言分家） */
	UPROPERTY(VisibleAnywhere, Category = "Beacon")
	TObjectPtr<UPointLightComponent> BeaconLight;

	/** BasicShapeMaterial 持有：无换装时的染色兜底 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	/** 待机浮动相位（拾取物家族通用语言） */
	float BobPhase = 0.f;

	/** 待机锚点 */
	FVector RestLocation = FVector::ZeroVector;
};
