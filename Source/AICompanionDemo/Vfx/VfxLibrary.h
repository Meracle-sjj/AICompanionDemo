/**
 * [INPUT]: 依赖 Engine 的 AActor/SpawnActor（蓝图类加载 _C 后缀）、Niagara 的 UNiagaraFunctionLibrary/UNiagaraComponent（NS直接播放及可选生命周期回传）
 * [OUTPUT]: 对外提供 UVfxLibrary——视觉特效统一入口（09-30 特效路线升级：Fab 三包免费 Niagara/BP 资产的 C++ 挂接层）：
 *  SpawnVfx返回样条Actor；可选OutNiagaraComponent回传爆炸组件，旧调用不变。Magic通用映射保留，猎杀/钥匙不依赖它。
 *  样条系走BP_SplineVFX_X，类缓存用弱引用并在GC/地图重载后按原路径重新加载；爆炸系走NS_Sub_EXP的自动销毁组件，需宿主提前清理时用回传组件登记。
 * [POS]: Vfx/ 特效基建唯一文件（与 Audio/SFXLibrary 对称：事件方一行挂接，素材路径在库内集中管理）；Boss 波 1 起全面推广
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VfxLibrary.generated.h"

class UWorld;
class UNiagaraComponent;

/** 视觉特效清单（Fab 三包已入库：_SplineVFX / F_FreeFlameFx_Pack / Fire_EXP_Vol01_Free） */
UENUM(BlueprintType)
enum class EVfx : uint8
{
	Magic,       // 通用金光（BP_SplineVFX_Magic；猎杀/钥匙现役显示不依赖此项）
	Electric,    // 电弧（BP_SplineVFX_ElectricLightning——感电/妖术师蓄力）
	Frost,       // 冰霜（BP_SplineVFX_Frost——冰弹命中）
	Fire,        // 灼烧（BP_SplineVFX_Fire——灼烧 DoT/火系）
	Holy,        // 神圣（BP_SplineVFX_Holy——治疗系视觉备用）
	Explosion,   // 爆炸（NS_Sub_EXP_Small——溅射/命中爆发）
	ExplosionLarge // 大爆炸（NS_Sub_EXP_Large——蠕虫落点爆：球形爆炸空间主视觉，10-06 七批）
};

UCLASS()
class AICOMPANIONDEMO_API UVfxLibrary : public UObject
{
	GENERATED_BODY()

public:
	/** 样条返回Actor，LifeSpan>0自动定时销毁、0由调用方管理；爆炸返回nullptr并保持Niagara自身AutoDestroy。
	 *  可选OutNiagaraComponent入口先置null，仅爆炸成功时回传真实组件，供宿主死亡/EndPlay提前清理。
	 *  资产缺失静默返回nullptr；不改变既有调用的参数或正常寿命。 */
	static AActor* SpawnVfx(UWorld* World, EVfx Kind, const FVector& Location, float LifeSpan = 0.4f,
		float Scale = 1.f, UNiagaraComponent** OutNiagaraComponent = nullptr);

	/** 开局预载五种样条和两种爆炸，提前走首用同步加载路径以减轻战斗中的首次加载。
	 *  GameMode::StartRun 调用；不使资源永久常驻，后续 GC 回收的样条类由 SpawnVfx 重新加载。 */
	static void PrecacheVfx(UWorld* World);
};
