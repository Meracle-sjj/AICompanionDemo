/**
 * [INPUT]: CoreMinimal/UENUM；九种 Boss 遗物的稳定编号与名称契约。
 * [OUTPUT]: EBossRelicId(0..8)、RelicIdName/RelicDisplayName/RelicBossIndex；运行时、奖励 UI、图标共享 ID。
 * [POS]: Relics/ 的数据契约；不依赖 Character、WaveDirector 或界面，编号只追加不重排。
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */
#pragma once

#include "CoreMinimal.h"
#include "BossRelicTypes.generated.h"

UENUM(BlueprintType)
enum class EBossRelicId : uint8
{
	BloodLink = 0,
	SpiritFrenzy = 1,
	MovingFireball = 2,
	StoneSpikes = 3,
	SeismicStrike = 4,
	StoneCarapace = 5,
	VolcanicRounds = 6,
	FlameWard = 7,
	BlinkPyre = 8,
	Count = 9 UMETA(Hidden)
};

inline bool IsValidRelicId(EBossRelicId Id)
{
	return static_cast<uint8>(Id) < static_cast<uint8>(EBossRelicId::Count);
}

inline FName RelicIdName(EBossRelicId Id)
{
	static const FName Names[] = { TEXT("BloodLink"), TEXT("SpiritFrenzy"), TEXT("MovingFireball"),
		TEXT("StoneSpikes"), TEXT("SeismicStrike"), TEXT("StoneCarapace"), TEXT("VolcanicRounds"),
		TEXT("FlameWard"), TEXT("BlinkPyre") };
	return IsValidRelicId(Id) ? Names[static_cast<uint8>(Id)] : NAME_None;
}

inline FString RelicDisplayName(EBossRelicId Id)
{
	static const TCHAR* Names[] = { TEXT("血誓共鸣"), TEXT("不灭执念"), TEXT("逐焰灵核"),
		TEXT("裂地獠牙"), TEXT("撼岳重击"), TEXT("磐岩逆甲"), TEXT("熔核爆弹"),
		TEXT("焚天护环"), TEXT("踏烬之径") };
	return IsValidRelicId(Id) ? Names[static_cast<uint8>(Id)] : TEXT("");
}

inline int32 RelicBossIndex(EBossRelicId Id)
{
	return IsValidRelicId(Id) ? static_cast<uint8>(Id) / 3 + 1 : 0;
}
