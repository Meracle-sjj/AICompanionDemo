/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent（/Engine 基础球体）与 Tick、Kismet 的 GetPlayerPawn（磁吸目标）、本模块 WaveDirector（收集经 GameMode 中枢上报 AddXp）、本模块 AICompanionDemoGameMode（组合根查询）、本模块 TemporaryPickup（磁吸风暴征集入口——ForceMagnetToPlayer 的调用方）
 * [OUTPUT]: 对外提供 AXpPickup——经验晶体（M3.75 B5）：敌人死亡掉落，玩家走近磁吸加速飞来、接触收集入账；浮动+自转的表现层，"队友击杀效率=玩家经验收入"的成长循环入口；E5 增补 ForceMagnetToPlayer（强制磁吸态，磁吸风暴的全图征集出口）
 * [POS]: AI/ 的 VS 成长循环入口件；被 EnemyGrunt::HandleDeath 生成（掉落方负责喂 XpValue），收集上报走 GameMode→WaveDirector 唯一中枢（D6 组合根精神，不反向注入指针）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "XpPickup.generated.h"

class UStaticMeshComponent;
class AWaveDirector;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API AXpPickup : public AActor
{
	GENERATED_BODY()

public:
	AXpPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 携带经验值：掉落方（EnemyGrunt 变体）在生成后设置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Xp")
	int32 XpValue = 2;

	/** 磁吸半径（厘米）：玩家进入即被吸走，越吸越快 */
	UPROPERTY(EditAnywhere, Category = "Xp", meta = (ClampMin = "50.0"))
	float MagnetRadius = 300.f;

	/** 收集半径（厘米）：接触即入账销毁 */
	UPROPERTY(EditAnywhere, Category = "Xp", meta = (ClampMin = "10.0"))
	float CollectRadius = 50.f;

	/** E5 磁吸风暴：强制磁吸态——无视磁吸半径恒速直飞玩家，到时自解（风暴收集时全图征集） */
	void ForceMagnetToPlayer(float Duration);

	// ---------- 外观换装（Fab 素材，2026-09-18） ----------

	/** 外观覆盖：Fab 素材软引用，空 = 默认 /Engine 几何；类级 Config 存 DefaultGame.ini——D18 免疫（ini 非关卡资产）+ 换装零编译 */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装均匀缩放（Fab 模型尺寸不一：水晶原高 479cm 配 0.07） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 收集入账：经 GameMode 组合根找到 WaveDirector（唯一中枢单例），再飘 +N 反馈、自毁 */
	void Collect();

	/** 应用换装：BeginPlay 期加载软引用替换默认几何；返回是否已换装（换装=原生材质说话，跳过染色） */
	bool ApplyMeshOverride();

	/** 强制磁吸到期（E5 磁吸风暴生命期，定时器回调） */
	void EndForceMagnet();

	/** 强制磁吸态：无视半径直飞（E5 磁吸风暴） */
	bool bForceMagnet = false;

	/** 强制磁吸到期定时器（重复触发刷新，ApplySlow 同范式） */
	FTimerHandle ForceMagnetTimerHandle;

	UPROPERTY(VisibleAnywhere, Category = "Xp")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 磁吸飞行速度真源（厘米/秒）：入圈从 0 渐增到上限——"越吸越快"的手感 */
	float MagnetSpeed = 0.f;

	/** 待机浮动基准位（BeginPlay 落定）：浮动叠加在它之上，不逐帧漂移 */
	FVector RestLocation = FVector::ZeroVector;

	/** 浮动相位：出生随机，晶体群不同步起伏 */
	float BobPhase = 0.f;
};
