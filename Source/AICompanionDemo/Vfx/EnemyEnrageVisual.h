/**
 * [INPUT]: EnemyGrunt生命周期、预Cook的NS_ColoredFireMid及其用户材质/颜色参数，紫红MI_EnrageFlame。
 * [OUTPUT]: 两处贴身紫红火焰覆盖下身/上身；14分钟沿Z抬高1.8倍，原XY范围/粒子数保持；死亡/关闭/EndPlay销毁，暂停冻结。
 * [POS]: Vfx/敌人狂暴纯显示层，不拥有狂暴数值或敌人伤害。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
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
	void SetHeightened(bool bInHeightened);
	bool IsEnraged() const { return bEnraged; }
	FString DebugVisualState() const;
	UPROPERTY(EditAnywhere, Category="Enemy|Enrage") FLinearColor FlameColor = FLinearColor(1.f, .025f, .65f, 1.f);
	UPROPERTY(EditAnywhere, Category="Enemy|Enrage", meta=(ClampMin="0.1")) float FlameScale = .8f;

private:
	// 两引用均指向已Cook资产；组件拥有实例参数，不拥有系统副本。
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> FlameSystem;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> SourceFlameSystem;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> EnrageMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Flames;
	bool bEnraged = false;
	bool bHeightened = false;
	bool bCompatibleColor = false;
	bool bSafeColoredSystem = false;
	int32 BuildCount = 0;
	bool BuildFlames();
	void DestroyFlames();
};
