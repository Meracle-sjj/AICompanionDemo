/**
 * [INPUT]: Health模式/减伤结算后、HP截断前的完整伤害与类别/落点/内部归属快照，游戏时钟、PlayerController投影与GameMode终局状态。
 * [OUTPUT]: 既有金币与全伤害漫画数字；十族类型图样、20–32pt十伤害档、最多50%拥挤透明及含图标/描边的屏幕梯子排布，64条短命反馈与真实绘制快照。
 * [POS]: UI唯一正式数值反馈层；Health单次提交完整伤害，来源只留内部审计；画面放不下仅暂缓旧条绘制，暂停冻结/终局清账，不改变账本或事件寿命。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Layout/SlateRect.h"
#include "Subsystems/WorldSubsystem.h"
#include "Health/CombatDamageFeedback.h"
#include "CombatFeedbackSubsystem.generated.h"

class UCombatFeedbackSubsystem;

enum class ECombatFeedbackIcon : uint8 { Coin, Cleaver, Sword, Frost, Drop, Lightning, Spikes, Ray, Impact, Flame, Melt };

struct FCombatFeedbackEntry
{
	FVector Location = FVector::ZeroVector;
	float Amount = 0.f;
	float Age = 0.f;
	int32 MarkCount = 0;
	ECombatFeedbackIcon Icon = ECombatFeedbackIcon::Coin;
	ECombatDamageKind Kind = ECombatDamageKind::Generic;
	ECombatDamageSource Source = ECombatDamageSource::Unknown;
	FLinearColor Tint = FLinearColor::White;
};

/** 只记录上一帧实际排布；不参与伤害、事件寿命或下一帧位置决策。 */
struct FCombatFeedbackPaintSnapshot
{
	float Amount = 0.f;
	float Age = 0.f;
	int32 EntryIndex = 0;
	float FontPoints = 0.f;
	int32 LayoutAttempts = 0;
	bool bCoin = false;
	FSlateRect Bounds;
	float Alpha = 1.f;
};

UCLASS()
class AICOMPANIONDEMO_API UCombatFeedbackLayer : public UUserWidget
{
	GENERATED_BODY()
public:
	void Setup(UCombatFeedbackSubsystem* InSource);
	FString DebugPaintState() const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	TWeakObjectPtr<UCombatFeedbackSubsystem> Source;
	mutable TArray<FCombatFeedbackPaintSnapshot> LastPaint;
	mutable FVector2f LastViewport = FVector2f::ZeroVector;
	mutable float LastDpi = 1.f;
	mutable float LastCrowdOpacity = 1.f;
	mutable int32 LastProjected = 0;
	mutable int32 LastSuppressed = 0;
};

UCLASS()
class AICOMPANIONDEMO_API UCombatFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static void ShowGold(UWorld* World, const FVector& Location, int32 Amount);
	static void ShowExecute(UWorld* World, const FVector& Location, float ResolvedDamage, int32 MarkCount);
	static void ShowDamage(UWorld* World, const FCombatDamageContext& Context, float ResolvedDamage, ECombatDamageSource Source);
	void ClearForEnding();
	const TArray<FCombatFeedbackEntry>& GetEntries() const { return Entries; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	UFUNCTION(BlueprintPure, Category="Debug") FString DebugFeedbackState() const;
	UFUNCTION(BlueprintCallable, Category="Debug") void DebugEmitFeedback(bool bExecute, int32 Amount);
	/** 仅Editor显示夹具；不修改生命/金币，不代表真实战斗验证。 */
	UFUNCTION(BlueprintCallable, Category="Debug") void DebugEmitDamageFeedback(int32 Kind, float Amount, int32 Source, int32 Marks = 0);
private:
	void Push(const FCombatFeedbackEntry& Entry);
	TArray<FCombatFeedbackEntry> Entries;
	UPROPERTY(Transient) TObjectPtr<UCombatFeedbackLayer> Layer;
	bool bEndingMuted = false;
};
