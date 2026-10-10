/**
 * [INPUT]: CompanionHud Slate容器、BossBase真实身份/Health、世界活Actor；不修改战斗数值或Boss状态
 * [OUTPUT]: 屏幕中央上方响应式Boss血条；0.15s扫描、按身份稳定堆叠、免伤真读数、死亡/销毁移除；DebugBossHudState只读实际树和几何
 * [POS]: UI/ 常驻HUD的Boss显示局部实现，所有弱Actor引用，Slate资源随宿主树释放；高层菜单继续覆盖HUD
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */
#include "CompanionHud.h"

#include "AI/BossBase.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UCompanionHud::BuildBossHudLane()
{
	BossRows.Reset();
	return SAssignNew(BossLanePadding, SBox)
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.f, BossLaneTop, 0.f, 0.f))
		[
			SAssignNew(BossLaneBox, SBox)
			.WidthOverride(BossLaneWidth)
			.Visibility(EVisibility::Collapsed)
			[SAssignNew(BossRowsBox, SVerticalBox)]
		];
}

void UCompanionHud::UpdateBossHudLayout(const FGeometry& Geometry)
{
	const FVector2D Size = Geometry.GetLocalSize();
	if (Size.X <= 1.f || Size.Y <= 1.f) { return; }
	// 顶部计时/统计保留；血条约屏幕高度17%，居中宽52%，小窗口也不占满视野。
	BossLaneWidth = FMath::Min(FMath::Clamp(Size.X * .52f, 340.f, 680.f), FMath::Max(1.f, Size.X - 40.f));
	BossLaneTop = FMath::Max(112.f, Size.Y * .17f);
	if (BossLaneBox.IsValid()) { BossLaneBox->SetWidthOverride(BossLaneWidth); }
	if (BossLanePadding.IsValid()) { BossLanePadding->SetPadding(FMargin(0.f, BossLaneTop, 0.f, 0.f)); }
}

void UCompanionHud::RefreshBossHud()
{
	if (!BossRowsBox.IsValid() || !BossLaneBox.IsValid() || !GetWorld()) { return; }
	TArray<ABossBase*> Alive;
	for (TActorIterator<ABossBase> It(GetWorld()); It; ++It)
	{
		ABossBase* Boss = *It;
		const UHealthComponent* Health = Boss ? Boss->GetHealthComponent() : nullptr;
		if (IsValid(Boss) && !Boss->IsActorBeingDestroyed() && Health && !Health->IsDead()) { Alive.Add(Boss); }
	}
	Alive.Sort([](const ABossBase& A, const ABossBase& B)
	{
		return A.GetBossIndex() == B.GetBossIndex() ? A.GetPathName() < B.GetPathName() : A.GetBossIndex() < B.GetBossIndex();
	});
	bool bRebuild = BossRows.Num() != Alive.Num();
	if (!bRebuild)
	{
		for (int32 Index = 0; Index < Alive.Num(); ++Index)
		{
			if (BossRows[Index].Boss.Get() != Alive[Index]) { bRebuild = true; break; }
		}
	}
	if (bRebuild)
	{
		BossRowsBox->ClearChildren();
		BossRows.Reset();
		for (ABossBase* Boss : Alive)
		{
			FBossHudRow& Row = BossRows.AddDefaulted_GetRef();
			Row.Boss = Boss;
			BossRowsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(12.f, 7.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
					[
						SAssignNew(Row.Label, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
						.Justification(ETextJustify::Center)
						.ColorAndOpacity(FLinearColor(.98f, .86f, .73f))
						.ShadowOffset(FVector2D(1.f, 1.f))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(13.f)
						[
							SAssignNew(Row.Bar, SProgressBar)
							.Style(&RoundedBarStyle)
							.BarFillType(EProgressBarFillType::LeftToRight)
							.BorderPadding(FVector2D::ZeroVector)
						]
					]
				]
			];
		}
	}
	BossLaneBox->SetVisibility(BossRows.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	for (FBossHudRow& Row : BossRows)
	{
		const ABossBase* Boss = Row.Boss.Get();
		const UHealthComponent* Health = Boss ? Boss->GetHealthComponent() : nullptr;
		if (!Health || !Row.Label.IsValid() || !Row.Bar.IsValid()) { continue; }
		const float Max = FMath::Max(1.f, Health->GetMaxHealth());
		const float Current = Health->GetCurrentHealth();
		const bool bInvulnerable = Health->IsInvulnerable();
		Row.AssignedPercent = FMath::Clamp(Current / Max, 0.f, 1.f);
		Row.Bar->SetPercent(Row.AssignedPercent);
		Row.Bar->SetFillColorAndOpacity(bInvulnerable ? FLinearColor(.60f, .43f, .80f) : FLinearColor(.88f, .15f, .12f));
		Row.Label->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  %.0f / %.0f%s"),
			*Boss->GetBossName(), Current, Max, bInvulnerable ? TEXT("  ·  暂时免伤") : TEXT(""))));
	}
}

FString UCompanionHud::DebugBossHudState() const
{
#if WITH_EDITOR
	auto Quote = [](FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\")); Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n")); Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		return FString(TEXT("\"")) + Value + TEXT("\"");
	};
	FString Rows(TEXT("["));
	for (int32 Index = 0; Index < BossRows.Num(); ++Index)
	{
		if (Index > 0) { Rows += TEXT(","); }
		const FBossHudRow& Row = BossRows[Index];
		const ABossBase* Boss = Row.Boss.Get();
		const UHealthComponent* Health = Boss ? Boss->GetHealthComponent() : nullptr;
		Rows += FString::Printf(TEXT("{\"path\":%s,\"bossIndex\":%d,\"name\":%s,\"label\":%s,\"hp\":%.4f,\"maxHp\":%.4f,\"assignedPercent\":%.6f,\"invulnerable\":%s,\"dead\":%s,\"slateReady\":%s}"),
			*Quote(Boss ? Boss->GetPathName() : FString()), Boss ? Boss->GetBossIndex() : 0,
			*Quote(Boss ? Boss->GetBossName() : FString()), *Quote(Row.Label.IsValid() ? Row.Label->GetText().ToString() : FString()),
			Health ? Health->GetCurrentHealth() : 0.f, Health ? Health->GetMaxHealth() : 0.f, Row.AssignedPercent,
			Health && Health->IsInvulnerable() ? TEXT("true") : TEXT("false"), !Health || Health->IsDead() ? TEXT("true") : TEXT("false"),
			Row.Label.IsValid() && Row.Bar.IsValid() ? TEXT("true") : TEXT("false"));
	}
	Rows += TEXT("]");
	const FVector2D HudSize = GetCachedGeometry().GetLocalSize();
	const FVector2D Size = BossLaneBox.IsValid() ? BossLaneBox->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	const FVector2D Position = BossLaneBox.IsValid() ? GetCachedGeometry().AbsoluteToLocal(BossLaneBox->GetCachedGeometry().GetAbsolutePosition()) : FVector2D::ZeroVector;
	return FString::Printf(TEXT("{\"inViewport\":%s,\"slateReady\":%s,\"visible\":%s,\"rowCount\":%d,\"hudSize\":[%.3f,%.3f],\"position\":[%.3f,%.3f],\"size\":[%.3f,%.3f],\"configuredWidth\":%.3f,\"configuredTop\":%.3f,\"rows\":%s}"),
		IsInViewport() ? TEXT("true") : TEXT("false"), BossLaneBox.IsValid() && BossRowsBox.IsValid() ? TEXT("true") : TEXT("false"),
		BossLaneBox.IsValid() && BossLaneBox->GetVisibility() != EVisibility::Collapsed ? TEXT("true") : TEXT("false"), BossRows.Num(),
		HudSize.X, HudSize.Y, Position.X, Position.Y, Size.X, Size.Y, BossLaneWidth, BossLaneTop, *Rows);
#else
	return TEXT("{}");
#endif
}
