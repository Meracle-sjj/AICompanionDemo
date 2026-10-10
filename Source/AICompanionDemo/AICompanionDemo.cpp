// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 AICompanionDemo.h 的 LogAICompanionDemo 声明、Modules/ModuleManager（主模块注册）
 * [OUTPUT]: IMPLEMENT_PRIMARY_GAME_MODULE 主模块实现 + DEFINE_LOG_CATEGORY(LogAICompanionDemo) 落地；零业务逻辑——组合根职责在 GameMode
 * [POS]: 模块入口实现（引擎加载本模块的起点），与 .h 构成最小编译单元
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "AICompanionDemo.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, AICompanionDemo, "AICompanionDemo" );

DEFINE_LOG_CATEGORY(LogAICompanionDemo)