// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 Engine 的 SpawnActor/FindPlayerStart/World，依赖 AI/CompanionCharacter 与 AI/EnemyGrunt 的类对象、AI/WaveDirector（M3.75 敌人生成路径的接管者）
 * [OUTPUT]: 开局装配、免费初始箱、开始/暂停/设置/死亡屏；归途门协调遗物队列与近门E优先级，Ending暂停世界和导演并清战斗屏幕消息，真实时钟渐入由界面负责；主菜单重载清理本局
 * [POS]: Source/AICompanionDemo 的组合根实现；"谁入场"在此装配，"何时何地"在 WaveDirector，"入场后做什么"在 CompanionAIController——三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "AICompanionDemoGameMode.h"
#include "AICompanionDemoCharacter.h"
#include "AICompanionDemo.h"
#include "AI/CompanionCharacter.h"
#include "AI/DifficultyAltar.h"
#include "AI/EmergencyBeacon.h"
#include "AI/EndingPortal.h"
#include "AI/EnemyGrunt.h"
#include "AI/KeyPickup.h"
#include "AI/LockedChest.h"
#include "AI/TreasureChest.h"
#include "AI/WaveDirector.h"
#include "Audio/SFXLibrary.h"
#include "Health/HealthComponent.h"
#include "Vfx/VfxLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CompanionHud.h"
#include "UI/DamageFlash.h"
#include "UI/DeathScreen.h"
#include "UI/DebugPanel.h"
#include "UI/StartMenu.h"
#include "UI/EndingScreen.h"
#include "UI/PauseMenu.h"
#include "UI/DisplaySettingsMenu.h"
#include "UI/RunEventPanel.h"
#include "Framework/Application/SlateApplication.h"

AAICompanionDemoGameMode::AAICompanionDemoGameMode()
{
	// stub
}

// ---------- UI 装配工具 ----------

void AAICompanionDemoGameMode::AttachFullscreenUI(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget)
	{
		return;
	}
	// 09-25 回退视口级钉死（SetDesiredSizeInViewport/Alignment）：PIE 下视口物理尺寸与 Slate 逻辑坐标
	// DPI 错位，画成"中央矩形"（用户截图实锤：幕布与红框四边内缩一圈）——UserWidget 默认布局本就全屏
	//（CompanionHud 顶部居中/左下血条一直正确），brush 修复后只需 AddToViewport
	Widget->AddToViewport(ZOrder);
}

void AAICompanionDemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const AActor* PlayerStart = FindPlayerStart(nullptr, FString());
	const FVector BaseLocation = PlayerStart ? PlayerStart->GetActorLocation() : FVector::ZeroVector;

	// 碰撞自适应落位为生成范式（D6）：玩家胶囊可能占位，允许引擎微调但必须成功
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// ---------- 队友入场（M1）----------
	// 未配置 BP 时回退裸 C++ 类：无外观但有行为，保证跟随链路总能被验证
	const TSubclassOf<ACompanionCharacter> ResolvedCompanionClass =
		CompanionClass ? CompanionClass : TSubclassOf<ACompanionCharacter>(ACompanionCharacter::StaticClass());

	const FVector CompanionLocation = BaseLocation + CompanionSpawnOffset;
	ACompanionCharacter* Companion = World->SpawnActor<ACompanionCharacter>(
		ResolvedCompanionClass, CompanionLocation, FRotator::ZeroRotator, Params);

	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] GameMode spawned: %s at %s (class=%s)"),
		*GetNameSafe(Companion), *CompanionLocation.ToCompactString(), *GetNameSafe(ResolvedCompanionClass));

	// ---------- 敌人生成：波次导演（M3.75 割草皮）或静态三敌阵（M2 回归安全网）----------
	// EnemyClass 两路共用：为空回退裸假人类（无外观但有行为）——指回 AEnemyTarget 即回归球体木桩
	const TSubclassOf<AActor> ResolvedEnemyClass =
		EnemyClass ? EnemyClass : TSubclassOf<AActor>(AEnemyGrunt::StaticClass());

	if (bUseWaveDirector)
	{
		// 导演装配（D6 范式）：波次表/刷怪环/同屏上限在其身上配置；敌人类在此注入
		// 注：SpawnActor 即触发导演 BeginPlay（开喘息节拍），SetEnemyClass 在其首次刷怪前生效即可
		const TSubclassOf<AWaveDirector> ResolvedDirectorClass =
			WaveDirectorClass ? WaveDirectorClass : TSubclassOf<AWaveDirector>(AWaveDirector::StaticClass());
		WaveDirector = World->SpawnActor<AWaveDirector>(ResolvedDirectorClass, BaseLocation, FRotator::ZeroRotator, Params);
		if (WaveDirector)
		{
			WaveDirector->SetEnemyClass(ResolvedEnemyClass);
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] GameMode spawned director: %s（敌人类 %s）"),
			*GetNameSafe(WaveDirector), *GetNameSafe(ResolvedEnemyClass));
	}
	else
	{
		// 静态三敌阵（M2 老玩法）：每个偏移一个战斗单位——评分器多候选的最小可复现场景
		for (const FVector& Offset : EnemySpawnOffsets)
		{
			const FVector SpawnLocation = BaseLocation + Offset;
			AActor* Enemy = World->SpawnActor<AActor>(ResolvedEnemyClass, SpawnLocation, FRotator::ZeroRotator, Params);
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Enemy] GameMode spawned: %s at %s"),
				*GetNameSafe(Enemy), *SpawnLocation.ToCompactString());
		}
	}

	// ---------- 紧急呼叫信标入场（09-25 深度扩展 A）：四象限内侧各一座，花金召回队友 ----------
	for (const FVector& BeaconLocation : BeaconSpawnLocations)
	{
		if (AEmergencyBeacon* Beacon = World->SpawnActor<AEmergencyBeacon>(
			AEmergencyBeacon::StaticClass(), BeaconLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Beacon] spawned at %s"), *BeaconLocation.ToCompactString());
		}
	}

	// ---------- 深度扩展 B/C 入场（09-26）：难度祭坛 + 钥匙 + 锁箱 ----------
	for (const FVector& AltarLocation : AltarSpawnLocations)
	{
		if (ADifficultyAltar* Altar = World->SpawnActor<ADifficultyAltar>(
			ADifficultyAltar::StaticClass(), AltarLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Altar] spawned at %s"), *AltarLocation.ToCompactString());
		}
	}
	for (const FVector& KeyLocation : KeySpawnLocations)
	{
		if (AKeyPickup* Key = World->SpawnActor<AKeyPickup>(
			AKeyPickup::StaticClass(), KeyLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Key] spawned at %s"), *KeyLocation.ToCompactString());
		}
	}
	for (const FVector& LockedLocation : LockedChestSpawnLocations)
	{
		if (ALockedChest* Locked = World->SpawnActor<ALockedChest>(
			ALockedChest::StaticClass(), LockedLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[LockedChest] spawned at %s"), *LockedLocation.ToCompactString());
		}
	}

	// ---------- 宝箱入场（M3.75 E3：道具交互的代表件）----------
	// 固定点铺在场地四象限的地标旁（石阵/走廊口/双山侧）——玩家打钱后绕路开箱，交互即目的
	for (const FVector& ChestLocation : ChestSpawnLocations)
	{
		if (ATreasureChest* Chest = World->SpawnActor<ATreasureChest>(ATreasureChest::StaticClass(), ChestLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Chest] spawned at %s"), *ChestLocation.ToCompactString());
		}
	}

	// ---------- UI 三件套（09-21 游戏感三件套）：HUD 常驻 + 标题幕冻结开局 ----------
	if (UCompanionHud* HudWidget = CreateWidget<UCompanionHud>(World, UCompanionHud::StaticClass()))
	{
		HudWidget->BindSources(WaveDirector);
		HudWidget->AddToViewport();
		Hud = HudWidget;
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] CompanionHud 已挂载"));
	}

	// 受击红闪层（09-22 G4）：Z 序 5——HUD 之上、菜单与死亡屏之下
	if (UDamageFlash* FlashWidget = CreateWidget<UDamageFlash>(World, UDamageFlash::StaticClass()))
	{
		AttachFullscreenUI(FlashWidget, 5);
		DamageFlash = FlashWidget;
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] DamageFlash 已挂载"));
	}

	if (URunEventPanel* Panel = CreateWidget<URunEventPanel>(World, URunEventPanel::StaticClass()))
	{
		RunEventPanel = Panel;
		Panel->AddToViewport(2);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &AAICompanionDemoGameMode::HandleApplicationActivation);
	}
	ShowStartMenu();
}

void AAICompanionDemoGameMode::ShowStartMenu()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UStartMenu* MenuWidget = CreateWidget<UStartMenu>(World, UStartMenu::StaticClass());
	if (!MenuWidget)
	{
		// 菜单装配失败则不冻结世界（宁可没有幕布也不能永远黑着）
		return;
	}
	MenuWidget->BindHost(this);
	AttachFullscreenUI(MenuWidget, 10);
	StartMenu = MenuWidget;

	// 09-26 鼠标解锁（用户实测"鼠标锁在游戏里无法点开始"）：GameAndUI 默认模式下鼠标被
	// 游戏视口捕获转视角、且光标不可见——菜单期必须切 UI 输入模式 + 显示光标
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly InputMode;
		PC->SetInputMode(InputMode);
	}

	// 标题幕即冻结：不刷怪、不计时、敌人静止——点"开始游戏"才开演
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] StartMenu 已挂载（世界冻结）"));
}

void AAICompanionDemoGameMode::StartRun()
{
	if (bRunStarted || bEndingShown || DeathScreen || bReturningToMenu) { return; }
	bRunStarted = true;
	if (!bTestMode) { SpawnStarterChest(); }
	// 特效预载（09-30 用户实测"加载特效卡顿"）：六系 Vfx 首用加载一次付清——游戏内零卡顿
	UVfxLibrary::PrecacheVfx(GetWorld());

	// 开演：输入模式切回游戏 + 藏光标（菜单期 UIOnly 的逆操作）
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
	}
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	if (StartMenu)
	{
		StartMenu->RemoveFromParent();
		StartMenu = nullptr;
	}
	USFXLibrary::PlayBGM(GetWorld());   // 09-26 BGM：开演即起播（循环常驻，槽空静默）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] 开演（世界解冻）"));
}

// ---------- 测试模式（09-30 调试面板：开始页入口，验收提速专用） ----------

void AAICompanionDemoGameMode::StartTestRun()
{
	if (bRunStarted || bEndingShown || DeathScreen) { return; }
	bTestMode = true; // 先确定入口身份，正常局免费箱不进入测试局。
	StartRun();
	if (WaveDirector)
	{
		WaveDirector->SetTestMode(true);   // 自动刷怪/祭坛调度全停——调试面板手动控制
		WaveDirector->SetLevel(10);        // 测试模式开局 Lv.10（射击解锁+全强化可测）
		WaveDirector->DebugGrantRandomUpgrades(10);   // 随机 10 卡武装（用户点名"测试免手加技能"）
	}

	// 测试模式常驻 UIAndGame（面板可点+游戏可玩）+ 鼠标显示
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}
	ShowDebugPanel();
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Test] 测试模式开演——调试面板已挂载"));
}

void AAICompanionDemoGameMode::ShowDebugPanel()
{
	if (DebugPanel || !GetWorld())
	{
		return;
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (UDebugPanel* Panel = CreateWidget<UDebugPanel>(PC, UDebugPanel::StaticClass()))
		{
			Panel->BindHost(this);
			Panel->AddToViewport(40);
			DebugPanel = Panel;
		}
	}
}

void AAICompanionDemoGameMode::DebugSpawnBoss(int32 Index)
{
	if (WaveDirector)
	{
		WaveDirector->DebugSpawnBoss(Index);
	}
}

void AAICompanionDemoGameMode::DebugSpawnEnemies(int32 Count)
{
	if (WaveDirector)
	{
		WaveDirector->DebugSpawnEnemies(Count);
	}
}

void AAICompanionDemoGameMode::DebugClearEnemies()
{
	if (WaveDirector)
	{
		WaveDirector->DebugClearEnemies();
	}
}

void AAICompanionDemoGameMode::DebugApplyUpgradeByIdx(int32 UpgradeIndex)
{
	if (WaveDirector)
	{
		WaveDirector->DebugApplyUpgradeByIdx(UpgradeIndex);
	}
}

void AAICompanionDemoGameMode::DebugAddGold(int32 Amount)
{
	if (WaveDirector)
	{
		WaveDirector->AddGold(Amount);
	}
}

void AAICompanionDemoGameMode::DebugAddLevel()
{
	if (WaveDirector)
	{
		const auto Snap = WaveDirector->GetHudSnapshot();
		WaveDirector->AddXp(Snap.XpToNext);   // 精确补足升级所需（只升一级，不跳级）
	}
}

void AAICompanionDemoGameMode::DebugToggleGod()
{
	bGodMode = !bGodMode;
	if (APawn* P = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		if (UHealthComponent* PlayerHealth = P->FindComponentByClass<UHealthComponent>())
		{
			PlayerHealth->SetInvulnerable(bGodMode ? 9999.f : 0.01f);
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(95, 3.f, FColor::Green,
			FString::Printf(TEXT("[Debug] 无敌：%s"), bGodMode ? TEXT("开") : TEXT("关")));
	}
}

void AAICompanionDemoGameMode::DebugLogBossSummoner()
{
	if (WaveDirector)
	{
		const auto Snap = WaveDirector->GetHudSnapshot();
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Debug] 召唤师诊断：Level=%d Gold=%d Boss击杀=%d"),
			Snap.Level, Snap.GoldCount, Snap.BossKilledCount);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(94, 5.f, FColor::Yellow,
				FString::Printf(TEXT("[诊断] Lv=%d 金=%d Boss击杀=%d"), Snap.Level, Snap.GoldCount, Snap.BossKilledCount));
		}
	}
}

void AAICompanionDemoGameMode::ShowDeathScreen()
{
	if (DeathScreen || bEndingShown)
	{
		return;   // 幕已落（重复 OnDeath 防御）
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UDeathScreen* Screen = CreateWidget<UDeathScreen>(World, UDeathScreen::StaticClass());
	if (!Screen)
	{
		return;
	}

	// 结算统计从导演快照装配（导演缺席则保持零值——安全网模式也有落幕）
	float Survival = 0.f;
	int32 Kills = 0, Level = 1;
	if (WaveDirector)
	{
		const AWaveDirector::FEndlessHudSnapshot Snap = WaveDirector->GetHudSnapshot();
		Survival = Snap.SurvivalSeconds;
		Kills = Snap.KillCount;
		Level = Snap.Level;
	}
	// 终局接管高层输入；清除暂停/设置/测试屏，死亡幕始终在最上层。
	if (PauseMenu) { PauseMenu->RemoveFromParent(); PauseMenu = nullptr; }
	if (DisplaySettingsMenu) { DisplaySettingsMenu->RemoveFromParent(); DisplaySettingsMenu = nullptr; }
	if (DebugPanel) { DebugPanel->RemoveFromParent(); DebugPanel = nullptr; }
	if (bGameplayPaused) { UGameplayStatics::SetGamePaused(this, false); }
	bGameplayPaused = false;
	Screen->Setup(Survival, Kills, Level);
	Screen->BindHost(this);
	AttachFullscreenUI(Screen, 220);
	DeathScreen = Screen;
	if (EndingPortal) { EndingPortal->Destroy(); EndingPortal = nullptr; }

	// 死亡屏同为 UI 输入模式（重开按钮需要光标可点——与开始菜单同款修法）
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly InputMode;
		PC->SetInputMode(InputMode);
	}

	// 世界定格已延后至死亡屏倒地窗（1.5s）——布娃娃需要真实世界时间倒下
	//（首版立即 dilation=0 把物理定格在站姿——用户截图"僵直站立"案）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] DeathScreen 已挂载（1.5s 后定格）存活 %.1fs 击杀 %d Lv.%d"), Survival, Kills, Level);
}

void AAICompanionDemoGameMode::FreezeWorld()
{
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[UI] 世界定格（倒地窗结束）"));
}

void AAICompanionDemoGameMode::NotifyPlayerHurt()
{
	if (DamageFlash && !bEndingShown)
	{
		DamageFlash->Pulse();
	}
}

void AAICompanionDemoGameMode::NotifyWurmDefeated(const FVector& DefeatLocation)
{
	if (bWurmDefeated || bEndingShown || DeathScreen || !bRunStarted || !GetWorld()) { return; }
	bWurmDefeated = true;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	FRotator Rotation = FRotator::ZeroRotator;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Rotation.Yaw = (Player->GetActorLocation() - DefeatLocation).Rotation().Yaw;
	}
	EndingPortal = GetWorld()->SpawnActor<AEndingPortal>(AEndingPortal::StaticClass(), DefeatLocation, Rotation, Params);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Ending] 蠕虫击倒，归途门=%s；奖励领取完成后走近点击。"), *GetNameSafe(EndingPortal));
}

bool AAICompanionDemoGameMode::CanUseEndingPortal(const AEndingPortal* Portal) const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* Health = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	return IsValid(Portal) && Portal == EndingPortal && bRunStarted && bWurmDefeated && !bEndingShown
		&& !DeathScreen && !bGameplayPaused && !DisplaySettingsMenu && Health && !Health->IsDead() && (!WaveDirector || !WaveDirector->HasPendingRewardChoices());
}

bool AAICompanionDemoGameMode::HasNearbyEndingPortal() const
{
	// 即使奖励尚未领完也保留E优先级，防同键触发附近宝箱又排入新选择队列。
	return bRunStarted && bWurmDefeated && !bEndingShown && !DeathScreen
		&& IsValid(EndingPortal) && EndingPortal->IsPlayerNear();
}

bool AAICompanionDemoGameMode::TryEnterEndingPortal(AEndingPortal* Portal)
{
	if (!CanUseEndingPortal(Portal) || !Portal->IsPlayerNear()) { return false; }
	UEndingScreen* Screen = CreateWidget<UEndingScreen>(GetWorld(), UEndingScreen::StaticClass());
	if (!Screen) { return false; }
	const auto Snap = WaveDirector ? WaveDirector->GetHudSnapshot() : AWaveDirector::FEndlessHudSnapshot();
	Screen->Setup(this, Snap.SurvivalSeconds, Snap.KillCount, Snap.Level);
	bEndingShown = true;
	EndingScreen = Screen;
	if (WaveDirector) { WaveDirector->StopForEnding(); }
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Hero->ResetWurmEncounterCamera();
	}
	// GamePaused真正停止世界Timer/敌人火力；仅dilation=0会被引擎钳成0.0001。
	// 先保护同帧已进入结算的伤害回调，再暂停，防结束页背后玩家死亡。
	for (AActor* Ally : { static_cast<AActor*>(UGameplayStatics::GetPlayerPawn(this, 0)),
		UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()) })
	{
		if (Ally)
		{
			if (UHealthComponent* Health = Ally->FindComponentByClass<UHealthComponent>()) { Health->SetInvulnerable(86400.f); }
			Ally->SetActorTickEnabled(false);
		}
	}
	const bool bPaused = UGameplayStatics::SetGamePaused(this, true);
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	if (Hud) { Hud->RemoveFromParent(); }
	if (DebugPanel) { DebugPanel->RemoveFromParent(); }
	if (DamageFlash) { DamageFlash->RemoveFromParent(); }
	if (GEngine) { GEngine->ClearOnScreenDebugMessages(); }
	// Destroy触发门EndPlay先移除提示树，Ending装配后只保留一个全屏输入宿主。
	Portal->Destroy();
	EndingPortal = nullptr;
	AttachFullscreenUI(Screen, 120);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Screen->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Ending] 真实传送门交互完成；世界暂停=%d 存活=%.1fs 击杀=%d Lv=%d"), bPaused, Snap.SurvivalSeconds, Snap.KillCount, Snap.Level);
	return true;
}

void AAICompanionDemoGameMode::ReturnToMainMenu()
{
	if (bReturningToMenu || !GetWorld()) { return; }
	bReturningToMenu = true;
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
	}
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

bool AAICompanionDemoGameMode::DebugTryEnterEndingPortal()
{
#if WITH_EDITOR
	return EndingPortal && EndingPortal->TryInteract();
#else
	return false;
#endif
}

FString AAICompanionDemoGameMode::DebugEndingState() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("{\"runStarted\":%s,\"wurmDefeated\":%s,\"endingShown\":%s,\"portal\":%s,\"near\":%s,\"canEnter\":%s,\"rewardPending\":%s,\"paused\":%s,\"artLoaded\":%s,\"inViewport\":%s}"),
		bRunStarted ? TEXT("true") : TEXT("false"), bWurmDefeated ? TEXT("true") : TEXT("false"),
		bEndingShown ? TEXT("true") : TEXT("false"), IsValid(EndingPortal) ? TEXT("true") : TEXT("false"),
		EndingPortal && EndingPortal->IsPlayerNear() ? TEXT("true") : TEXT("false"),
		CanUseEndingPortal(EndingPortal) ? TEXT("true") : TEXT("false"),
		WaveDirector && WaveDirector->HasPendingRewardChoices() ? TEXT("true") : TEXT("false"),
		UGameplayStatics::IsGamePaused(this) ? TEXT("true") : TEXT("false"),
		EndingScreen && EndingScreen->HasArtwork() ? TEXT("true") : TEXT("false"),
		EndingScreen && EndingScreen->IsInViewport() ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}

void AAICompanionDemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EndingPortal) { EndingPortal->Destroy(); EndingPortal = nullptr; }
	if (EndingScreen) { EndingScreen->RemoveFromParent(); EndingScreen = nullptr; }
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this);
	}
	if (PauseMenu) { PauseMenu->RemoveFromParent(); PauseMenu = nullptr; }
	if (DisplaySettingsMenu) { DisplaySettingsMenu->RemoveFromParent(); DisplaySettingsMenu = nullptr; }
	if (RunEventPanel) { RunEventPanel->RemoveFromParent(); RunEventPanel = nullptr; }
	Super::EndPlay(EndPlayReason);
}

// 1.1：暂停归组合根所有，不修改奖励选择的GlobalTimeDilation。
void AAICompanionDemoGameMode::HandleApplicationActivation(bool bIsActive)
{
	if (!bIsActive) { PauseForFocusLoss(); }
}

void AAICompanionDemoGameMode::PauseForFocusLoss()
{
	if (bRunStarted && !bGameplayPaused && !DisplaySettingsMenu) { TogglePauseMenu(); }
}

void AAICompanionDemoGameMode::TogglePauseMenu()
{
	if (DisplaySettingsMenu) { CloseDisplaySettings(); return; }
	if (bGameplayPaused) { ResumeRunFromPause(); return; }
	if (!bRunStarted || bEndingShown || DeathScreen || bReturningToMenu || !GetWorld()) { return; }
	UPauseMenu* Menu = CreateWidget<UPauseMenu>(GetWorld(), UPauseMenu::StaticClass());
	if (!Menu) { return; }
	if (!UGameplayStatics::SetGamePaused(this, true)) { return; }
	bGameplayPaused = true;
	PauseMenu = Menu;
	Menu->BindHost(this);
	AttachFullscreenUI(Menu, 200);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Menu->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Pause] paused; pendingReward=%d"), WaveDirector && WaveDirector->HasPendingRewardChoices());
}

void AAICompanionDemoGameMode::RestoreRunInput()
{
	if (bReturningToMenu) { return; }
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		UUserWidget* Exclusive = nullptr;
		if (bEndingShown) { Exclusive = EndingScreen; }
		else if (DeathScreen) { Exclusive = DeathScreen; }
		else if (DisplaySettingsMenu) { Exclusive = DisplaySettingsMenu; }
		else if (bGameplayPaused) { Exclusive = PauseMenu; }
		else if (!bRunStarted) { Exclusive = StartMenu; }
		if (Exclusive)
		{
			PC->bShowMouseCursor = true;
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(Exclusive->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode);
			return;
		}
		const bool bNeedsUI = bTestMode || (WaveDirector && WaveDirector->HasPendingRewardChoices());
		PC->bShowMouseCursor = bNeedsUI;
		if (bNeedsUI)
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		else { PC->SetInputMode(FInputModeGameOnly()); }
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
	}
}

void AAICompanionDemoGameMode::ResumeRunFromPause()
{
	if (!bGameplayPaused || bEndingShown || DeathScreen || bReturningToMenu) { return; }
	if (DisplaySettingsMenu) { DisplaySettingsMenu->RemoveFromParent(); DisplaySettingsMenu = nullptr; }
	if (PauseMenu) { PauseMenu->RemoveFromParent(); PauseMenu = nullptr; }
	UGameplayStatics::SetGamePaused(this, false);
	bGameplayPaused = false;
	RestoreRunInput();
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Pause] resumed; dilation=%.5f pendingReward=%d"), UGameplayStatics::GetGlobalTimeDilation(this), WaveDirector && WaveDirector->HasPendingRewardChoices());
}

void AAICompanionDemoGameMode::ShowDisplaySettings()
{
	if (DisplaySettingsMenu || bEndingShown || DeathScreen || bReturningToMenu || !GetWorld()) { return; }
	if (bRunStarted && !bGameplayPaused) { TogglePauseMenu(); }
	UDisplaySettingsMenu* Menu = CreateWidget<UDisplaySettingsMenu>(GetWorld(), UDisplaySettingsMenu::StaticClass());
	if (!Menu) { return; }
	DisplaySettingsMenu = Menu;
	Menu->BindHost(this);
	AttachFullscreenUI(Menu, 210);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Menu->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
}

void AAICompanionDemoGameMode::CloseDisplaySettings()
{
	if (!DisplaySettingsMenu) { return; }
	DisplaySettingsMenu->RemoveFromParent();
	DisplaySettingsMenu = nullptr;
	UUserWidget* Host = bGameplayPaused ? static_cast<UUserWidget*>(PauseMenu) : static_cast<UUserWidget*>(StartMenu);
	if (Host)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(Host->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode);
		}
	}
	else { RestoreRunInput(); }
}

void AAICompanionDemoGameMode::SpawnStarterChest()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player || !WaveDirector || !GetWorld()) { return; }
	const FVector Target = Player->GetActorLocation() + FVector(210.f, 170.f, 0.f);
	FHitResult Ground;
	FCollisionQueryParams Trace(SCENE_QUERY_STAT(StarterChestGround), false, Player);
	const FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
	if (!GetWorld()->LineTraceSingleByObjectType(Ground, Target + FVector(0.f,0.f,600.f), Target-FVector(0.f,0.f,3000.f), StaticObjects, Trace)) { return; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (ATreasureChest* Chest = GetWorld()->SpawnActor<ATreasureChest>(ATreasureChest::StaticClass(), Ground.ImpactPoint+FVector(0.f,0.f,40.f), FRotator::ZeroRotator, Params))
	{
		Chest->ConfigureStarterChest();
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[StarterChest] spawned at %s; free three-choice reward"), *Chest->GetActorLocation().ToCompactString());
	}
}

FString AAICompanionDemoGameMode::DebugRunState() const
{
	return FString::Printf(TEXT("{\"runStarted\":%s,\"testMode\":%s,\"paused\":%s,\"pauseMenu\":%s,\"settings\":%s,\"startMenu\":%s,\"rewardPending\":%s,\"dilation\":%.6f}"),
		bRunStarted?TEXT("true"):TEXT("false"), bTestMode?TEXT("true"):TEXT("false"), UGameplayStatics::IsGamePaused(this)?TEXT("true"):TEXT("false"),
		PauseMenu && PauseMenu->IsInViewport()?TEXT("true"):TEXT("false"), DisplaySettingsMenu && DisplaySettingsMenu->IsInViewport()?TEXT("true"):TEXT("false"),
		StartMenu && StartMenu->IsInViewport()?TEXT("true"):TEXT("false"), WaveDirector && WaveDirector->HasPendingRewardChoices()?TEXT("true"):TEXT("false"), UGameplayStatics::GetGlobalTimeDilation(this));
}
