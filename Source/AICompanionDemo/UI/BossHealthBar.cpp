/**
 * [INPUT]: 依赖 BossHealthBar.h 的组件契约、Slate 的 SOverlay/SProgressBar/STextBlock、FCoreStyle 默认字体、HealthComponent 血量读数
 * [OUTPUT]: 对外提供 UBossHealthBar 的实现——BindSource 注入；NativeTick 0.1s 节流轮询（血条百分比+名字+数字）
 * [POS]: UI/ 界面层实现件：世界空间 Boss 血条（Screen 模式随 Boss 投影）
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "BossHealthBar.h"

#include "Health/HealthComponent.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void UBossHealthBar::BindSource(UHealthComponent* InHealth, const FString& InBossName)
{
	Health = InHealth;
	BossName = InBossName;
}

TSharedRef<SWidget> UBossHealthBar::RebuildWidget()
{
	return SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SAssignNew(HealthBar, SProgressBar)
			.BarFillType(EProgressBarFillType::LeftToRight)
			.FillColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.16f, 0.13f)))
			.BorderPadding(FVector2D(0.f, 0.f))
			.Percent(1.f)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(NameText, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
			.ShadowOffset(FVector2D(1.5f, 1.5f))
			.Text(FText::FromString(BossName))
		];
}

void UBossHealthBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 0.1s 轮询（UI 血条不需要逐帧——省一半刷新）
	static float Throttle = 0.f;
	Throttle -= InDeltaTime;
	if (Throttle > 0.f)
	{
		return;
	}
	Throttle = 0.1f;

	if (const UHealthComponent* Comp = Health.Get())
	{
		const float Max = FMath::Max(Comp->GetMaxHealth(), 1.f);
		const float Cur = Comp->GetCurrentHealth();
		if (HealthBar.IsValid())
		{
			HealthBar->SetPercent(Cur / Max);
		}
		if (NameText.IsValid())
		{
			NameText->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f / %.0f"),
				*BossName, Cur, Max)));
		}
	}
}
