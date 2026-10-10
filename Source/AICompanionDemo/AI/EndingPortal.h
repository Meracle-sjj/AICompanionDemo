/**
 * [INPUT]: Engine场景/实例网格/基础几何与染色材质；GameMode终局/奖励/输入契约；EndingPortalPrompt薄视图
 * [OUTPUT]: 蠕虫死亡后的紫金旋转归途门，无碰撞/导航阻挡；4.5m内按E，正式交互复验距离与奖励；首次获得资格帧跳过按键，后续观察松E才接纳新按下，防领奖穿透；EndPlay移除提示
 * [POS]: AI/世界交互物；门只呈现与提交交互，终局统计/冻结/重载由组合根负责
 * [PROTOCOL]: 变更时更新本头部，然后检查模块AGENTS.md
 */
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EndingPortal.generated.h"

class USceneComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UEndingPortalPrompt;
class AAICompanionDemoGameMode;

UCLASS()
class AICOMPANIONDEMO_API AEndingPortal : public AActor
{
	GENERATED_BODY()
public:
	AEndingPortal();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool IsPlayerNear() const;
	bool TryInteract();
	UFUNCTION(BlueprintPure, Category = "Debug|Ending")
	FString DebugInteractionState() const;
	/** 合成E输入经真实PlayerInput收集，下帧仍由正式Tick/资格/latch决定；不等于物理键盘。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Ending")
	bool DebugInjectInteractKey(bool bDown);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ending", meta = (ClampMin = "150.0"))
	float InteractRadius = 450.f;
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> PurpleRing;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> GoldOrbit;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PortalFace;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PortalLight;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UEndingPortalPrompt> Prompt;
	TWeakObjectPtr<AAICompanionDemoGameMode> Host;
	/** 奖励关闭或首次走近后，须观察松E，再接纳后续新按下。 */
	bool bInteractionEligible = false;
	bool bInteractKeyArmed = false;
};
