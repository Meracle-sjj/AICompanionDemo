/**
 * [INPUT]: 宿主实际双来源寒霜状态、Health死亡委托、WorldWidget与游戏暂停时钟。
 * [OUTPUT]: UFrostMarkVisual慢落雪花标记；同一组件刷新，不染身体、无碰撞，死亡/EndPlay释放资源。
 * [POS]: Vfx纯显示；冰状态、传播资格与计时属于EnemyGrunt，不由图形反推玩法。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "FrostMarkVisual.generated.h"

class UHealthComponent;
class SFrostGlyph;

UCLASS(ClassGroup=(Visual))
class AICOMPANIONDEMO_API UFrostMarkVisual : public UWidgetComponent
{
	GENERATED_BODY()
public:
	UFrostMarkVisual(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void SetFrosted(bool bInFrosted);
	FString DebugVisualState() const;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void ReleaseResources() override;
private:
	void FaceCamera();
	UFUNCTION() void HandleDeath(UHealthComponent* Health);
	TSharedPtr<SFrostGlyph> Glyph;
	TWeakObjectPtr<UHealthComponent> ObservedHealth;
	float Clock = 0.f;
	bool bFrosted = false;
};
