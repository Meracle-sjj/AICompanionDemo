/** [INPUT]: 两个感电敌人弱引用、既有合法Spline Electric Niagara及引擎Spline DI契约。
 * [OUTPUT]: 胸口到胸口九点锯齿电弧；0.35游戏秒、两端死亡/EndPlay清理，真实端点/绑定只读验收。
 * [POS]: Vfx显示Actor，不施伤、不刷新感电、不持有目标强引用或改原NS资源。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md */
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShockArcActor.generated.h"
class USplineComponent;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS()
class AICOMPANIONDEMO_API AShockArcActor : public AActor
{
	GENERATED_BODY()
public:
	AShockArcActor();
	bool Connect(AActor* From, AActor* To);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION(BlueprintPure, Category="Debug|Elements") FString DebugArcState() const;
private:
	void UpdatePath();
	bool BindSplineInterfaces();
	UPROPERTY(VisibleAnywhere) TObjectPtr<USplineComponent> ArcPath;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Lightning;
	UPROPERTY() TObjectPtr<UNiagaraSystem> LightningSystem;
	TWeakObjectPtr<AActor> FromActor, ToActor;
	TArray<FName> BindingNames;
	float Remaining=.35f;
	float Age=0.f;
	int32 SplineInterfaceCount=0;
	bool bBindingReady=false;
};
