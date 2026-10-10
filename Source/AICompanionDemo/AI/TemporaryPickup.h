/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent（/Engine 基础几何 Cube/Cylinder/Sphere + BasicShapeMaterial "Color" 染色管线——XpPickup 实证）与 Kismet 的 GameplayStatics/GetPlayerPawn、本模块 XpPickup（磁吸风暴全图征集）、本模块 CompanionAIController（狂怒攻速）、本模块 HealthComponent（圣盾无敌）
 * [OUTPUT]: 对外提供 ATemporaryPickup 与 ERushType——暂态掉落道具 rush 三件套（M3.75 E5，用户 09-13 裁决 Megabonk 借鉴"掉落物太单调"）：磁吸风暴（全图经验晶体 2s 强制飞向玩家）/狂怒攻速（队友开火间隔×0.5 持续 8s 弹道变紫）/圣盾无敌（玩家 5s 伤害早退）；EnemyGrunt 概率掉落（标准/猎犬 2%、蛮兵 20%），种类由掉落方随机定型
 * [POS]: AI/ 拾取物家族第三系（经验青蓝/金币金/蛋糕黄白/宝箱宝蓝四色之外的短时刺激节拍）；即时作用不经 WaveDirector 账本（HealthPickup 蛋糕先例）；碰撞全关纯距离判定 + 磁吸三段式（XpPickup 同族铁律）；效果生命期定时器自恢复（ApplySlow/bark 冷却同范式）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TemporaryPickup.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;

UENUM(BlueprintType)
enum class ERushType : uint8
{
	MagnetStorm,   // 磁吸风暴：全图经验晶体强制飞向玩家——战场"唰"地清空
	Frenzy,        // 狂怒攻速：队友开火间隔 ×0.5 持续 8s，弹道变紫
	Aegis          // 圣盾无敌：玩家 5s 无敌（HealthComponent 伤害早退闸门）
};

UCLASS(Config = Game)   // 09-25 探针实证：漏声明则 DefaultGame.ini 的 [TemporaryPickup] 段整段不读（三系换装全 None）
class AICOMPANIONDEMO_API ATemporaryPickup : public AActor
{
	GENERATED_BODY()

public:
	ATemporaryPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 道具种类：掉落方在 spawn 之后随机赋值（外观只能首帧 Tick 定型——构造/BeginPlay 期均不可知） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rush")
	ERushType RushType = ERushType::MagnetStorm;

	// ---------- Fab 换装（09-25，XpPickup/GoldPickup/HealthPickup 同款管线）：素材到位 ini 填路径即换 ----------

	/** 狂怒（紫药剂）外观覆盖：选择带贴图的素材——缺贴图的 Fab 材质渲染纯白（蛋糕案教训） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> FrenzyMeshOverride;

	/** 磁吸风暴（磁铁）外观覆盖 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MagnetMeshOverride;

	/** 圣盾外观覆盖 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> AegisMeshOverride;

	// ---------- 各系独立缩放（Fab 素材尺寸天差地别：药瓶 2.6m、盾 126m——30cm 观感基准各自定标） ----------

	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.001"))
	float FrenzyMeshScale = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.001"))
	float MagnetMeshScale = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.001"))
	float AegisMeshScale = 1.f;

	/** 磁吸半径（厘米）：暂态道具是稀有爽点，小磁吸鼓励捡取（蛋糕的"无磁吸张力"刻意不沿用） */
	UPROPERTY(EditAnywhere, Category = "Rush", meta = (ClampMin = "50.0"))
	float MagnetRadius = 300.f;

	/** 收集半径（厘米）：接触即触发效果并销毁 */
	UPROPERTY(EditAnywhere, Category = "Rush", meta = (ClampMin = "30.0"))
	float CollectRadius = 100.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 效果分派：三类即时作用（磁吸风暴/狂怒攻速/圣盾无敌），不经 WaveDirector 账本 */
	void Collect();

	/** 外观定型（首帧一次）：形状+颜色按 RushType 三系区分 */
	void ApplyRushVisual();

	/** 磁吸风暴：全图征集经验晶体（TActorIterator 广播式触发，拾取物互不相识） */
	void ApplyMagnetStorm();

	/** 狂怒攻速：征集场上队友控制器（单队友 demo，迭代器写法零成本兼容未来多队友） */
	void ApplyFrenzy();

	/** 圣盾无敌：直达玩家 HealthComponent（蛋糕"收集直达组件"先例） */
	void ApplyAegis();

	UPROPERTY(VisibleAnywhere, Category = "Rush")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 拾取光晕：三系同色点光——远距离可辨（稀有物存在感；掉落低频不构成光源性能压力，无投影） */
	UPROPERTY(VisibleAnywhere, Category = "Rush")
	TObjectPtr<UPointLightComponent> RushLight;

	/** 三系候选形状：构造期 Finder 常驻（非构造函数禁 ConstructorHelpers，只能预存备用） */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MeshCube = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MeshCylinder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MeshSphere = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	/** 外观已定型标记（首帧染色只做一次） */
	bool bVisualApplied = false;

	/** 磁吸飞行速度真源（厘米/秒）：入圈渐增——"越吸越快"的手感（XpPickup 同款） */
	float MagnetSpeed = 0.f;

	/** 待机浮动基准位（BeginPlay 落定）：浮动叠加在它之上，不逐帧漂移 */
	FVector RestLocation = FVector::ZeroVector;

	/** 浮动相位：出生随机，多道具不同步起伏 */
	float BobPhase = 0.f;
};
