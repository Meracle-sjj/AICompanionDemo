/**
 * [INPUT]: CompanionHud.h、导演快照、共享图卡、Slate与Health/Character只读口；队友控制器当前模式、世界活Boss血条局部实现
 * [OUTPUT]: 六列图标/紫魂秒数、四操作槽/血量/XP/小图；右下Q模式与中央上方Boss真血条，0.15s轮询不拥有效果账本
 * [POS]: UI/ 三件套实现之一：常驻层（Z 序默认，菜单与死亡屏盖其上）；HUD v2——圆角面板统一视觉语言，配色沿用拾取物家族（金=账本、青=成长、红=血）
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "CompanionHud.h"
#include "HudSkillIcon.h"

#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "AI/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "AI/EnemyGrunt.h"
#include "AI/KeyPickup.h"
#include "AI/LockedChest.h"
#include "AI/TreasureChest.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "MinimapCanvas.h"
#include "Health/HealthComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"


namespace
{
	// HUD 配色（拾取物家族同语言：金=账本/青=成长/红=血/紫=强化）
	const FLinearColor kHudGold(1.f, 0.85f, 0.40f);
	const FLinearColor kHudSoft(0.90f, 0.92f, 0.95f);
	const FLinearColor kHudHealthRed(0.86f, 0.16f, 0.13f);
	const FLinearColor kHudLagGold(1.f, 0.92f, 0.62f);   // 血条迟滞层：淡金——"刚挨了多少"的余像
	const FLinearColor kHudXpAmber(1.f, 0.78f, 0.20f);
	const FLinearColor kHudCardCyan(0.45f, 0.90f, 1.f);
	const FLinearColor kHudDimWhite(1.f, 1.f, 1.f, 0.35f);

	// 字体：引擎默认（Roboto fallback 中文）；标题行大、正文中、条上数字小
	// GetFontStyle 返回的就是 FSlateFontInfo——复制改字号（C2440 实证：无 (FSlateFontInfo, float) 构造重载）
	FSlateFontInfo HudFont(float Size, bool bBold = false)
	{
		FSlateFontInfo Font = FCoreStyle::Get().GetFontStyle(bBold ? TEXT("Bold") : TEXT("Normal"));
		Font.Size = Size;
		return Font;
	}
}

void UCompanionHud::BindSources(AWaveDirector* InDirector)
{
	Director = InDirector;
}

TSharedPtr<FSlateBrush> UCompanionHud::ResolveIconBrush(FName UpgradeId)
{
	if (const TSharedPtr<FSlateBrush>* Existing = IconBrushes.Find(UpgradeId))
	{
		return *Existing;
	}
	const TCHAR* Path = UpgradeCardPresentation::GetCardTextureAssetPath(UpgradeId);
	UTexture2D* Texture = Path ? LoadObject<UTexture2D>(nullptr, Path) : nullptr;
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	if (Texture)
	{
		IconTextures.Add(UpgradeId, Texture);
		Brush->SetResourceObject(Texture);
		Brush->SetImageSize(FVector2D(Texture->GetSizeX(), Texture->GetSizeY()));
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->ImageType = ESlateBrushImageType::FullColor;
		Brush->TintColor = FSlateColor(FLinearColor::White);
	}
	else
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[HudIcons] 升级图未载入：%s；使用原生技能图形。"),
			*UpgradeId.ToString());
	}
	// 缺图也缓存，避免每次轮询/重建重复加载与告警。
	IconBrushes.Add(UpgradeId, Brush);
	return Brush;
}

void UCompanionHud::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TimeText.Reset();
	StatText.Reset();
	ModeTitleText.Reset();
	ModeDescriptionText.Reset();
	BossLanePadding.Reset();
	BossLaneBox.Reset();
	BossRowsBox.Reset();
	BossRows.Reset();
	CardIconGrid.Reset();
	FrenzyRelicIcon.Reset();
	FrenzyRelicCountdownText.Reset();
	HealthBar.Reset();
	LagBar.Reset();
	HealthText.Reset();
	XpBar.Reset();
	XpText.Reset();
	BlinkSlot = FSkillSlotWidgets();
	RecallSlot = FSkillSlotWidgets();
	ShotSlot = FSkillSlotWidgets();
	KeySlot = FSkillSlotWidgets();
	Minimap.Reset();
	MinimapBox.Reset();
	bMinimapZoomed = false;
	bMinimapTerrainUploaded = false;
	bCardIconsBuilt = false;
	// 旧树在SObjectWidget完成ResetWidget前仍可存活，资源/成员样式不在这里清空。
	// UObject GC与后续重建共用同一纹理/heap brush缓存。
}

TSharedRef<SWidget> UCompanionHud::RebuildWidget()
{
	// HUD v2 零资产圆角底座（引擎源码实证：FSlateRoundedBoxBrush 值语义 brush + FProgressBarStyle 链式换图）：
	// 面板=深色半透明圆角+微弱白描边；条=背景深圆角 / 填充白圆角（白底由 FillColorAndOpacity 着色出各条颜色）
	if (!PanelBrush.IsValid())
	{
		PanelBrush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor(0.04f, 0.05f, 0.08f, 0.62f), 8.f,
			FLinearColor(1.f, 1.f, 1.f, 0.10f), 1.f);
	}
	bCardIconsBuilt = false;
	RefreshCountdown = 0.f;
	bMinimapTerrainUploaded = false;
	RoundedBarStyle = FProgressBarStyle::GetDefault();
	RoundedBarStyle.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(0.02f, 0.02f, 0.04f, 0.90f), 4.f));
	RoundedBarStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));

	// 操作提示保留短键位/状态，图标成为主视觉；自动射击不再标LMB。
	auto MakeSlotPanel = [this](FSkillSlotWidgets& InSlot, const FString& KeyLabel,
		EHudSkillGlyph Glyph, FName ArtId, const FString& SkillName) -> TSharedRef<SWidget>
	{
		return SNew(SBorder)
			.BorderImage(PanelBrush.Get())
			.Padding(FMargin(6.f, 7.f, 6.f, 7.f))
			.ToolTipText(FText::FromString(SkillName))
			[
				SNew(SBox)
				.WidthOverride(66.f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SAssignNew(InSlot.Icon, SHudSkillIcon)
						.Size(56.f)
						.Glyph(Glyph)
						.ArtBrush(ArtId.IsNone() ? TSharedPtr<FSlateBrush>() : ResolveIconBrush(ArtId))
						.RingColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.5f))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.f, 3.f, 0.f, 0.f)
					[
						SAssignNew(InSlot.KeyText, STextBlock)
						.Font(HudFont(12.f, true))
						.ColorAndOpacity(FSlateColor(kHudDimWhite))
						.ShadowOffset(FVector2D(1.5f, 1.5f))
						.Text(FText::FromString(KeyLabel))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.f, 3.f, 0.f, 4.f)
					[
						SAssignNew(InSlot.StateText, STextBlock)
						.Font(HudFont(12.f))
						.ColorAndOpacity(FSlateColor(kHudDimWhite))
						.Text(FText::GetEmpty())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(5.f)
						[
							SAssignNew(InSlot.CooldownBar, SProgressBar)
							.Style(&RoundedBarStyle)
							.BarFillType(EProgressBarFillType::LeftToRight)
							.FillColorAndOpacity(FSlateColor(kHudCardCyan))
							.BorderPadding(FVector2D(0.f, 0.f))
							.Percent(1.f)
						]
					]
				]
			];
	};

	return SNew(SOverlay)
		// 各Boss共享固定屏幕锚点，多个活Boss依身份稳定堆叠；高层菜单仍覆盖HUD。
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
		[BuildBossHudLane()]
		// Q模式与被动说明放四操作槽上方，两个百分比来自同一控制器参数。
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 24.f, 152.f)
		[
			SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(12.f, 9.f))
			[
				SNew(SBox).WidthOverride(318.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[SAssignNew(ModeTitleText, STextBlock).Font(HudFont(16.f, true)).ColorAndOpacity(FSlateColor(kHudSoft))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
					[SAssignNew(ModeDescriptionText, STextBlock).Font(HudFont(13.f)).ColorAndOpacity(FSlateColor(kHudSoft))]
				]
			]
		]
		// ---------- 右上：战场缩略图（09-28 小地图，Tab 放大；SBox 硬限尺寸防槽拉伸——白块案根治） ----------
		+ SOverlay::Slot()
		.HAlign(HAlign_Right).VAlign(VAlign_Top)
		.Padding(0.f, 14.f, 14.f, 0.f)
		[
			SAssignNew(MinimapBox, SBox)
			.WidthOverride(150.f)
			.HeightOverride(150.f)
			[
				SAssignNew(Minimap, SMinimapCanvas)
			]
		]
		// 顶层留白容器：五区排布
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)

			// ---------- 顶部：存活主视觉（U3 居中放大）+ 统计行 ----------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				SAssignNew(TimeText, STextBlock)
				.Font(HudFont(44.f, true))
				.ColorAndOpacity(FSlateColor(kHudGold))
				.ShadowOffset(FVector2D(2.5f, 2.5f))
				.Text(FText::FromString(TEXT("0:00")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 4.f, 0.f, 0.f)
			[
				SAssignNew(StatText, STextBlock)
				.Font(HudFont(20.f))
				.ColorAndOpacity(FSlateColor(kHudSoft))
				.ShadowOffset(FVector2D(1.5f, 1.5f))
				.Text(FText::GetEmpty())
			]

			// ---------- 左侧：小强化图标，六列限制横向占用 ----------
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(18.f, 26.f, 0.f, 0.f)
			[
				SAssignNew(CardIconGrid, SUniformGridPanel)
				.SlotPadding(FMargin(3.f))
			]

			// ---------- 左下：圆角面板（血条双层错峰 + XP 条，U2） ----------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(24.f, 0.f, 0.f, 22.f)
			[
				SNew(SBorder)
				.BorderImage(PanelBrush.Get())
				.Padding(FMargin(10.f, 8.f, 10.f, 8.f))
				[
					SNew(SBox)
					.WidthOverride(330.f)
					[
						SNew(SVerticalBox)

						// 血条：SOverlay 三层（迟滞淡金 → 即时红 → 居中数字）
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
							[
								SAssignNew(LagBar, SProgressBar)
								.Style(&RoundedBarStyle)
								.BarFillType(EProgressBarFillType::LeftToRight)
								.FillColorAndOpacity(FSlateColor(kHudLagGold))
								.BorderPadding(FVector2D(0.f, 0.f))
								.Percent(1.f)
							]
							+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
							[
								SAssignNew(HealthBar, SProgressBar)
								.Style(&RoundedBarStyle)
								.BarFillType(EProgressBarFillType::LeftToRight)
								.FillColorAndOpacity(FSlateColor(kHudHealthRed))
								.BorderPadding(FVector2D(0.f, 0.f))
								.Percent(1.f)
							]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SAssignNew(HealthText, STextBlock)
								.Font(HudFont(17.f, true))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.ShadowOffset(FVector2D(1.5f, 1.5f))
								.Text(FText::GetEmpty())
							]
						]

						// XP 条（维持左下——用户裁决）：叠行文字
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SOverlay)
							+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
							[
								SAssignNew(XpBar, SProgressBar)
								.Style(&RoundedBarStyle)
								.BarFillType(EProgressBarFillType::LeftToRight)
								.FillColorAndOpacity(FSlateColor(kHudXpAmber))
								.BorderPadding(FVector2D(0.f, 0.f))
							]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SAssignNew(XpText, STextBlock)
								.Font(HudFont(13.f))
								.ColorAndOpacity(FSlateColor(FLinearColor::Black))
								.Text(FText::GetEmpty())
							]
						]
					]
				]
			]
		]

		// ---------- 右下：四操作图标（与左下血条同底行） ----------
		+ SOverlay::Slot()
		.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 24.f, 22.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				MakeSlotPanel(BlinkSlot, TEXT("SHIFT"), EHudSkillGlyph::Blink, TEXT("Blink"), TEXT("闪烁"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				MakeSlotPanel(RecallSlot, TEXT("C"), EHudSkillGlyph::Recall, NAME_None, TEXT("召回队友"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				MakeSlotPanel(ShotSlot, TEXT("AUTO"), EHudSkillGlyph::AutoFire, NAME_None, TEXT("自动射击"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeSlotPanel(KeySlot, TEXT("KEY"), EHudSkillGlyph::Key, NAME_None, TEXT("持有钥匙"))
			]
		];
}

void UCompanionHud::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateBossHudLayout(MyGeometry);

	// 玩家血量/角色惰性解析（首帧 pawn 才就绪——消 BeginPlay 时序依赖）
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (!PlayerHealth.IsValid())
		{
			PlayerHealth = Pawn->FindComponentByClass<UHealthComponent>();
		}
		if (!PlayerChar.IsValid())
		{
			PlayerChar = Cast<AAICompanionDemoCharacter>(Pawn);
		}
	}

	RefreshCountdown -= InDeltaTime;
	if (RefreshCountdown > 0.f)
	{
		return;
	}
	RefreshCountdown = 0.15f;
	RefreshValues();
}

void UCompanionHud::UpdateSkillSlot(FSkillSlotWidgets& InSlot, bool bUnlocked, float Remain, float Total,
	EUpgradeCardRarity Rarity, const FString& LockHint, const FString& ReadyHint)
{
	if (!InSlot.Icon.IsValid() || !InSlot.KeyText.IsValid() ||
		!InSlot.StateText.IsValid() || !InSlot.CooldownBar.IsValid())
	{
		return;
	}
	InSlot.bLastUnlocked = bUnlocked;
	InSlot.LastRemaining = FMath::Max(0.f, Remain);
	InSlot.LastTotal = FMath::Max(0.f, Total);
	const FLinearColor ReadyColor = UpgradeCardPresentation::GetRarityColor(Rarity);

	// 锁定态：键位与状态全灰，提示解锁途径
	if (!bUnlocked)
	{
		InSlot.Icon->SetAppearance(FLinearColor(0.4f, 0.4f, 0.4f, 0.50f), 0.25f);
		InSlot.KeyText->SetColorAndOpacity(FSlateColor(kHudDimWhite));
		InSlot.StateText->SetColorAndOpacity(FSlateColor(kHudDimWhite));
		InSlot.StateText->SetText(FText::FromString(LockHint));
		InSlot.CooldownBar->SetPercent(0.f);
		InSlot.AssignedCooldownFraction = 0.f;
		return;
	}

	// 冷却中：键位压暗、秒数递减、条按剩余比例排空（呼叫 45s 长冷却下秒数比条更可读——两件都给）
	if (Remain > 0.01f)
	{
		InSlot.Icon->SetAppearance(ReadyColor, 0.40f);
		InSlot.KeyText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.45f)));
		InSlot.StateText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.85f)));
		InSlot.StateText->SetText(FText::FromString(FString::Printf(TEXT("%.1fs"), Remain)));
		InSlot.AssignedCooldownFraction = Total > 0.f ? FMath::Clamp(Remain / Total, 0.f, 1.f) : 0.f;
		InSlot.CooldownBar->SetPercent(InSlot.AssignedCooldownFraction);
		InSlot.CooldownBar->SetFillColorAndOpacity(FSlateColor(ReadyColor));
		return;
	}

	// 就绪：键位亮起、条满（就绪文案可定制——射击是被动强化，显示"自动开火"而非"就绪"）
	InSlot.Icon->SetAppearance(ReadyColor, 1.f);
	InSlot.KeyText->SetColorAndOpacity(FSlateColor(kHudSoft));
	InSlot.StateText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.55f)));
	InSlot.StateText->SetText(FText::FromString(ReadyHint));
	InSlot.CooldownBar->SetPercent(1.f);
	InSlot.CooldownBar->SetFillColorAndOpacity(FSlateColor(ReadyColor));
	InSlot.AssignedCooldownFraction = 1.f;
}

void UCompanionHud::RefreshValues()
{
	const AWaveDirector* Dir = Director.Get();
	const AWaveDirector::FEndlessHudSnapshot Snap = Dir ? Dir->GetHudSnapshot()
		: AWaveDirector::FEndlessHudSnapshot();

	RefreshMinimap();
	RefreshCompanionMode();
	RefreshBossHud();

	// 玩家血条（导演不在也照画——安全网模式下血量仍是核心信息）
	if (const UHealthComponent* Health = PlayerHealth.Get())
	{
		const float Max = FMath::Max(Health->GetMaxHealth(), 1.f);
		const float Cur = Health->GetCurrentHealth();

		// 迟滞层（damage-lag，格斗游戏标配）：受击白条留住旧血再慢速追上——挨了多少一眼可见；
		// 回血立即拉平（涨血不吃迟滞）。追速 2.5 配 0.15s 轮询节拍
		if (!bLagInit || Cur > LagHealth)
		{
			LagHealth = Cur;
			bLagInit = true;
		}
		else
		{
			LagHealth = FMath::FInterpTo(LagHealth, Cur, 0.15f, 2.5f);
		}

		if (HealthBar.IsValid())
		{
			HealthBar->SetPercent(Cur / Max);
		}
		if (LagBar.IsValid())
		{
			LagBar->SetPercent(LagHealth / Max);
		}
		if (HealthText.IsValid())
		{
			HealthText->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Cur, Max)));
		}
	}

	// 闪烁图标沿真实已获卡稀有度；召回/自动射击/钥匙没有升级卡稀有度，使用基础操作色。
	EUpgradeCardRarity BlinkRarity = EUpgradeCardRarity::Legendary;
	for (const AWaveDirector::FHudUpgradeCard& Card : Snap.UpgradeCards)
	{
		if (Card.UpgradeId == FName(TEXT("Blink")))
		{
			BlinkRarity = Card.Rarity;
			break;
		}
	}
	const AAICompanionDemoCharacter* Char = PlayerChar.Get();
	UpdateSkillSlot(BlinkSlot, Char && Char->IsBlinkUnlocked(),
		Char ? Char->GetDashCooldownRemaining() : 0.f, Char ? Char->GetDashCooldownTotal() : 0.f,
		BlinkRarity, TEXT("抽卡解锁"), TEXT("就绪"));
	UpdateSkillSlot(RecallSlot, Char && Char->IsRecallUnlocked(),
		Char ? Char->GetRecallCooldownRemaining() : 0.f, Char ? Char->GetRecallCooldownTotal() : 0.f,
		EUpgradeCardRarity::Common, TEXT("信标解锁"), TEXT("就绪"));
	UpdateSkillSlot(ShotSlot, Dir && Snap.Level >= 6,
		0.f, 1.f, EUpgradeCardRarity::Common, TEXT("Lv.6 解锁"), TEXT("自动开火"));
	// 持钥槽保留锁箱资格读数；外圈与其他基础操作使用统一配色。
	UpdateSkillSlot(KeySlot, Dir && Snap.bHasTreasureKey,
		0.f, 1.f, EUpgradeCardRarity::Common, TEXT("寻钥"), TEXT("可开锁箱"));

	if (!Dir)
	{
		return;
	}

	// 导演快照：顶部行 + XP + 强化列表
	const int32 TotalSec = static_cast<int32>(Snap.SurvivalSeconds);
	if (TimeText.IsValid())
	{
		// 存活计时=主视觉（U3：VS 惯例，run timer 是情绪核心），血量倍率并入统计行
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d"), TotalSec / 60, TotalSec % 60)));
	}
	if (StatText.IsValid())
	{
		StatText->SetText(FText::FromString(FString::Printf(TEXT("活敌 %d    击杀 %d    金 %d    血量 ×%.2f"),
			Snap.AliveCount, Snap.KillCount, Snap.GoldCount, Snap.HpMultiplier)));
	}
	if (XpBar.IsValid())
	{
		XpBar->SetPercent(static_cast<float>(Snap.CurrentXp) / FMath::Max(Snap.XpToNext, 1));
	}
	if (XpText.IsValid())
	{
		XpText->SetText(FText::FromString(FString::Printf(TEXT("Lv.%d    XP %d / %d"),
			Snap.Level, Snap.CurrentXp, Snap.XpToNext)));
	}

	// 名称只进入工具提示；常驻强化区是图标+次数，不解析中文名或升级长说明。
	TArray<FHudCardDisplay> Cards;
	Cards.Reserve(Snap.UpgradeCards.Num());
	for (const AWaveDirector::FHudUpgradeCard& Card : Snap.UpgradeCards)
	{
		FHudCardDisplay Display;
		Display.UpgradeId = Card.UpgradeId;
		Display.Name = Card.Name;
		Display.Count = Card.Count;
		Display.Rarity = Card.Rarity;
		Cards.Add(MoveTemp(Display));
	}
	if (CardIconGrid.IsValid() && (!bCardIconsBuilt || Cards != LastCards))
	{
		LastCards = MoveTemp(Cards);
		CardIconGrid->ClearChildren();
		FrenzyRelicIcon.Reset();
		FrenzyRelicCountdownText.Reset();
		bCardIconsBuilt = true;
		++CardGridBuildCount;
		for (int32 Index = 0; Index < LastCards.Num(); ++Index)
		{
			const FHudCardDisplay& Card = LastCards[Index];
			const FLinearColor RarityColor = UpgradeCardPresentation::GetRarityColor(Card.Rarity);
			TSharedPtr<SHudSkillIcon> CardIcon;
			TSharedRef<SOverlay> Tile = SNew(SOverlay)
				.ToolTipText(FText::FromString(FString::Printf(TEXT("%s ×%d · %s"), *Card.Name,
					Card.Count, UpgradeCardPresentation::GetRarityLabel(Card.Rarity))))
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SAssignNew(CardIcon, SHudSkillIcon)
					.Size(52.f)
					.ArtBrush(ResolveIconBrush(Card.UpgradeId))
					.RingColor(RarityColor)
					.UniqueRelic(Card.Rarity == EUpgradeCardRarity::UniqueRelic)
				];
			if (Card.UpgradeId == FName(TEXT("SpiritFrenzy")))
			{
				// 秒数与激活状态不参与结构diff，只刷新原图标和短标签。
				FrenzyRelicIcon = CardIcon;
				Tile->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
				[
					SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(2.f, 0.f))
					[
						SAssignNew(FrenzyRelicCountdownText, STextBlock)
						.Font(HudFont(11.f, true))
						.ColorAndOpacity(FSlateColor(RarityColor))
					]
				];
			}
			if (Card.Count > 1)
			{
				Tile->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
				[
					SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(3.f, 1.f))
					[
						SNew(STextBlock)
						.Font(HudFont(11.f, true))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text(FText::FromString(FString::Printf(TEXT("×%d"), Card.Count)))
					]
				];
			}
			CardIconGrid->AddSlot(Index % 6, Index / 6)[Tile];
		}
	}
	else if (CardIconGrid.IsValid())
	{
		++UnchangedCardSnapshotCount;
	}
	LastFrenzyCooldown = LastFrenzyTotal = LastFrenzyActive = 0.f;
	for (const AWaveDirector::FHudUpgradeCard& Card : Snap.UpgradeCards)
	{
		if (Card.UpgradeId != FName(TEXT("SpiritFrenzy"))) continue;
		LastFrenzyCooldown = Card.CooldownRemaining;
		LastFrenzyTotal = Card.CooldownTotal;
		LastFrenzyActive = Card.ActiveRemaining;
		if (!FrenzyRelicIcon.IsValid() || !FrenzyRelicCountdownText.IsValid()) break;
		const bool bRunning = Card.CooldownTotal > 0.f;
		const bool bActive = bRunning && Card.ActiveRemaining > 0.f;
		const FLinearColor Color = bActive ? FLinearColor(.95f, .30f, 1.f)
			: UpgradeCardPresentation::GetRarityColor(EUpgradeCardRarity::UniqueRelic);
		FrenzyRelicIcon->SetAppearance(Color, bRunning ? 1.f : .35f);
		FrenzyRelicCountdownText->SetColorAndOpacity(FSlateColor(bRunning ? Color : kHudDimWhite));
		const FString State = !bRunning ? TEXT("已停止") : bActive
			? FString::Printf(TEXT("狂暴%ds"), FMath::CeilToInt(Card.ActiveRemaining))
			: FString::Printf(TEXT("%ds"), FMath::CeilToInt(Card.CooldownRemaining));
		FrenzyRelicCountdownText->SetText(FText::FromString(State));
		FrenzyRelicCountdownText->SetToolTipText(FText::FromString(FString::Printf(
			TEXT("不灭执念 · 距下次狂暴 %.1f 秒；当前狂暴剩余 %.1f 秒。"),
			Card.CooldownRemaining, Card.ActiveRemaining)));
		break;
	}
}

void UCompanionHud::RefreshCompanionMode()
{
	if (!ModeSource.IsValid())
	{
		ModeSource = Cast<ACompanionAIController>(UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass()));
	}
	const ACompanionAIController* AI = ModeSource.Get();
	if (ModeTitleText.IsValid())
	{
		const bool bGuard = AI && AI->CurrentMode == ECompanionMode::Guard;
		ModeTitleText->SetText(FText::FromString(AI ? FString::Printf(TEXT("Q  切换队友模式 · 当前%s"), bGuard ? TEXT("护卫") : TEXT("歼灭")) : TEXT("Q  队友正在入场")));
		ModeTitleText->SetColorAndOpacity(FSlateColor(!AI ? kHudDimWhite : bGuard ? FLinearColor(.45f, 1.f, .78f) : FLinearColor(1.f, .55f, .38f)));
	}
	if (ModeDescriptionText.IsValid())
	{
		ModeDescriptionText->SetText(FText::FromString(AI ? FString::Printf(TEXT("护卫：治疗 +%.0f%%\n歼灭：AI 伤害 +%.0f%%"),
			FMath::Clamp(AI->GuardHealingBonus, 0.f, 2.f) * 100.f, FMath::Clamp(AI->AnnihilationDamageBonus, 0.f, 2.f) * 100.f) : FString()));
	}
}

FString UCompanionHud::DebugCompanionModeState() const
{
#if WITH_EDITOR
	auto Quote = [](FString Text)
	{
		Text.ReplaceInline(TEXT("\\"), TEXT("\\\\")); Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Text.ReplaceInline(TEXT("\n"), TEXT("\\n")); Text.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		return FString(TEXT("\"")) + Text + TEXT("\"");
	};
	const ACompanionAIController* AI = ModeSource.Get();
	return FString::Printf(TEXT("{\"sourceAvailable\":%s,\"slateReady\":%s,\"title\":%s,\"description\":%s,\"guard\":%s,\"healingMultiplier\":%.4f,\"damageMultiplier\":%.4f}"),
		AI ? TEXT("true") : TEXT("false"), ModeTitleText.IsValid() && ModeDescriptionText.IsValid() ? TEXT("true") : TEXT("false"),
		*Quote(ModeTitleText.IsValid() ? ModeTitleText->GetText().ToString() : FString()),
		*Quote(ModeDescriptionText.IsValid() ? ModeDescriptionText->GetText().ToString() : FString()),
		AI && AI->CurrentMode == ECompanionMode::Guard ? TEXT("true") : TEXT("false"),
		AI ? AI->GetPlayerHealingMultiplier() : 1.f, AI ? AI->GetModeDamageMultiplier() : 1.f);
#else
	return TEXT("{}");
#endif
}

FString UCompanionHud::DebugSkillIconState() const
{
#if WITH_EDITOR
	auto JsonString = [](FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return FString(TEXT("\"")) + Value + TEXT("\"");
	};
	auto SlotJson = [&](const TCHAR* Name, const FSkillSlotWidgets& SkillSlot)
	{
		const FLinearColor Ring = SkillSlot.Icon.IsValid() ? SkillSlot.Icon->GetRingColor() : FLinearColor::Transparent;
		const FString Key = SkillSlot.KeyText.IsValid() ? SkillSlot.KeyText->GetText().ToString() : FString();
		const FString State = SkillSlot.StateText.IsValid() ? SkillSlot.StateText->GetText().ToString() : FString();
		return FString::Printf(TEXT("{\"name\":%s,\"slate_ready\":%s,\"key_text\":%s,\"state_text\":%s,"
			"\"unlocked_at_last_poll\":%s,\"remaining_at_last_poll\":%.4f,\"total_at_last_poll\":%.4f,"
			"\"assigned_cooldown_fraction\":%.4f,\"ring_rgba\":[%.4f,%.4f,%.4f,%.4f],\"icon_opacity\":%.4f}"),
			*JsonString(Name), SkillSlot.Icon.IsValid() && SkillSlot.KeyText.IsValid() &&
			SkillSlot.StateText.IsValid() && SkillSlot.CooldownBar.IsValid() ? TEXT("true") : TEXT("false"),
			*JsonString(Key), *JsonString(State), SkillSlot.bLastUnlocked ? TEXT("true") : TEXT("false"),
			SkillSlot.LastRemaining, SkillSlot.LastTotal, SkillSlot.AssignedCooldownFraction,
			Ring.R, Ring.G, Ring.B, Ring.A, SkillSlot.Icon.IsValid() ? SkillSlot.Icon->GetIconOpacity() : 0.f);
	};
	int32 ValidTextures = 0;
	int32 BoundBrushes = 0;
	for (const TPair<FName, TObjectPtr<UTexture2D>>& Entry : IconTextures)
	{
		if (IsValid(Entry.Value.Get()))
		{
			++ValidTextures;
			const TSharedPtr<FSlateBrush>* Brush = IconBrushes.Find(Entry.Key);
			if (Brush && Brush->IsValid() && (*Brush)->GetResourceObject() == Entry.Value.Get())
			{
				++BoundBrushes;
			}
		}
	}
	FString Cards(TEXT("["));
	for (int32 Index = 0; Index < LastCards.Num(); ++Index)
	{
		if (Index > 0) { Cards += TEXT(","); }
		const FHudCardDisplay& Card = LastCards[Index];
		const FLinearColor Ring = UpgradeCardPresentation::GetRarityColor(Card.Rarity);
		const TObjectPtr<UTexture2D>* Texture = IconTextures.Find(Card.UpgradeId);
		Cards += FString::Printf(TEXT("{\"id\":%s,\"name\":%s,\"count\":%d,\"rarity\":%d,"
			"\"ring_rgba\":[%.4f,%.4f,%.4f,%.4f],\"texture_alive\":%s}"),
			*JsonString(Card.UpgradeId.ToString()), *JsonString(Card.Name), Card.Count,
			static_cast<int32>(Card.Rarity), Ring.R, Ring.G, Ring.B, Ring.A,
			Texture && IsValid(Texture->Get()) ? TEXT("true") : TEXT("false"));
	}
	Cards += TEXT("]");
	const int32 WidgetCount = CardIconGrid.IsValid() ? CardIconGrid->GetChildren()->Num() : 0;
	return FString::Printf(TEXT("{\"upgrade_icon_widgets\":%d,\"last_snapshot_cards\":%d,"
		"\"grid_built\":%s,\"grid_build_count\":%d,\"unchanged_snapshot_count\":%d,"
		"\"texture_cache_count\":%d,\"valid_texture_count\":%d,\"brush_cache_count\":%d,"
		"\"texture_bound_brush_count\":%d,\"cards\":%s,\"operation_slots\":[%s,%s,%s,%s],"
		"\"spirit_frenzy\":{\"slate_ready\":%s,\"text\":%s,\"next_in_at_last_poll\":%.4f,"
		"\"interval_at_last_poll\":%.4f,\"active_at_last_poll\":%.4f,\"ring_rgba\":[%.4f,%.4f,%.4f,%.4f]}}"),
		WidgetCount, LastCards.Num(), bCardIconsBuilt ? TEXT("true") : TEXT("false"),
		CardGridBuildCount, UnchangedCardSnapshotCount, IconTextures.Num(), ValidTextures,
		IconBrushes.Num(), BoundBrushes, *Cards,
		*SlotJson(TEXT("blink"), BlinkSlot), *SlotJson(TEXT("recall"), RecallSlot),
		*SlotJson(TEXT("auto_fire"), ShotSlot), *SlotJson(TEXT("key"), KeySlot),
		FrenzyRelicIcon.IsValid() && FrenzyRelicCountdownText.IsValid() ? TEXT("true") : TEXT("false"),
		*JsonString(FrenzyRelicCountdownText.IsValid() ? FrenzyRelicCountdownText->GetText().ToString() : FString()),
		LastFrenzyCooldown, LastFrenzyTotal, LastFrenzyActive,
		FrenzyRelicIcon.IsValid() ? FrenzyRelicIcon->GetRingColor().R : 0.f,
		FrenzyRelicIcon.IsValid() ? FrenzyRelicIcon->GetRingColor().G : 0.f,
		FrenzyRelicIcon.IsValid() ? FrenzyRelicIcon->GetRingColor().B : 0.f,
		FrenzyRelicIcon.IsValid() ? FrenzyRelicIcon->GetRingColor().A : 0.f);
#else
	return FString();
#endif
}
