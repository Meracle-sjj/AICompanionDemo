/**
 * [INPUT]: 依赖 Engine 的 AActor/StaticMeshComponent（BasicShapeMaterial 红染圆柱）/PointLightComponent、Kismet 的 GetPlayerPawn、本模块 BossBase 的激活 spawn（BossClass 蓝图/C++ 类注入）、DrawDebugHelpers（常驻红柱）
 * [OUTPUT]: 对外提供 ABossAltar——Boss 祭坛（09-30 三 Boss 终局：定时在地图冒出，玩家接近即激活 spawn Boss）：
 *   SetBossInfo 注入 Boss 类/名/序号；红柱常驻标识（25m 可见）+ 接近 500cm 自动激活 → spawn Boss 于祭坛位置 → 自毁
 * [POS]: AI/ 的 Boss 终局引导件（Boss 波 1）：由 WaveDirector 定时 spawn（3/6/9min 三档），位置=玩家周围 60-80m 随机角
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossAltar.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ABossBase;

UCLASS()
class AICOMPANIONDEMO_API ABossAltar : public AActor
{
	GENERATED_BODY()

public:
	ABossAltar();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 身份注入（WaveDirector spawn 时调用）：要召唤的 Boss 类与名/序号 */
	void SetBossInfo(TSubclassOf<ABossBase> InBossClass, const FString& InName, int32 InIndex);

protected:
	/** 激活：祭坛自毁，Boss 于祭坛位置降世 */
	void ActivateBoss();

	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<class UPointLightComponent> AltarLight;

	TSubclassOf<ABossBase> BossClass;
	FString BossName = TEXT("BOSS");
	int32 BossIndex = 1;
};
