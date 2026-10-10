/**
 * [INPUT]: RunEventLog's record contract, Game/PIE world time and game-thread publishers.
 * [OUTPUT]: Appends factual text, evicts the oldest entries above 100 and increments a HUD revision.
 * [POS]: Events/ implementation; no timers, actors, audio, presentation or invented events.
 * [PROTOCOL]: Update Events/AGENTS.md when the record contract or lifetime changes.
 */
#include "Events/RunEventLog.h"

#include "AICompanionDemo.h"
#include "Engine/World.h"

void URunEventLog::Publish(UWorld* World, const FString& Speaker, const FString& Text, const FName EventId)
{
	if (!IsValid(World) || Text.IsEmpty()) return;
	check(IsInGameThread());
	URunEventLog* Journal = World->GetSubsystem<URunEventLog>();
	if (!Journal) return;

	FRunEventEntry Entry;
	Entry.Seq = Journal->NextSequence++;
	Entry.RunTime = World->GetTimeSeconds();
	Entry.Speaker = Speaker;
	Entry.Text = Text;
	Entry.EventId = EventId;
	Journal->Entries.Add(MoveTemp(Entry));
	if (Journal->Entries.Num() > MaxEntries)
	{
		Journal->Entries.RemoveAt(0, Journal->Entries.Num() - MaxEntries, EAllowShrinking::No);
	}
	++Journal->Revision;

	const FRunEventEntry& Published = Journal->Entries.Last();
	UE_LOG(LogAICompanionDemo, Log, TEXT("[RunEvent] seq=%lld time=%.2f event=%s speaker=%s text=%s"),
		Published.Seq, Published.RunTime, *Published.EventId.ToString(), *Published.Speaker, *Published.Text);
}

bool URunEventLog::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URunEventLog::Deinitialize()
{
	Entries.Reset();
	NextSequence = 1;
	Revision = 0;
	Super::Deinitialize();
}
