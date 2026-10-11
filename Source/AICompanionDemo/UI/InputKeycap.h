/**
 * [INPUT]: Slate布局/文本与Active属性；呼叫方提供键名和可用状态。
 * [OUTPUT]: F1与TAB共用浅青键帽，底部4px阴影/55×40逻辑尺寸。
 * [POS]: UI无状态显示片段；不绑定输入、鼠标或玩法资格。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#pragma once
#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

inline TSharedRef<SWidget> MakeInputKeycap(const FString& Key, TAttribute<bool> Active = true)
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	return SNew(SBorder).BorderImage(White).Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		.BorderBackgroundColor_Lambda([Active]() { return FSlateColor(Active.Get()
			? FLinearColor(.10f, .29f, .34f) : FLinearColor(.18f, .23f, .25f)); })
		[
			SNew(SBorder).BorderImage(White).Padding(0.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.BorderBackgroundColor_Lambda([Active]() { return FSlateColor(Active.Get()
				? FLinearColor(.68f, .90f, .97f) : FLinearColor(.35f, .42f, .46f)); })
			[
				SNew(STextBlock).Text(FText::FromString(Key))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", Key.Len() > 2 ? 17 : 20))
				.ColorAndOpacity(FLinearColor(.015f, .08f, .105f)).AutoWrapText(false)
			]
		];
}
