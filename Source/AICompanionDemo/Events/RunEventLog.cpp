/**
 * [INPUT]: RunEventLog记录契约、Game/PIE世界时间和游戏线程发布者。
 * [OUTPUT]: 追加真实文本/Kind，超过100条淘汰最旧项，递增HUD刷新版本。
 * [POS]: Events/数据实现，不拥有计时器/Actor/音频/界面或虚构事件。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "Events/RunEventLog.h"

#include "AICompanionDemo.h"
#include "Engine/World.h"

void URunEventLog::Publish(UWorld* World, const FString& Speaker, const FString& Text, const FName EventId,
	const ERunEventKind Kind)
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
	Entry.Kind = Kind;
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
