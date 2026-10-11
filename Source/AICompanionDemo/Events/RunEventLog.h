/**
 * [INPUT]: UWorldSubsystem生命周期与真实事件所有者发布的游戏文本。
 * [OUTPUT]: 每世界最近100条有序记录及特殊字幕配色用Kind语义。
 * [POS]: Events/HUD历史共享只读真源，不持有Actor或推进玩法。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RunEventLog.generated.h"

UENUM(BlueprintType)
enum class ERunEventKind : uint8
{
	General,
	AttackUnlock,
	BossAppear,
	BossDefeated
};

USTRUCT()
struct FRunEventEntry
{
	GENERATED_BODY()

	UPROPERTY() int64 Seq = 0;
	UPROPERTY() double RunTime = 0.0;
	UPROPERTY() FString Speaker;
	UPROPERTY() FString Text;
	UPROPERTY() FName EventId = NAME_None;
	UPROPERTY() ERunEventKind Kind = ERunEventKind::General;
};

/** 与本局世界同生命周期的有界记录；EventId仅标记来源，不作为去重键。 */
UCLASS()
class AICOMPANIONDEMO_API URunEventLog : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static void Publish(UWorld* World, const FString& Speaker, const FString& Text, const FName EventId = NAME_None,
		ERunEventKind Kind = ERunEventKind::General);
	const TArray<FRunEventEntry>& GetEntries() const { return Entries; }
	int64 GetRevision() const { return Revision; }
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static constexpr int32 MaxEntries = 100;
	UPROPERTY() TArray<FRunEventEntry> Entries;
	int64 NextSequence = 1;
	int64 Revision = 0;
};
