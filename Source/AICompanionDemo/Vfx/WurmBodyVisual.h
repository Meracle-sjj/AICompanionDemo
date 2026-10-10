/**
 * [INPUT]: 宿主在游戏线程提供 AActor 与按头到尾排序的 Sphere 代理；代理的位置、半径、缩放、碰撞与死亡碎屑生命周期仍由宿主维护；/Game/Bosses/Wurm 的头/身/尾网格及玄武岩/岩浆材质
 * [OUTPUT]: UWurmBodyVisual——纯外观附属层；一次初始化加载五项资源并按 Basalt/MagmaCore 槽名绑定；全套成功后只隐藏代理自身，缺资源/缺槽/建树失败完整回退原球；Refresh 按 DeltaSeconds 做帧率无关的外观转向，TickCleanup 独立销毁失效代理对应的外观
 * [POS]: Vfx/ 蠕虫外观 helper，不依赖 BossWurm 或任何战斗/状态机类型；宿主须用 UPROPERTY 持有它，在 Segments 清空后的死亡 Tick 仍调用 TickCleanup，并在 EndPlay 调 Shutdown；网格以原点为 pivot、+X 为前、顶点最大半径不超过基础 Sphere 的 50cm，附属相对位置零/缩放一
 * [PROTOCOL]: 变更时同步本文件头部，然后检查 ../AGENTS.md；资源契约见 /Content/Bosses/Wurm/AGENTS.md，宿主生命周期契约见 AI/BossWurm
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WurmBodyVisual.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** 原 Sphere 是命中代理与运动学真源；本类的附属网格仅负责显示。 */
UCLASS()
class AICOMPANIONDEMO_API UWurmBodyVisual : public UObject
{

	GENERATED_BODY()

public:
	/** 可重入：先关闭旧外观；所有资源、槽名与组件准备成功后才隐藏原代理。 */
	bool Initialize(AActor* Owner, const TArray<UStaticMeshComponent*>& Proxies);

	/** Positions 必须与初始化代理同序同数量；仅按 DeltaSeconds 平滑外观转向，空数组/数量不符不更新。 */
	void Refresh(const TArray<FVector>& Positions, float DeltaSeconds);

	/** 独立于 Refresh：死亡移交后即使 Segments.Empty，仍须逐 Tick 调用以销毁孤立外观。 */
	void TickCleanup();

	/** 幂等：销毁附属网格、恢复仍有效代理的原可见态、释放所有 UObject 引用。 */
	void Shutdown();

	/** 统计实际仍有效且已注册的外观组件，方便宿主验收死亡清理。 */
	UFUNCTION(BlueprintPure, Category = "Wurm|Visual")
	int32 GetActiveVisualCount() const;

private:
	void DestroyVisual(int32 Index, bool bRestoreProxy);

	/** UObject 与 Actor 的相互引用由 UE GC 追踪；EndPlay/Shutdown 显式断开。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> VisualOwner;

	/** 固定角色顺序：头、身、尾；资源在 Initialize 加载，Refresh/TickCleanup 不加载。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;

	/** 固定材质角色：Basalt、MagmaCore；槽索引仍通过每个 mesh 的 GetMaterialIndex 获得。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;

	/** 弱代理不延长已销毁体节寿命；保持槽位顺序，清理时不 RemoveAt/换位。 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UStaticMeshComponent>> ProxyComponents;

	/** Actor OwnedComponents + UPROPERTY 双重保活，父代理销毁不会自动销毁 child。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> VisualComponents;

	TArray<bool> OriginalProxyVisibility;
	TArray<bool> HiddenProxies;
	FVector PreviousHeadPosition = FVector::ZeroVector;
	bool bHasPreviousHeadPosition = false;
};
