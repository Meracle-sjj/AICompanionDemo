/**
 * [INPUT]: 依赖 Engine 的 AActor/UStaticMeshComponent（/Engine 基础球体）与 Tick、Kismet 的 GetPlayerPawn（磁吸目标）、本模块 WaveDirector（收集经 GameMode 中枢上报 AddGold）、本模块 AICompanionDemoGameMode（组合根查询）
 * [OUTPUT]: 对外提供 AGoldPickup——金币（M3.75 E3 宝箱交互链的货币层）：敌人死亡概率掉落，玩家走近磁吸收集入账金币钱包；XpPickup 的姊妹件（同一磁吸三段式范式，账本口不同）
 * [POS]: AI/ 的宝箱经济入口件；被 EnemyGrunt::HandleDeath 生成（掉落方喂 GoldValue），收集上报走 GameMode→WaveDirector 唯一中枢（与 XpPickup 同款职责分离）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GoldPickup.generated.h"

class UStaticMeshComponent;
class AWaveDirector;

UCLASS(Config = Game)
class AICOMPANIONDEMO_API AGoldPickup : public AActor
{
	GENERATED_BODY()

public:
	AGoldPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 携带金币数：掉落方（EnemyGrunt 变体）在生成后设置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gold")
	int32 GoldValue = 1;

	/** 磁吸半径（厘米）：玩家进入即被吸走（09-18 从 300 翻倍——金币散落大陆捡不到是"钱慢"体感的一半根因） */
	UPROPERTY(EditAnywhere, Category = "Gold", meta = (ClampMin = "50.0"))
	float MagnetRadius = 600.f;

	/** 收集半径（厘米）：接触即入账销毁 */
	UPROPERTY(EditAnywhere, Category = "Gold", meta = (ClampMin = "10.0"))
	float CollectRadius = 50.f;

	// ---------- 外观换装（Fab 素材，2026-09-18；语义见 XpPickup 同名块） ----------

	/** 外观覆盖：Fab 素材软引用，空 = 默认 /Engine 几何；类级 Config 存 DefaultGame.ini */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	/** 换装均匀缩放（金币原径 40cm 配 0.45） */
	UPROPERTY(EditAnywhere, Config, Category = "Appearance", meta = (ClampMin = "0.01"))
	float MeshOverrideScale = 1.f;

protected:
	virtual void BeginPlay() override;

private:
	/** 收集入账：经 GameMode 组合根找到 WaveDirector，金币入钱包 + 飘字 + 自毁 */
	void Collect();

	/** 应用换装：BeginPlay 期加载软引用替换默认几何；返回是否已换装（换装=原生材质说话，跳过染色） */
	bool ApplyMeshOverride();

	UPROPERTY(VisibleAnywhere, Category = "Gold")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 磁吸飞行速度真源（厘米/秒）：入圈从 0 渐增到上限 */
	float MagnetSpeed = 0.f;

	/** 待机浮动基准位（BeginPlay 落定） */
	FVector RestLocation = FVector::ZeroVector;

	/** 浮动相位：出生随机，金币群不同步起伏 */
	float BobPhase = 0.f;
};
