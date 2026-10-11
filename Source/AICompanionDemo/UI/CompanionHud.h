/**
 * [INPUT]: UMG/Slate、UpgradeCardPresentation路径与稀有度；导演快照、Health/Character只读口、CompanionAIController真实模式与增益参数、世界活Boss身份和血量
 * [OUTPUT]: 常驻统计/强化/紫魂倒计时、血量/XP四槽、小图；Q当前护卫/歼灭增益；中央上方Boss真血条，0.15s独立刷新、死亡移除
 * [POS]: UI/ 三件套之一（G1 + HUD v2，与 DeathScreen/StartMenu 姊妹）：GameMode 组合根装配；配色语言沿用拾取物家族（金=账本、青=成长、红=血）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "UpgradeCardPresentation.h"
#include "CompanionHud.generated.h"

class AWaveDirector;
class UHealthComponent;
class AAICompanionDemoCharacter;
class ACompanionAIController;
class ABossBase;
class SOverlay;
class SVerticalBox;
class STextBlock;
class SProgressBar;
class SBox;
class SMinimapCanvas;
class SUniformGridPanel;
class SHudSkillIcon;
class UTexture2D;

UCLASS()
class AICOMPANIONDEMO_API UCompanionHud : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 数据源注入（GameMode 组合根装配）：导演快照；玩家血量/角色首帧惰性解析（消 BeginPlay 时序依赖） */
	void BindSources(AWaveDirector* InDirector);

	/** 编辑器只读验收桥；非编辑器返回空串，不改变技能/账本/刷新节拍。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	FString DebugSkillIconState() const;
	/** 只读小图验收：地形采样、同场揭示、活Boss/未激活祭坛的实际展示快照。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UI")
	FString DebugMinimapState() const;
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugCompanionModeState() const;
	/** 只读真实屏幕血条树/几何/来源快照；不改变Boss、健康或显示节拍。 */
	UFUNCTION(BlueprintPure, Category = "Debug|UI")
	FString DebugBossHudState() const;

protected:
	/** 纯代码构建 Slate 树（零蓝图资产哲学：不建 WidgetBlueprint） */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	/** 操作槽：图标、短键位、状态/秒数和CD条。 */
	struct FSkillSlotWidgets
	{
		TSharedPtr<SHudSkillIcon> Icon;
		TSharedPtr<STextBlock> KeyText;
		TSharedPtr<STextBlock> StateText;
		TSharedPtr<SProgressBar> CooldownBar;
		bool bLastUnlocked = false;
		float LastRemaining = 0.f;
		float LastTotal = 0.f;
		float AssignedCooldownFraction = 0.f;
	};

	/** 单槽三态刷新：锁定（灰+解锁提示）→ 冷却中（压暗+条排空+秒数）→ 就绪（键位亮+就绪文案） */
	void UpdateSkillSlot(FSkillSlotWidgets& SkillSlot, bool bUnlocked, float Remain, float Total,
		EUpgradeCardRarity Rarity, const FString& LockHint, const FString& ReadyHint);

	/** 同一升级只装载一次；纹理GC保活与稳定brush地址分别由两份缓存承担。 */
	TSharedPtr<FSlateBrush> ResolveIconBrush(FName UpgradeId);

	/** UI仅缓存快照展示字段，不按中文名反查升级或持有玩法账本。 */
	struct FHudCardDisplay
	{
		FName UpgradeId;
		FString Name;
		FString Description;
		int32 Count = 1;
		EUpgradeCardRarity Rarity = EUpgradeCardRarity::Common;
		bool operator==(const FHudCardDisplay& Other) const
		{
			return UpgradeId == Other.UpgradeId && Name == Other.Name &&
				Count == Other.Count && Rarity == Other.Rarity && Description == Other.Description;
		}
	};

	/** 轮询刷新：血条/XP/顶部行 + 技能栏三态 + 强化列表 diff 重建（变化才动） */
	void RefreshValues();
	void RefreshCompanionMode();
	TSharedRef<SWidget> BuildBossHudLane();
	void RefreshBossHud();
	void UpdateBossHudLayout(const FGeometry& Geometry);
	/** 小图世界查询与分帧地形采样；绘制不运行射线，地图资产不修改。 */
	void RefreshMinimap();

	/** 顶部存活大字（U3 主视觉）与统计行 */
	TSharedPtr<STextBlock> TimeText;
	TSharedPtr<STextBlock> StatText;
	/** 模式只展示控制器真源，不在HUD计算或施加战斗效果。 */
	TSharedPtr<STextBlock> ModeTitleText;
	TSharedPtr<STextBlock> ModeDescriptionText;
	TWeakObjectPtr<ACompanionAIController> ModeSource;
	/** 常驻HUD装载三Boss屏幕血条，弱Actor不延长死亡/GC生命周期。 */
	struct FBossHudRow
	{
		TWeakObjectPtr<ABossBase> Boss;
		TSharedPtr<STextBlock> Label;
		TSharedPtr<SProgressBar> Bar;
		float AssignedPercent = 0.f;
	};
	TSharedPtr<SBox> BossLanePadding;
	TSharedPtr<SBox> BossLaneBox;
	TSharedPtr<SVerticalBox> BossRowsBox;
	TArray<FBossHudRow> BossRows;
	float BossLaneWidth = 560.f;
	float BossLaneTop = 130.f;

	/** 左侧已获强化图标（6列，快照变化才重建）。 */
	TSharedPtr<SUniformGridPanel> CardIconGrid;
	/** 紫魂只缓存Slate展示与最后快照；计时归组件Clock与队友FrenzyTimer。 */
	TSharedPtr<SHudSkillIcon> FrenzyRelicIcon;
	TSharedPtr<STextBlock> FrenzyRelicCountdownText;
	float LastFrenzyCooldown = 0.f;
	float LastFrenzyTotal = 0.f;
	float LastFrenzyActive = 0.f;

	/** 血条双层错峰（底层 LagBar 白条迟滞 / 顶层即时红条）+ 覆盖数字 */
	TSharedPtr<SProgressBar> HealthBar;
	TSharedPtr<SProgressBar> LagBar;
	TSharedPtr<STextBlock> HealthText;

	/** 经验条与覆盖文字 */
	TSharedPtr<SProgressBar> XpBar;
	TSharedPtr<STextBlock> XpText;

	/** 右下四槽：闪烁Shift/召回C/自动射击AUTO/持钥KEY；没有左键射击误导。 */
	FSkillSlotWidgets BlinkSlot;
	FSkillSlotWidgets RecallSlot;
	FSkillSlotWidgets ShotSlot;
	FSkillSlotWidgets KeySlot;

	/** 右上小地图（09-28：Slate 自绘+战争迷雾，Tab 放大） */
	TSharedPtr<SMinimapCanvas> Minimap;
	TSharedPtr<class SBox> MinimapBox;   /** 尺寸硬限（150/400 切换）——防 SOverlay 槽拉伸白块案 */
	TSharedPtr<SBox> MinimapHint;
	bool bMinimapZoomed = false;
	/** 当前世界只采一次Landscape高度；33²含两端，分帧48条/轮询。MAX_flt为无有效地形首击。 */
	static constexpr int32 MinimapTerrainN = 33;
	TArray<float> MinimapTerrainHeights;
	int32 MinimapTerrainCursor = 0;
	int32 MinimapTerrainHits = 0;
	int32 MinimapTerrainRetryPass = 0;
	bool bMinimapTerrainUploaded = false;

	/** 零资产圆角视觉底座：面板 brush（各槽/血条区共用 SBorder 底——UniquePtr 持有：FSlateRoundedBoxBrush 无默认构造，UHT 生成代码要求值成员可默认构造）+ 圆角条样式 */
	TUniquePtr<FSlateRoundedBoxBrush> PanelBrush;
	FProgressBarStyle RoundedBarStyle;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> IconTextures;
	/** Slate图标复制shared brush；TMap扩容或列表重建不悬空。缓存随HUD存活保留。 */
	TMap<FName, TSharedPtr<FSlateBrush>> IconBrushes;

	/** 数据源（导演注入；玩家血量/角色首帧解析缓存） */
	TWeakObjectPtr<AWaveDirector> Director;
	TWeakObjectPtr<UHealthComponent> PlayerHealth;
	TWeakObjectPtr<AAICompanionDemoCharacter> PlayerChar;

	/** 刷新节拍与状态缓存（列表 diff；血条迟滞层追值） */
	float RefreshCountdown = 0.f;
	TArray<FHudCardDisplay> LastCards;
	bool bCardIconsBuilt = false;
	int32 CardGridBuildCount = 0;
	int32 UnchangedCardSnapshotCount = 0;
	float LagHealth = 0.f;
	bool bLagInit = false;
};
