/**
 * [INPUT]: RunEventLog的实际记录/代次、Slate字体/自动换行/滚动框。
 * [OUTPUT]: 620x190真实起播记录；固定行宽消除延后换行，预排版及跨帧末行可见确认防最后一条滞后。
 * [POS]: UI/RunEventPanel局部实现；暂停不改变记录或游戏时钟，Travel随旧世界销毁。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#include "RunEventPanel.h"
#include "RunEventStyle.h"
#include "Events/RunEventLog.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
URunEventPanel::URunEventPanel(const FObjectInitializer& Initializer) : Super(Initializer)
{
 SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}
TSharedRef<SWidget> URunEventPanel::RebuildWidget()
{
 LastRevision=-1;
 return SNew(SOverlay)
 +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(22.f,0.f,0.f,170.f))
 [
  SNew(SBox).WidthOverride(620.f).HeightOverride(190.f)
  [
   SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
   .BorderBackgroundColor(FLinearColor(0.01f,0.025f,0.04f,0.68f)).Padding(10.f)
   [
    SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,5.f)
    [SNew(STextBlock).Text(FText::FromString(TEXT("事件记录"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",12)).ColorAndOpacity(FLinearColor(0.62f,0.78f,0.83f))]
    + SVerticalBox::Slot().FillHeight(1.f)
    [SAssignNew(Scroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Rows,SVerticalBox)]]
   ]
  ]
 ];
}
void URunEventPanel::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
 Super::NativeTick(Geometry,DeltaTime);
 const URunEventLog* Log=GetWorld()?GetWorld()->GetSubsystem<URunEventLog>():nullptr;
 if(Log && Log->GetRevision()!=LastRevision) { RefreshEntries(); }
 if(bScrollPending && Scroll.IsValid() && LastRow.IsValid())
 {
  // ScrollToEnd下一次Tick只消费一次；换行/新树高度尚未稳定时必须继续确认真正末行。
  ForceLayoutPrepass();
  const FGeometry RowGeometry=LastRow->GetCachedGeometry();
  const FGeometry ScrollGeometry=Scroll->GetCachedGeometry();
  const float RowBottom=RowGeometry.LocalToAbsolute(FVector2D(0.f,RowGeometry.GetLocalSize().Y)).Y;
  const float ViewTop=ScrollGeometry.LocalToAbsolute(FVector2D::ZeroVector).Y;
  const float ViewBottom=ScrollGeometry.LocalToAbsolute(FVector2D(0.f,ScrollGeometry.GetLocalSize().Y)).Y;
  const bool bAtBottom=RowGeometry.GetLocalSize().Y>0.f && ScrollGeometry.GetLocalSize().Y>0.f
   && RowBottom>=ViewTop && RowBottom<=ViewBottom+1.f
   && FMath::IsNearlyEqual(Scroll->GetScrollOffset(),Scroll->GetScrollOffsetOfEnd(),1.f);
  StableBottomFrames=bAtBottom?StableBottomFrames+1:0;
  if(StableBottomFrames>=2) { bScrollPending=false; }
  else { Scroll->EndInertialScrolling(); Scroll->ScrollToEnd(); }
 }
}
void URunEventPanel::RefreshEntries()
{
 const URunEventLog* Log=GetWorld()?GetWorld()->GetSubsystem<URunEventLog>():nullptr;
 if(!Log || !Rows.IsValid()) { return; }
 Rows->ClearChildren();
 LastRow.Reset();LastText.Reset();StableBottomFrames=0;
 for(const FRunEventEntry& Entry:Log->GetEntries())
 {
  const int32 Seconds=FMath::Max(0,FMath::FloorToInt(Entry.RunTime));
  const FString Line=FString::Printf(TEXT("[%02d:%02d] %s：%s"),Seconds/60,Seconds%60,*Entry.Speaker,*Entry.Text);
  Rows->AddSlot().AutoHeight().Padding(0.f,2.f)
  [SAssignNew(LastRow,STextBlock).Text(FText::FromString(Line)).Font(FCoreStyle::GetDefaultFontStyle("Regular",14))
   .AutoWrapText(false).WrapTextAt(570.f).ColorAndOpacity(Entry.Kind!=ERunEventKind::General || Entry.Speaker==TEXT("队友") ? RunEventColor(Entry.Kind) : FLinearColor(1.f,.78f,.44f))];
  LastText=Entry.Text;
 }
 DisplayedRows=Log->GetEntries().Num();
 LastRevision=Log->GetRevision();
 bScrollPending=LastRow.IsValid();
 if(Scroll.IsValid()) { ForceLayoutPrepass();Scroll->EndInertialScrolling();Scroll->ScrollToEnd(); }
}
void URunEventPanel::ReleaseSlateResources(bool bReleaseChildren)
{
 Super::ReleaseSlateResources(bReleaseChildren);
 Rows.Reset();Scroll.Reset();LastRevision=-1;DisplayedRows=0;
 LastRow.Reset();LastText.Reset();bScrollPending=false;StableBottomFrames=0;
}
FString URunEventPanel::DebugEventPanelState() const
{
 const URunEventLog* Log=GetWorld()?GetWorld()->GetSubsystem<URunEventLog>():nullptr;
 TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();
 Root->SetNumberField(TEXT("displayedRows"),DisplayedRows);
 Root->SetNumberField(TEXT("width"),620);Root->SetNumberField(TEXT("height"),190);
 Root->SetNumberField(TEXT("revision"),LastRevision);
 Root->SetStringField(TEXT("lastText"),LastText);
 Root->SetBoolField(TEXT("scrollPending"),bScrollPending);
 Root->SetNumberField(TEXT("stableBottomFrames"),StableBottomFrames);
 if(Scroll.IsValid() && LastRow.IsValid())
 {
  const FGeometry RowGeometry=LastRow->GetCachedGeometry();
  const FGeometry ScrollGeometry=Scroll->GetCachedGeometry();
  const float RowBottom=RowGeometry.LocalToAbsolute(FVector2D(0.f,RowGeometry.GetLocalSize().Y)).Y;
  const float ViewTop=ScrollGeometry.LocalToAbsolute(FVector2D::ZeroVector).Y;
  const float ViewBottom=ScrollGeometry.LocalToAbsolute(FVector2D(0.f,ScrollGeometry.GetLocalSize().Y)).Y;
  Root->SetNumberField(TEXT("scrollOffset"),Scroll->GetScrollOffset());
  Root->SetNumberField(TEXT("scrollEnd"),Scroll->GetScrollOffsetOfEnd());
  Root->SetNumberField(TEXT("lastRowHeight"),RowGeometry.GetLocalSize().Y);
  Root->SetNumberField(TEXT("lastRowBottom"),RowBottom);
  Root->SetNumberField(TEXT("viewportBottom"),ViewBottom);
  Root->SetBoolField(TEXT("lastRowBottomVisible"),RowGeometry.GetLocalSize().Y>0.f && RowBottom>=ViewTop && RowBottom<=ViewBottom+1.f);
 }
 TArray<TSharedPtr<FJsonValue>> Entries;
 if(Log) { for(const FRunEventEntry& Entry:Log->GetEntries()) {
  TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
  Row->SetNumberField(TEXT("seq"),Entry.Seq);Row->SetNumberField(TEXT("time"),Entry.RunTime);
  Row->SetStringField(TEXT("speaker"),Entry.Speaker);Row->SetStringField(TEXT("text"),Entry.Text);Row->SetStringField(TEXT("event"),Entry.EventId.ToString());
  Row->SetNumberField(TEXT("kind"),static_cast<int32>(Entry.Kind));
  Entries.Add(MakeShared<FJsonValueObject>(Row));
 } }
 Root->SetArrayField(TEXT("entries"),Entries);
 FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}
