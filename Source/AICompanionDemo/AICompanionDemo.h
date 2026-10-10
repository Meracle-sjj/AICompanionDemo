// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 CoreMinimal（纯声明头，无引擎类型依赖）
 * [OUTPUT]: 对外提供主日志类别 LogAICompanionDemo（全模块通用日志出口；业务日志锚点 [Companion]/[Combat]/[Wave] 等的清单见任务仓库 STATUS.md 日志锚点节）
 * [POS]: 模块入口声明头，最小编译单元；模块实现与日志类别落地在 AICompanionDemo.cpp
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogAICompanionDemo, Log, All);