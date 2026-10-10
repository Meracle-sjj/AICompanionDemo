/**
 * [INPUT]: WurmBodyVisual.h 的纯外观/弱代理/宿主 EndPlay 契约；Engine 的 LoadObject、UStaticMesh 槽名查询与动态附属 UStaticMeshComponent；素材约定原点 pivot、+X forward、最大半径 50cm，宿主原 Sphere 按 Radius/50 缩放
 * [OUTPUT]: 五资源一次初始化加载、三网格逐个槽名校验、全成功才隐藏代理；NoCollision/不影响导航/不投影的头身尾外观以相对零位/一倍缩放继承代理，+X 按 DeltaSeconds 做帧率无关的平滑定向；代理失效/外观脱离后显式销毁，Shutdown 可安全重复调用
 * [POS]: Vfx/ 运行时附属显示实现；不更改代理的网格/材质/位置/旋转/缩放/碰撞，不认识战斗、状态机、火球或伤害；死亡外观随原代理移交碎屑系统，宿主必须继续调 TickCleanup
 * [PROTOCOL]: 变更时同步本文件头部，然后检查 ../AGENTS.md；资源契约见 /Content/Bosses/Wurm/AGENTS.md，宿主生命周期契约见 AI/BossWurm
 */

#include "WurmBodyVisual.h"

#include "AICompanionDemo.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace WurmBodyVisualPrivate
{
	// 独立命名空间规避 adaptive unity 合并 .cpp 时的匿名常量重名。
	constexpr int32 HeadMeshIndex = 0;
	constexpr int32 BodyMeshIndex = 1;
	constexpr int32 TailMeshIndex = 2;
	constexpr int32 BasaltMaterialIndex = 0;
	constexpr int32 CoreMaterialIndex = 1;
	constexpr float OrientationResponse = 30.6495f; // 60fps 下 alpha 约 0.4；其他帧率按时间指数收敛。

	const TCHAR* const MeshPaths[] =
	{
		TEXT("/Game/Bosses/Wurm/SM_Wurm_Head.SM_Wurm_Head"),
		TEXT("/Game/Bosses/Wurm/SM_Wurm_Body.SM_Wurm_Body"),
		TEXT("/Game/Bosses/Wurm/SM_Wurm_Tail.SM_Wurm_Tail")
	};
	const TCHAR* const MaterialPaths[] =
	{
		TEXT("/Game/Bosses/Wurm/M_Wurm_Basalt.M_Wurm_Basalt"),
		TEXT("/Game/Bosses/Wurm/M_Wurm_Core.M_Wurm_Core")
	};
	const FName BasaltSlot(TEXT("Basalt"));
	const FName CoreSlot(TEXT("MagmaCore"));
}

bool UWurmBodyVisual::Initialize(AActor* Owner, const TArray<UStaticMeshComponent*>& Proxies)
{
	Shutdown();
	if (!IsValid(Owner) || !Owner->GetWorld() || Proxies.IsEmpty())
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 宿主/体节代理无效，保留原球外观。"));
		return false;
	}

	TSet<UStaticMeshComponent*> SeenProxies;
	for (UStaticMeshComponent* Proxy : Proxies)
	{
		if (!IsValid(Proxy) || Proxy->IsBeingDestroyed() || Proxy->GetOwner() != Owner ||
			!Proxy->IsRegistered() || SeenProxies.Contains(Proxy))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 体节代理必须有效、已注册、同宿主且不重复，保留原球外观。"));
			return false;
		}
		SeenProxies.Add(Proxy);
	}

	VisualOwner = Owner;
	bool bAssetsValid = true;
	for (const TCHAR* Path : WurmBodyVisualPrivate::MeshPaths)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		LoadedMeshes.Add(Mesh);
		if (!IsValid(Mesh))
		{
			bAssetsValid = false;
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 缺少网格 %s，保留原球外观。"), Path);
		}
	}
	for (const TCHAR* Path : WurmBodyVisualPrivate::MaterialPaths)
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
		LoadedMaterials.Add(Material);
		if (!IsValid(Material))
		{
			bAssetsValid = false;
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 缺少材质 %s，保留原球外观。"), Path);
		}
	}
	if (!bAssetsValid)
	{
		Shutdown();
		return false;
	}

	// 三个 mesh 全部先验证；槽名是素材契约，不能假定槽序为 0/1。
	TArray<FIntPoint> MaterialSlots;
	for (const TObjectPtr<UStaticMesh>& Mesh : LoadedMeshes)
	{
		const int32 BasaltSlot = Mesh->GetMaterialIndex(WurmBodyVisualPrivate::BasaltSlot);
		const int32 CoreSlot = Mesh->GetMaterialIndex(WurmBodyVisualPrivate::CoreSlot);
		if (BasaltSlot == INDEX_NONE || CoreSlot == INDEX_NONE)
		{
			UE_LOG(LogAICompanionDemo, Warning,
				TEXT("[WurmVisual] 网格 %s 缺少 Basalt/MagmaCore 材质槽（%d/%d），保留原球外观。"),
				*Mesh->GetName(), BasaltSlot, CoreSlot);
			Shutdown();
			return false;
		}
		MaterialSlots.Emplace(BasaltSlot, CoreSlot);
	}

	ProxyComponents.Reserve(Proxies.Num());
	VisualComponents.Reserve(Proxies.Num());
	OriginalProxyVisibility.Reserve(Proxies.Num());
	HiddenProxies.Reserve(Proxies.Num());
	int64 TotalTriangles = 0;
	for (int32 Index = 0; Index < Proxies.Num(); ++Index)
	{
		UStaticMeshComponent* Proxy = Proxies[Index];
		const int32 MeshIndex = Index == 0 ? WurmBodyVisualPrivate::HeadMeshIndex :
			(Index == Proxies.Num() - 1 ? WurmBodyVisualPrivate::TailMeshIndex : WurmBodyVisualPrivate::BodyMeshIndex);
		UStaticMesh* Mesh = LoadedMeshes[MeshIndex];
		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		ProxyComponents.Add(Proxy);
		VisualComponents.Add(Visual);
		OriginalProxyVisibility.Add(Proxy->GetVisibleFlag());
		HiddenProxies.Add(false);
		if (!IsValid(Visual))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 外观组件创建失败，回退全部原球外观。"));
			Shutdown();
			return false;
		}

		Visual->SetMobility(EComponentMobility::Movable);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetGenerateOverlapEvents(false);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetCastShadow(false);
		Visual->SetVisibility(true, false);
		if (!Visual->SetStaticMesh(Mesh))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 外观网格赋值失败，回退全部原球外观。"));
			Shutdown();
			return false;
		}
		Visual->SetMaterial(MaterialSlots[MeshIndex].X, LoadedMaterials[WurmBodyVisualPrivate::BasaltMaterialIndex]);
		Visual->SetMaterial(MaterialSlots[MeshIndex].Y, LoadedMaterials[WurmBodyVisualPrivate::CoreMaterialIndex]);
		Owner->AddInstanceComponent(Visual);
		if (!Visual->AttachToComponent(Proxy, FAttachmentTransformRules::KeepRelativeTransform))
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 外观附属失败，回退全部原球外观。"));
			Shutdown();
			return false;
		}
		// 不补偿 proxy scale：素材最大半径 50cm，恰好继承原 Radius/50 的球比例。
		Visual->SetRelativeTransform(FTransform::Identity);
		Visual->RegisterComponent();
		if (!Visual->IsRegistered())
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmVisual] 外观注册失败，回退全部原球外观。"));
			Shutdown();
			return false;
		}
		TotalTriangles += Mesh->GetNumTriangles(0);
	}

	// 提交点：此前任何失败都没改过任何 proxy 的可见态；false 不传播给成功附属的 child。
	for (int32 Index = 0; Index < Proxies.Num(); ++Index)
	{
		Proxies[Index]->SetVisibility(false, false);
		HiddenProxies[Index] = true;
	}
	PreviousHeadPosition = Proxies[0]->GetComponentLocation();
	bHasPreviousHeadPosition = !PreviousHeadPosition.ContainsNaN();
	UE_LOG(LogAICompanionDemo, Display, TEXT("[WurmVisual] 头/身/尾外观已附属 %d 节，LOD0 合计 %lld 三角形；命中代理不变。"),
		GetActiveVisualCount(), TotalTriangles);
	return true;
}

void UWurmBodyVisual::Refresh(const TArray<FVector>& Positions, float DeltaSeconds)
{
	if (Positions.IsEmpty() || Positions.Num() != VisualComponents.Num())
	{
		return;
	}
	FVector HeadDirection = FVector::ZeroVector;
	if (!Positions[0].ContainsNaN() && bHasPreviousHeadPosition)
	{
		HeadDirection = Positions[0] - PreviousHeadPosition;
	}
	const float OrientationAlpha = 1.f - FMath::Exp(-WurmBodyVisualPrivate::OrientationResponse * FMath::Max(DeltaSeconds, 0.f));
	for (int32 Index = 0; Index < VisualComponents.Num(); ++Index)
	{
		UStaticMeshComponent* Visual = VisualComponents[Index];
		UStaticMeshComponent* Proxy = ProxyComponents[Index].Get();
		if (!IsValid(Visual) || !IsValid(Proxy) || Proxy->IsBeingDestroyed() || Positions[Index].ContainsNaN())
		{
			continue;
		}
		FVector Direction = FVector::ZeroVector;
		if (Index == 0)
		{
			Direction = HeadDirection;
			if (Direction.IsNearlyZero() && Positions.Num() > 1)
			{
				Direction = Positions[0] - Positions[1];
			}
		}
		else
		{
			// 中节用两邻节的中心切线，尾节朝前一节；所有 +X 始终指向头的方向。
			const int32 RearIndex = FMath::Min(Index + 1, Positions.Num() - 1);
			Direction = Positions[Index - 1] - Positions[RearIndex];
			if (Direction.IsNearlyZero())
			{
				Direction = Positions[Index - 1] - Positions[Index];
			}
		}
		if (Direction.ContainsNaN() || !Direction.Normalize())
		{
			continue;
		}
		const FQuat TargetRotation = Direction.ToOrientationQuat();
		Visual->SetWorldRotation(FQuat::Slerp(Visual->GetComponentQuat(), TargetRotation,
			OrientationAlpha));
	}
	PreviousHeadPosition = Positions[0];
	bHasPreviousHeadPosition = !PreviousHeadPosition.ContainsNaN();
}

void UWurmBodyVisual::TickCleanup()
{
	for (int32 Index = 0; Index < VisualComponents.Num(); ++Index)
	{
		UStaticMeshComponent* Proxy = ProxyComponents[Index].Get();
		UStaticMeshComponent* Visual = VisualComponents[Index];
		// UE 的父 DestroyComponent 会分离/改挂 child；必须显式销毁，不能靠附属关系清理。
		if (!IsValid(Proxy) || Proxy->IsBeingDestroyed() || !IsValid(Visual) ||
			Visual->IsBeingDestroyed() || Visual->GetAttachParent() != Proxy)
		{
			DestroyVisual(Index, true);
		}
	}
}

void UWurmBodyVisual::DestroyVisual(int32 Index, bool bRestoreProxy)
{
	if (VisualComponents.IsValidIndex(Index))
	{
		if (UStaticMeshComponent* Visual = VisualComponents[Index]; IsValid(Visual) && !Visual->IsBeingDestroyed())
		{
			Visual->DestroyComponent();
		}
		VisualComponents[Index] = nullptr;
	}
	if (ProxyComponents.IsValidIndex(Index))
	{
		UStaticMeshComponent* Proxy = ProxyComponents[Index].Get();
		if (bRestoreProxy && HiddenProxies.IsValidIndex(Index) && HiddenProxies[Index] &&
			OriginalProxyVisibility.IsValidIndex(Index) && IsValid(Proxy) && !Proxy->IsBeingDestroyed())
		{
			Proxy->SetVisibility(OriginalProxyVisibility[Index], false);
		}
		ProxyComponents[Index].Reset();
	}
	if (HiddenProxies.IsValidIndex(Index))
	{
		HiddenProxies[Index] = false;
	}
}

void UWurmBodyVisual::Shutdown()
{
	for (int32 Index = 0; Index < VisualComponents.Num(); ++Index)
	{
		DestroyVisual(Index, true);
	}
	VisualComponents.Empty();
	ProxyComponents.Empty();
	OriginalProxyVisibility.Empty();
	HiddenProxies.Empty();
	LoadedMeshes.Empty();
	LoadedMaterials.Empty();
	VisualOwner = nullptr;
	PreviousHeadPosition = FVector::ZeroVector;
	bHasPreviousHeadPosition = false;
}

int32 UWurmBodyVisual::GetActiveVisualCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<UStaticMeshComponent>& Visual : VisualComponents)
	{
		if (IsValid(Visual) && !Visual->IsBeingDestroyed() && Visual->IsRegistered())
		{
			++Count;
		}
	}
	return Count;
}
