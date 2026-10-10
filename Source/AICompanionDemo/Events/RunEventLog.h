/**
 * [INPUT]: UWorldSubsystem lifetime and factual gameplay text published by event owners.
 * [OUTPUT]: URunEventLog stores the latest 100 ordered records for one Game/PIE world.
 * [POS]: Events/ shared read-only source for the HUD history; owns no actors or gameplay state.
 * [PROTOCOL]: Update Events/AGENTS.md when the record contract or lifetime changes.
 */
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RunEventLog.generated.h"

USTRUCT()
struct FRunEventEntry
{
	GENERATED_BODY()

	UPROPERTY() int64 Seq = 0;
	UPROPERTY() double RunTime = 0.0;
	UPROPERTY() FString Speaker;
	UPROPERTY() FString Text;
	UPROPERTY() FName EventId = NAME_None;
};

/** A bounded journal that shares one run world's lifetime; EventId is metadata, not a deduplication key. */
UCLASS()
class AICOMPANIONDEMO_API URunEventLog : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static void Publish(UWorld* World, const FString& Speaker, const FString& Text, const FName EventId = NAME_None);
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
