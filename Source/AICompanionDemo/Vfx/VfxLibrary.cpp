/**
 * [INPUT]: 依赖 VfxLibrary.h 的入口契约、Engine 的 SpawnActor（蓝图类 _C 路径加载）、TWeakObjectPtr 的 GC 失效检测、Niagara 的 UNiagaraFunctionLibrary（NS 直接播放——Explosion 系非样条无 BP 包装）
 * [OUTPUT]: 样条类弱缓存失效时按原路径重新加载后SpawnActor+LifeSpan，避免跨地图GC后使用悬空类；爆炸SpawnSystemAtLocation保持AutoDestroy，可选回传真实Niagara组件供宿主提前清理
 * [POS]: Vfx/ 特效基建实现文件，与 VfxLibrary.h 一体两面；业务文件只认 SpawnVfx 一行，素材换包只改本文件路径表
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */

#include "VfxLibrary.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace
{
	// 样条系蓝图类路径（BP_SplineVFX_X = Actor+Spline+NS 完整装配，spawn 即用——Fab Free Spline VFX 包）
	const TCHAR* SplineBpPath(EVfx Kind)
	{
		switch (Kind)
		{
		case EVfx::Magic:     return TEXT("/Game/_SplineVFX/_GenericSource/BP/BP_SplineVFX_Magic.BP_SplineVFX_Magic_C");
		case EVfx::Electric:  return TEXT("/Game/_SplineVFX/_GenericSource/BP/BP_SplineVFX_ElectricLightning.BP_SplineVFX_ElectricLightning_C");
		case EVfx::Frost:     return TEXT("/Game/_SplineVFX/_GenericSource/BP/BP_SplineVFX_Frost.BP_SplineVFX_Frost_C");
		case EVfx::Fire:      return TEXT("/Game/_SplineVFX/_GenericSource/BP/BP_SplineVFX_Fire.BP_SplineVFX_Fire_C");
		case EVfx::Holy:      return TEXT("/Game/_SplineVFX/_GenericSource/BP/BP_SplineVFX_Holy.BP_SplineVFX_Holy_C");
		default:              return nullptr;
		}
	}
}

void UVfxLibrary::PrecacheVfx(UWorld* World)
{
	if (!World)
	{
		return;
	}

	// 预载=地下无人处闪现 0.01s，提前走本世界首用加载路径；GC 后的类缓存由 SpawnVfx 重验并重载。
	for (int32 K = 0; K < static_cast<int32>(EVfx::Explosion); ++K)
	{
		SpawnVfx(World, static_cast<EVfx>(K), FVector(0.f, 0.f, -100000.f), 0.01f, 0.01f);
	}
	LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Small_002.NS_Sub_EXP_Small_002"));
	LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Large_001_01.NS_Sub_EXP_Large_001_01"));
}

AActor* UVfxLibrary::SpawnVfx(UWorld* World, EVfx Kind, const FVector& Location, float LifeSpan,
	float Scale, UNiagaraComponent** OutNiagaraComponent)
{
	if (OutNiagaraComponent) { *OutNiagaraComponent = nullptr; }
	if (!World)
	{
		return nullptr;
	}

	// 爆炸系：非样条、无 BP 包装——NS 直接定点播放（Fire_EXP Vol.1 免费包）
	if (Kind == EVfx::Explosion || Kind == EVfx::ExplosionLarge)
	{
		const TCHAR* NSPath = Kind == EVfx::ExplosionLarge
			? TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Large_001_01.NS_Sub_EXP_Large_001_01")
			: TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Small_002.NS_Sub_EXP_Small_002");
		if (UNiagaraSystem* NS = LoadObject<UNiagaraSystem>(nullptr, NSPath))
		{
			UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, NS, Location, FRotator::ZeroRotator,
				FVector(Scale), true, true);
			if (OutNiagaraComponent) { *OutNiagaraComponent = Component; }
		}
		return nullptr;   // 正常AutoDestroy；选择回传的宿主可在死亡/EndPlay时提前销毁。
	}

	// 静态缓存跨地图存在，不能保存不受 GC 跟踪的裸类指针；弱引用失效后重新加载。
	static TMap<EVfx, TWeakObjectPtr<UClass>> BpCache;
	const TWeakObjectPtr<UClass>* CachedClass = BpCache.Find(Kind);
	UClass* BpClass = CachedClass ? CachedClass->Get() : nullptr;
	if (!BpClass)
	{
		if (const TCHAR* Path = SplineBpPath(Kind))
		{
			BpClass = LoadClass<AActor>(nullptr, Path);
			if (BpClass)
			{
				BpCache.Add(Kind, TWeakObjectPtr<UClass>(BpClass));
			}
		}
	}
	if (BpClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* VfxActor = World->SpawnActor<AActor>(BpClass, Location, FRotator::ZeroRotator, Params);
		if (VfxActor)
		{
			VfxActor->SetActorScale3D(FVector(Scale));
			if (LifeSpan > 0.f)
			{
				VfxActor->SetLifeSpan(LifeSpan);
			}
		}
		return VfxActor;
	}
	return nullptr;
}
