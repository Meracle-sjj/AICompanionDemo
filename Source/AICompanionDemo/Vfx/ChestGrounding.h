/**
 * [INPUT]: 宝箱实际StaticMesh局部边界、现有根组件缩放/朝向与WorldStatic地形射线。
 * [OUTPUT]: 九点底面贴坡落位与只读接触快照；保留箱体XY、缩放和业务身份，底面轻埋沙。
 * [POS]: Vfx宝箱摆放局部工具；不拥有生成位置、互动半径、钱包、钥匙或奖励。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMeshComponent;

struct FChestGroundContact
{
	bool bGrounded = false;
	int32 HitSamples = 0;
	FVector LocalMin = FVector::ZeroVector;
	FVector LocalMax = FVector::ZeroVector;
	FVector GroundPoint = FVector::ZeroVector;
	FVector GroundNormal = FVector::UpVector;
	float BurialDepth = 0.f;
	float MaxBottomGap = 0.f;
	float MaxBottomBurial = 0.f;
	FString ToJson() const;
};

namespace ChestGrounding
{
	/** 保留Actor XY，用真实网格九点底面与实际坡面求根位置，失败不伪造落地。 */
	bool Place(AActor* Chest, UStaticMeshComponent* Body, FChestGroundContact& Contact);
	/** 以实际网格八角世界包围盒取得直立标记锚点，不继承箱体斜率/缩放。 */
	FVector MarkerAnchor(const UStaticMeshComponent* Body, float HeightAbove = 65.f);
}
