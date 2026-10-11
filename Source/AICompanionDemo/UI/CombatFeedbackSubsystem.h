/**
 * [INPUT]: 世界游戏时钟、PlayerController投影、UMG/Slate矢量绘制与GameMode终局状态
 * [OUTPUT]: 金币/处决图标和粗体描边数字；每世界64条短命反馈，终局显式清理并拒绝新增
 * [POS]: UI的正式数值反馈层；业务只提交实际数量，不再用引擎DebugCanvas展示这两类反馈
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatFeedbackSubsystem.generated.h"

class UCombatFeedbackSubsystem;

enum class ECombatFeedbackIcon : uint8 { Coin, Cleaver };

struct FCombatFeedbackEntry
{
	FVector Location = FVector::ZeroVector;
	float Amount = 0.f;
	float Age = 0.f;
	int32 MarkCount = 0;
	ECombatFeedbackIcon Icon = ECombatFeedbackIcon::Coin;
};

UCLASS()
class AICOMPANIONDEMO_API UCombatFeedbackLayer : public UUserWidget
{
	GENERATED_BODY()
public:
	void Setup(UCombatFeedbackSubsystem* InSource);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	TWeakObjectPtr<UCombatFeedbackSubsystem> Source;
};

UCLASS()
class AICOMPANIONDEMO_API UCombatFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static void ShowGold(UWorld* World, const FVector& Location, int32 Amount);
	static void ShowExecute(UWorld* World, const FVector& Location, float ActualDamage, int32 MarkCount);
	void ClearForEnding();
	const TArray<FCombatFeedbackEntry>& GetEntries() const { return Entries; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	UFUNCTION(BlueprintPure, Category="Debug") FString DebugFeedbackState() const;
	UFUNCTION(BlueprintCallable, Category="Debug") void DebugEmitFeedback(bool bExecute, int32 Amount);
private:
	void Push(const FCombatFeedbackEntry& Entry);
	TArray<FCombatFeedbackEntry> Entries;
	UPROPERTY(Transient) TObjectPtr<UCombatFeedbackLayer> Layer;
	bool bEndingMuted = false;
};
