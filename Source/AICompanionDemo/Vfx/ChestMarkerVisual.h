/**
 * [INPUT]: 宿主注入免费/金币/钥匙身份、导演真实价格和原互动资格；WidgetComponent/Slate/本地相机。
 * [OUTPUT]: 常驻加粗FREE!、同水平中心的漫画E键帽、接近平滑显现的锁+大数字/钥匙标记；零碰撞、朝镜头、领取/终局/EndPlay清理。
 * [POS]: Vfx宝箱纯展示组件；距离渐显仅决定画面，不决定钱包、持钥和E键交互。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "ChestMarkerVisual.generated.h"

enum class EChestMarkerKind : uint8 { Free, Gold, Key };
class SChestMarkerGlyph;

UCLASS(ClassGroup=(Visual))
class AICOMPANIONDEMO_API UChestMarkerVisual : public UWidgetComponent
{
	GENERATED_BODY()
public:
	UChestMarkerVisual(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void Present(EChestMarkerKind Kind, int32 Price, bool bInteractNearby, bool bHasKey = false);
	void Retire();
	FString DebugVisualState() const;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void ReleaseResources() override;
private:
	void FaceCamera();
	TSharedPtr<SChestMarkerGlyph> Glyph;
	EChestMarkerKind DisplayKind = EChestMarkerKind::Gold;
	int32 DisplayPrice = 0;
	float Opacity = 0.f;
	float TargetOpacity = 0.f;
	float PlayerDistance = MAX_flt;
	bool bNearby = false;
	bool bKeyOwned = false;
	bool bRetired = false;
};
