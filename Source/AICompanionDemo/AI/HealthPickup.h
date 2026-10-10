/**
 * [INPUT]: 依赖 Engine 的 StaticMesh/MaterialInstanceDynamic（/Engine 基础圆柱 × BaseColor 染色——XpPickup 实证管线）、DrawDebugString（拾取 +N 飘字，Development 保留）、Kismet 的 GetPlayerPawn、本模块 HealthComponent 的 Heal/GetCurrentHealth/GetMaxHealth
 * [OUTPUT]: 对外提供 AHealthPickup——柠檬蛋糕回血拾取物：双层圆柱零资产拼装（黄蛋糕体+白奶油顶）/Engine 基础几何染色；碰撞全关（纯距离判定，XpPickup 同款哲学）；**无磁吸**——玩家必须走到才吃，冒险绕路=走位玩法的张力线；**满血不吃**（蛋糕留在原地等你受伤——"省着用"的微型决策）；浮动自转待机 + 45s 无人拾取自毁
 * [POS]: AI/ 的拾取物家族成员（XpPickup 的同族姊妹）：EnemyGrunt 概率掉落（蛮兵必掉）；收集直接作用于玩家 HealthComponent，不经过 WaveDirector 账本——回血是即时消耗不是成长入账，账本只管 XP
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HealthPickup.generated.h"

class UStaticMeshComponent;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API AHealthPickup : public AActor
{
	GENERATED_BODY()

public:
	AHealthPickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 回血量：拾取即回复（HealthComponent::Heal 钳制在上限） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1.0"))
	float HealAmount = 25.f;

	/** 拾取判定半径（厘米）：走过碰到才吃——刻意无磁吸，蛋糕是走位目标而非自动收入 */
	UPROPERTY(EditAnywhere, Category = "Health", meta = (ClampMin = "30.0"))
	float CollectRadius = 100.f;

	/** 存留时间（秒）：无人拾取自毁（战场不堆积，XpPickup 同款规则） */
	UPROPERTY(EditAnywhere, Category = "Health", meta = (ClampMin = "5.0"))
	float Lifespan = 45.f;

	// ---------- 外观换装（Fab 素材，2026-09-18；语义见 XpPickup 同名块） ----------

	/** 外观覆盖：换装后三件套拼装整体退役（奶油圈/樱桃隐藏，蛋糕体替换为 Fab 模型） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装均匀缩放（蛋糕卷含盘原宽 305cm 配 0.18） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

private:
	/** 收集：玩家 Heal + 头顶飘字 + 日志 + 自毁 */
	void Collect();

	/** 应用换装：蛋糕体替换 + 奶油圈/樱桃隐藏；返回是否已换装（换装=原生材质说话，跳过三件染色） */
	bool ApplyMeshOverride();

	/** BasicShapeMaterial 持有：换装蛋糕缺贴图时的染色兜底（09-20 蛋糕白案） */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMat = nullptr;

	/** 黄色蛋糕体（根组件） */
	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<UStaticMeshComponent> CakeBody;

	/** 白色奶油圈（比蛋糕体外扩形成悬边——"蛋糕"剪影的关键一笔） */
	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<UStaticMeshComponent> CreamTop;

	/** 红樱桃（蛋糕顶点的点睛件，/Engine 基础球体） */
	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<UStaticMeshComponent> Cherry;

	/** 待机浮动相位（随机初相，错开同屏多个蛋糕的同步跳动） */
	float BobPhase = 0.f;

	/** 待机锚点：浮动叠加基准，不逐帧漂移（XpPickup 同款） */
	FVector RestLocation = FVector::ZeroVector;
};
