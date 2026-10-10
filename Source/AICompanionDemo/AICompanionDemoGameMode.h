// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * [INPUT]: 依赖 Engine 的 AGameModeBase 生命周期与 FindPlayerStart 出生点查询、SpawnActor 生成链路，依赖 AI/CompanionCharacter 与 AI/EnemyGrunt 的可生成实体类型、AI/WaveDirector（M3.75 玩法节奏中枢的装配目标）
 * [OUTPUT]: 开局装配与开始/死亡输入流转；蠕虫死亡生成归途传送门，奖励领取完且近门按E；Ending暂停世界、真实时钟渐入，返回主菜单重载本局
 * [POS]: Source/AICompanionDemo 的组合根（D6：代码生成取代手工摆放）——"谁入场"在此装配，"何时何地"已随 M3.75 迁入 WaveDirector，"入场后做什么"在 CompanionAIController：三层职责分离
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AICompanionDemoGameMode.generated.h"

class AWaveDirector;
class ATreasureChest;
class UUserWidget;
class UCompanionHud;
class UStartMenu;
class UDeathScreen;
class UDamageFlash;
class AEndingPortal;
class UEndingScreen;

/**
 * 简单第三人称 GameMode：M1 队友入场，M2 敌阵入场，M3.75 波次导演接管敌人生成。
 * 全部实体由代码确定性生成——摆放即正确的范式（D6）。
 */
UCLASS(abstract)
class AAICompanionDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	AAICompanionDemoGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 波次导演查询（组合根职责）：XpPickup 收集经验时经此上报——掉落方与收集方互不认识 */
	AWaveDirector* GetWaveDirector() const { return WaveDirector; }

	// ---------- 游戏流（09-21 游戏感三件套：菜单/HUD/死亡屏的装配与流转中枢） ----------

	/** 开始一局（StartMenu [开始游戏] 回调）：世界解冻 + 菜单退场 */
	void StartRun();

	/** 玩家死亡入口（Character OnDeath 调用）：死亡屏弹入（世界定格延后——布娃娃需要倒地窗） */
	void ShowDeathScreen();

	/** 世界定格（DeathScreen 倒地窗 1.5s 后经真实时钟回调）：怪与计时此刻才停 */
	void FreezeWorld();

	/** 玩家受击入口（Character OnDamaged 调用，09-22 G4）：闪红 UI 脉冲（镜头震动在角色侧自持；方向角已删——用户裁决方向不重要） */
	void NotifyPlayerHurt();

	/** 蠕虫死亡：先保留遗物队列，归途传送门只生成一次。 */
	void NotifyWurmDefeated(const FVector& DefeatLocation);
	bool CanUseEndingPortal(const AEndingPortal* Portal) const;
	bool TryEnterEndingPortal(AEndingPortal* Portal);
	/** 门附近的E优先保留给归途交互；Character据此跳过宝箱/锁箱/祭坛分支。 */
	UFUNCTION(BlueprintPure, Category = "Ending")
	bool HasNearbyEndingPortal() const;
	void ReturnToMainMenu();

	UFUNCTION(BlueprintPure, Category = "Ending")
	bool IsEndingShown() const { return bEndingShown; }

	/** 编辑器专项回读；交互桥仍调用正式传送门的距离/奖励校验。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Ending")
	bool DebugTryEnterEndingPortal();
	UFUNCTION(BlueprintPure, Category = "Debug|Ending")
	FString DebugEndingState() const;

	/** 队友类型：默认回退裸 C++ 类（有行为无外观）；编辑器里应设为 BP_CompanionCharacter（装配 Manny 外观） */
	UPROPERTY(EditAnywhere, Category = "Companion")
	TSubclassOf<class ACompanionCharacter> CompanionClass;

	/** 队友生成点相对玩家出生点的偏移（厘米）——避开玩家胶囊，Z 抬高让重力自然落定 */
	UPROPERTY(EditAnywhere, Category = "Companion")
	FVector CompanionSpawnOffset = FVector(150.f, 0.f, 100.f);

	/** 敌人类型：M3.5 起默认假人 AEnemyGrunt（BP 子类 BP_EnemyGrunt 装配外观与攻击/死亡动画）；指回 AEnemyTarget 即回归球体木桩（安全网）。波次导演与静态阵共用此配置 */
	UPROPERTY(EditAnywhere, Category = "Enemy")
	TSubclassOf<AActor> EnemyClass;

	/** 敌阵生成点：每个偏移生成一只猎手敌人（M3 默认梯形阵：两侧 10m + 前方 6m——与玩家距离拉开梯度，威胁分有区分度，多候选评分才有戏可看）。仅 bUseWaveDirector=false 时生效 */
	UPROPERTY(EditAnywhere, Category = "Enemy")
	TArray<FVector> EnemySpawnOffsets = { FVector(0.f, 1000.f, 100.f), FVector(0.f, -1000.f, 100.f), FVector(600.f, 0.f, 100.f) };

	// ---------- M3.75 玩法节奏 ----------

	/** 波次开关：true=WaveDirector 接管敌人生成（割草皮玩法：波次/变体/经验掉落）；false=开局静态三敌阵（M2 老玩法，回归安全网） */
	UPROPERTY(EditAnywhere, Category = "Wave")
	bool bUseWaveDirector = true;

	/** 波次导演类：默认裸 C++ 类；波次表/刷怪环/同屏上限/经验曲线在导演身上配置 */
	UPROPERTY(EditAnywhere, Category = "Wave")
	TSubclassOf<AWaveDirector> WaveDirectorClass;

	// ---------- M3.75 E3 宝箱经济 ----------

	/** 宝箱摆点（世界坐标，E3；09-25 加量 10→16，用户反馈"宝箱偏少钱花不出去"）：
	 *  内环 10 箱（百米圈密集布点）+ 外环 6 箱（探索奖励，最远 185m，山环 207m 内） */
	UPROPERTY(EditAnywhere, Category = "Chest")
	TArray<FVector> ChestSpawnLocations = {
		FVector(-3000.f, 3000.f, 40.f),
		FVector(3000.f, -3000.f, 40.f),
		FVector(-3500.f, -3000.f, 40.f),
		FVector(3500.f, 3500.f, 40.f),
		FVector(-15000.f, 15000.f, 40.f),
		FVector(15000.f, 15000.f, 40.f),
		FVector(-15000.f, -8000.f, 40.f),
		FVector(16000.f, -16000.f, 40.f),
		FVector(8000.f, 18000.f, 40.f),
		FVector(-9000.f, -17000.f, 40.f),
		FVector(0.f, -8000.f, 40.f),
		FVector(0.f, 8000.f, 40.f),
		FVector(-8000.f, 0.f, 40.f),
		FVector(8000.f, 0.f, 40.f),
		FVector(-6000.f, -6000.f, 40.f),
		FVector(6000.f, 6000.f, 40.f)
	};

	// ---------- 紧急呼叫信标（09-25 深度扩展 A） ----------

	/** 信标摆点：四象限内侧各一座（内环 60-90m 圈）——探索顺路可达，跑图期的"保险丝"节点 */
	UPROPERTY(EditAnywhere, Category = "Beacon")
	TArray<FVector> BeaconSpawnLocations = {
		FVector(-7000.f, 7000.f, 40.f),
		FVector(7000.f, -7000.f, 40.f),
		FVector(8000.f, 9000.f, 40.f),
		FVector(-9000.f, -9000.f, 40.f)
	};

	// ---------- 深度扩展 B/C：难度祭坛 + 钥匙锁箱寻宝（09-26） ----------

	/** 难度祭坛摆点：对角两座（主动加压的自选梯度） */
	UPROPERTY(EditAnywhere, Category = "Altar")
	TArray<FVector> AltarSpawnLocations = {
		FVector(-11000.f, 11000.f, 40.f),
		FVector(12000.f, -12000.f, 40.f)
	};

	/** 钥匙摆点：两把（东侧+西侧）——跨图寻找的探索钩子 */
	UPROPERTY(EditAnywhere, Category = "Treasure")
	TArray<FVector> KeySpawnLocations = {
		FVector(13000.f, 3000.f, 40.f),
		FVector(-13000.f, -3000.f, 40.f)
	};

	/** 锁箱摆点：两座（与钥匙分居——找到钥匙后还要再跑一趟，探索回路两点一线） */
	UPROPERTY(EditAnywhere, Category = "Treasure")
	TArray<FVector> LockedChestSpawnLocations = {
		FVector(-12000.f, 9000.f, 40.f),
		FVector(13000.f, -9000.f, 40.f)
	};

	// ---------- 测试模式（09-30 用户立项：开始页入口——跳过正常节奏，调试面板全手动，验收提速专用） ----------

	/** 测试局开演（StartMenu [测试模式] 回调）：StartRun 全套 + 自动刷怪/祭坛全停 + 调试面板常驻 + Lv.10 */
	void StartTestRun();

	/** 调试转发组（DebugPanel 按钮回调的中转——实际执行在 WaveDirector/Character） */
	void DebugSpawnBoss(int32 Index);
	void DebugSpawnEnemies(int32 Count);
	void DebugClearEnemies();
	void DebugApplyUpgradeByIdx(int32 UpgradeIndex);
	void DebugAddGold(int32 Amount);
	void DebugAddLevel();
	void DebugToggleGod();
	void DebugLogBossSummoner();
	FString DebugCardName(int32 UpgradeIndex) const;

private:
	/** 调试面板挂载（测试模式常驻右侧） */
	void ShowDebugPanel();

	/** 标题幕装配：菜单挂载 + 世界冻结（BeginPlay 末尾调用；装配失败则不冻结——宁可没有幕布不能永远黑着） */
	void ShowStartMenu();

	/** 全屏 UI 装配兜底（09-24）：AddToViewport + 视口级尺寸/对齐钉死——UMG 根塌缩前科的最终解 */
	void AttachFullscreenUI(UUserWidget* Widget, int32 ZOrder);

	/** 导演实例（BeginPlay 生成）：组合根持有的唯一中枢单例 */
	UPROPERTY(Transient)
	TObjectPtr<AWaveDirector> WaveDirector;

	// ---------- UI 三件套（09-21 G1/G2/G3）：装配于此，流转也在此（纯代码 UMG，零蓝图资产） ----------
	UPROPERTY(Transient)
	TObjectPtr<UCompanionHud> Hud;

	UPROPERTY(Transient)
	TObjectPtr<UStartMenu> StartMenu;

	UPROPERTY(Transient)
	TObjectPtr<UDeathScreen> DeathScreen;

	UPROPERTY(Transient)
	TObjectPtr<UDamageFlash> DamageFlash;

	UPROPERTY(Transient)
	TObjectPtr<AEndingPortal> EndingPortal;
	UPROPERTY(Transient)
	TObjectPtr<UEndingScreen> EndingScreen;
	bool bRunStarted = false;
	bool bWurmDefeated = false;
	bool bEndingShown = false;

	// ---------- 测试模式状态（09-30） ----------
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> DebugPanel;

	UPROPERTY(Transient)
	bool bTestMode = false;

	bool bGodMode = false;
};
