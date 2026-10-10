/**
 * [INPUT]: EnemyGrunt生命周期与真实Niagara FireMid的暴露颜色参数。
 * [OUTPUT]: 两处贴身紫红火焰覆盖下身/上身；参数类型核实后激活；死亡/关闭/EndPlay销毁，暂停冻结模拟。
 * [POS]: Vfx/敌人狂暴纯显示层，不拥有狂暴数值或敌人伤害。
 * [PROTOCOL]: 变更时更新此头部；成员地图由主agent统一维护。
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyEnrageVisual.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UMaterialInterface;

UCLASS(ClassGroup=(Custom))
class AICOMPANIONDEMO_API UEnemyEnrageVisual : public UActorComponent
{
	GENERATED_BODY()
public:
	UEnemyEnrageVisual();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	void SetEnraged(bool bInEnraged);
	bool IsEnraged() const { return bEnraged; }
	FString DebugVisualState() const;
	UPROPERTY(EditAnywhere, Category="Enemy|Enrage") FLinearColor FlameColor = FLinearColor(1.f, .025f, .65f, 1.f);
	UPROPERTY(EditAnywhere, Category="Enemy|Enrage", meta=(ClampMin="0.1")) float FlameScale = .8f;

private:
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> FlameSystem;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> SourceFlameSystem;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> EnrageMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Flames;
	bool bEnraged = false;
	bool bCompatibleColor = false;
	bool bSafeColoredSystem = false;
	int32 BuildCount = 0;
	bool BuildFlames();
	void DestroyFlames();
};
