/**
 * [INPUT]: AActor/Engine Cylinder组件、独立M_SummonColumn柔边红材质、玩家Health与BossBase身份/生成。
 * [OUTPUT]: 接近500cm后内部三游戏秒扩大现场红光柱至覆盖祭坛，再唯一生成Boss；零界面文字，暂停/奖励冻结，死亡/结束/EndPlay清柱。
 * [POS]: AI/ 的 Boss 终局引导件（Boss 波 1）：由 WaveDirector 定时 spawn（3/6/9min 三档），位置=玩家周围 60-80m 随机角
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossAltar.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ABossBase;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class AICOMPANIONDEMO_API ABossAltar : public AActor
{
	GENERATED_BODY()

public:
	ABossAltar();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 身份注入（WaveDirector spawn 时调用）：要召唤的 Boss 类与名/序号 */
	void SetBossInfo(TSubclassOf<ABossBase> InBossClass, const FString& InName, int32 InIndex);
	UFUNCTION(BlueprintPure, Category = "Debug|Boss") FString DebugAltarState() const;

protected:
	/** 预备结束后唯一激活；Boss于原祭坛位置降世，随后祭坛自毁。 */
	void ActivateBoss();
	void StartCountdown();
	void ClearCountdown();
	void UpdateSummonColumn();

	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Altar")
	TObjectPtr<class UPointLightComponent> AltarLight;

	TSubclassOf<ABossBase> BossClass;
	FString BossName = TEXT("BOSS");
	int32 BossIndex = 1;
	UPROPERTY(VisibleAnywhere, Category = "Altar") TObjectPtr<UStaticMeshComponent> SummonColumn;
	UPROPERTY() TObjectPtr<UMaterialInterface> SummonColumnMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SummonColumnMID;
	float CountdownRemaining = 0.f;
	bool bCountingDown = false;
	bool bSummonCommitted = false;
};
