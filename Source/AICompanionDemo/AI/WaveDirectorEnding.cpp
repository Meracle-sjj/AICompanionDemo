/**
 * [INPUT]: WaveDirector的节奏相位/奖励队列/ChoiceGeneration/视口树所有权；GameMode正式Ending提交
 * [OUTPUT]: StopForEnding终止导演Tick/Timer、清空奖励并使旧选择回调失效；先移除Slate树再释放View，不改宿主的终局输入
 * [POS]: AI/WaveDirector的终局边界；暂停世界/存活者无敌/Ending展示由组合根负责
 * [PROTOCOL]: 变更时更新本头部，然后检查模块AGENTS.md
 */
#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "TimerManager.h"
#include "UI/UpgradeCardView.h"

void AWaveDirector::StopForEnding()
{
	Phase = EWavePhase::Finished;
	SetActorTickEnabled(false);
	GetWorldTimerManager().ClearAllTimersForObject(this);
	++ChoiceGeneration;
	PendingChoices.Reset();
	bUpgradePending = false;
	bRelicPending = false;
	RelicChoiceBoss = 0;
	bChestDoublePick = false;
	ChestPicksRemaining = 0;
	ChestTakenSlots.Reset();
	// HideUpgradeOverlay会恢复GameOnly；这里仅收视图，输入由Ending宿主接管。
	if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef());
	}
	UpgradeOverlay.Reset();
	UpgradeCardView = nullptr;
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Ending] 导演已停止，奖励队列与旧选择回调已失效。"));
}
