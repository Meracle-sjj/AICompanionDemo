/**
 * [INPUT]: 原WaveDirector匿名namespace中的跨文件显示key与成长常量。
 * [OUTPUT]: 原值不变的inline constexpr单一定义，避免各局部实现各抄一份参数。
 * [POS]: AI导演实现私有常量；不暴露玩法API，不拥有状态或时钟。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#pragma once

namespace WaveDirectorPrivate
{
	inline constexpr int32 kBannerKey = 5;
	inline constexpr float kPlayerAttackIntervalFactor = 0.85f;
	inline constexpr float kPlayerAttackIntervalFloor = 0.15f;
	inline constexpr float kCompanionMoveFactor = 1.10f;
	inline constexpr int32 kCompanionMoveMaxPicks = 3;
}
