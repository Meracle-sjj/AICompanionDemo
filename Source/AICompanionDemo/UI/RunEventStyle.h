/**
 * [INPUT]: Events/RunEventLog的语义类别，CoreMinimal颜色类型。
 * [OUTPUT]: 字幕与事件记录共用的解锁金、首领紫、击败绿及普通台词青色。
 * [POS]: UI纯展示约定；玩法只提供类别，不在Audio或Actor复制颜色规则。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#pragma once
#include "CoreMinimal.h"
#include "Events/RunEventLog.h"

inline FLinearColor RunEventColor(ERunEventKind Kind)
{
	 switch (Kind)
	 {
	 case ERunEventKind::AttackUnlock: return FLinearColor(1.f, .82f, .30f);
	 case ERunEventKind::BossAppear: return FLinearColor(.84f, .52f, 1.f);
	 case ERunEventKind::BossDefeated: return FLinearColor(.43f, 1.f, .67f);
	 default: return FLinearColor(.72f, .92f, .97f);
	 }
}
