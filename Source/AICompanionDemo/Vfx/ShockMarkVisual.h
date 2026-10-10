/** [INPUT]: 宿主感电残留/死亡与World Widget/本地相机。
 * [OUTPUT]: 固定青白闪电标记；无感电时隐藏停Tick，死亡立即隐藏，EndPlay解绑并释放Slate。
 * [POS]: Vfx纯显示；不拥有残留计时/伤害，不生成散乱Niagara。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md */
#pragma once
#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "ShockMarkVisual.generated.h"

class UHealthComponent;

UCLASS(ClassGroup=(Visual))
class AICOMPANIONDEMO_API UShockMarkVisual : public UWidgetComponent
{
	GENERATED_BODY()
public:
	UShockMarkVisual(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void SetShocked(bool bInShocked);
	FString DebugVisualState() const;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void ReleaseResources() override;
private:
	void FaceCamera();
	UFUNCTION() void HandleDeath(UHealthComponent* Health);
	TSharedPtr<SWidget> Glyph;
	TWeakObjectPtr<UHealthComponent> ObservedHealth;
	bool bShocked = false;
};
