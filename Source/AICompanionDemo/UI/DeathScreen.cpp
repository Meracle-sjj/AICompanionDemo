/**
 * [INPUT]: 依赖 DeathScreen.h 的组件契约、Slate 控件头（SBorder/SBoxPanel/STextBlock/SButton）、FCoreStyle 默认字体、FPlatformTime 真实时钟、Kismet 的 GetCurrentLevelName/OpenLevel
 * [OUTPUT]: 对外提供 UDeathScreen 的实现——UpdateReveal 三段编排（0-1.2s 黑幕 alpha 0→0.88；1.2s 标题+统计显形；2.0s 重开按钮显形，防一命呜呼时的手滑误点）
 * [POS]: UI/ 三件套实现之二：全项目的"终局感"承载——此前玩家死亡什么都没发生（静默回血清零），此刻起死亡有分量
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "DeathScreen.h"

#include "AICompanionDemoGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// 死亡幕配色：暗幕黑 / 标题血红 / 统计灰白 / 按钮金
	const FLinearColor kDeathDim(0.f, 0.f, 0.f, 0.88f);
	const FLinearColor kDeathTitleRed(0.88f, 0.18f, 0.14f);
	const FLinearColor kDeathStatSoft(0.85f, 0.86f, 0.90f);
	const FLinearColor kDeathButtonGold(1.f, 0.85f, 0.40f);

	// 渐入编排相位（真实时钟秒）：黑幕 → 内容 → 世界定格 → 按钮
	constexpr float kDimDuration = 1.2f;
	constexpr float kContentAt = 1.2f;
	constexpr float kFreezeAt = 1.5f;   // 倒地窗结束：布娃娃已倒，世界定格
	constexpr float kButtonAt = 2.0f;

	FSlateFontInfo DeathFont(float Size, bool bBold = false)
	{
		FSlateFontInfo Font = FCoreStyle::Get().GetFontStyle(bBold ? TEXT("Bold") : TEXT("Normal"));
		Font.Size = Size;
		return Font;
	}
}

void UDeathScreen::BindHost(AAICompanionDemoGameMode* InHost)
{
	Host = InHost;
}

void UDeathScreen::Setup(float InSurvivalSeconds, int32 InKills, int32 InLevel)
{
	SurvivalSeconds = InSurvivalSeconds;
	Kills = InKills;
	Level = InLevel;

	// 统计文字补写（Setup 早于 RebuildWidget 时由 Tick 的首次 UpdateReveal 兜底重写）
	const int32 TotalSec = static_cast<int32>(SurvivalSeconds);
	if (StatText.IsValid())
	{
		StatText->SetText(FText::FromString(FString::Printf(
			TEXT("存活 %d:%02d    击杀 %d    等级 Lv.%d"), TotalSec / 60, TotalSec % 60, Kills, Level)));
	}
}

TSharedRef<SWidget> UDeathScreen::RebuildWidget()
{
	OpenRealSeconds = FPlatformTime::Seconds();

	return SAssignNew(DimBorder, SBorder)
		// 09-24 根因修复：默认 brush 指向编辑器贴图（游戏运行时不存在→黑幕从未画过）——挂代码生成纯白 brush
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f)))   // 起步全透——黑幕由 Tick 渐入
		.Padding(0.f)
		[
			// 09-24 塌缩修正（同菜单幕布案）：黑幕内容必须撑满全屏，否则 SBorder 缩成中央文字块
			SNew(SOverlay)

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SSpacer)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.FillHeight(0.42f)
			[
				SNullWidget::NullWidget
			]

			// ---------- 中央：标题 + 统计 ----------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(TitleText, STextBlock)
				.Font(DeathFont(52.f, true))
				.ColorAndOpacity(FSlateColor(kDeathTitleRed))
				.ShadowOffset(FVector2D(2.f, 2.f))
				.Visibility(EVisibility::Hidden)
				.Text(FText::FromString(TEXT("你倒下了")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 18.f, 0.f, 0.f)
			[
				SAssignNew(StatText, STextBlock)
				.Font(DeathFont(27.f))
				.ColorAndOpacity(FSlateColor(kDeathStatSoft))
				.Visibility(EVisibility::Hidden)
				.Text(FText::GetEmpty())
			]

			// ---------- 重开按钮 ----------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 42.f, 0.f, 0.f)
			[
				SAssignNew(RestartButton, SButton)
				.Visibility(EVisibility::Hidden)
				// UObject 绑定走 CreateUObject（弱引用安全）——裸 (this, &Fn) 走 CreateSP 要求 TSharedFromThis<C++类>，UWidget 子类必炸（C2039 实证）
				.OnClicked(FOnClicked::CreateUObject(this, &UDeathScreen::OnRestartClicked))
				.Content()
				[
					SNew(STextBlock)
					.Font(DeathFont(28.f, true))
					.ColorAndOpacity(FSlateColor(kDeathButtonGold))
					.Text(FText::FromString(TEXT("重 新 开 始")))
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.30f)
			[
				SNullWidget::NullWidget
			]
			]
		];
}

void UDeathScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 首帧兜底：Setup 早于 RebuildWidget 时统计文字未生效，这里补写
	const int32 TotalSec = static_cast<int32>(SurvivalSeconds);
	if (StatText.IsValid() && StatText->GetText().IsEmpty())
	{
		StatText->SetText(FText::FromString(FString::Printf(
			TEXT("存活 %d:%02d    击杀 %d    等级 Lv.%d"), TotalSec / 60, TotalSec % 60, Kills, Level)));
	}

	UpdateReveal();
}

void UDeathScreen::UpdateReveal()
{
	// 真实时钟（世界 dilation=0 定格下 UMG tick 的 dt 也是 0——世界时钟在此场景是死的）
	const double Elapsed = FPlatformTime::Seconds() - OpenRealSeconds;

	// 倒地窗（1.5s，kContentAt 与 kButtonAt 之间）：黑幕渐入期保持世界流速，布娃娃才有时间真的倒下
	//（首版立即 dilation=0 把物理定格在站姿——用户截图"僵直站立"案）；此刻定格，怪与计时同时停
	if (!bFreezeSent && Elapsed >= kFreezeAt && Host.IsValid())
	{
		bFreezeSent = true;
		Host->FreezeWorld();
	}

	if (DimBorder.IsValid())
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Elapsed / kDimDuration), 0.f, 1.f);
		DimBorder->SetBorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.f, 0.f, Alpha * kDeathDim.A)));
	}
	if (TitleText.IsValid() && Elapsed >= kContentAt)
	{
		TitleText->SetVisibility(EVisibility::Visible);
	}
	if (StatText.IsValid() && Elapsed >= kContentAt)
	{
		StatText->SetVisibility(EVisibility::Visible);
	}
	if (RestartButton.IsValid() && Elapsed >= kButtonAt)
	{
		RestartButton->SetVisibility(EVisibility::Visible);
	}
}

FReply UDeathScreen::OnRestartClicked()
{
	// OpenLevel 当前图：关卡全量重置（时间膨胀/暂停/账本随重载归位）
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
	return FReply::Handled();
}
