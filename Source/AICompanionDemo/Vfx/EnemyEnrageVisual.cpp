/**
 * [INPUT]: FireMid暴露参数、独立MI_EnrageFlame（DepthFade输出仅R遮罩×EnrageTint）、Niagara深复制/暂停API。
 * [OUTPUT]: 每世界共享一个纯运行时系统副本，只改安全副本的火焰Renderer材质；每杂兵两份紫红焰，幂等清理。
 * [POS]: 身体覆盖层独立于寒霜染色和猎印，显示变化不改移动/碰撞/元素契约。
 * [PROTOCOL]: 变更时更新此头部；成员地图由主agent统一维护。
 */
#include "EnemyEnrageVisual.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName FlameColorName(TEXT("User.Flame_Color"));
	FNiagaraVariable ColorParameter() { return FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), FlameColorName); }
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UNiagaraSystem>> ColoredSystemCache;

	UNiagaraSystem* ColoredSystem(UWorld* World, UNiagaraSystem* Source, UMaterialInterface* Material)
	{
		for (auto It=ColoredSystemCache.CreateIterator(); It; ++It)
			if (!It.Key().IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
		if (const TWeakObjectPtr<UNiagaraSystem>* Cached = ColoredSystemCache.Find(World)) return Cached->Get();
		if (!Source || !Material) return nullptr;
		UNiagaraSystem* Copy = DuplicateObject<UNiagaraSystem>(Source, GetTransientPackage());
		if (!Copy) return nullptr;
		Copy->ClearFlags(RF_Public | RF_Standalone);
		Copy->SetFlags(RF_Transient);
		int32 Replaced = 0;
		// 必须先完整检查，再修改任何Renderer；外部共享Emitter/Renderer绝不写入。
		for (FNiagaraEmitterHandle& Handle : Copy->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
			FVersionedNiagaraEmitterData* Data = Instance.GetEmitterData();
			if (!Instance.Emitter || !Instance.Emitter->IsIn(Copy) || !Data)
			{
				UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] 深复制Emitter归属异常，拒绝修改外部源资产"));
				return nullptr;
			}
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
				if (!Renderer || !Renderer->IsIn(Copy))
				{
					UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] 深复制Renderer归属异常，拒绝修改外部源资产"));
					return nullptr;
				}
		}
		for (FNiagaraEmitterHandle& Handle : Copy->GetEmitterHandles())
		{
			for (UNiagaraRendererProperties* Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
			{
				UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
				if (!Sprite || !Sprite->Material || !Sprite->Material->GetPathName().Contains(TEXT("MI_FireMid_Loop_01_Flame"))) continue;
				UE_LOG(LogTemp, Log, TEXT("[EnrageVisual] 替换副本材质=%s，旧User绑定=%s"),
					*Sprite->Material->GetPathName(), *Sprite->MaterialUserParamBinding.Parameter.GetName().ToString());
				// 即使源资源日后增加用户材质绑定，也不得越过专用紫红材质。
				Sprite->MaterialUserParamBinding.Parameter.SetName(NAME_None);
				Sprite->Material = Material;
#if WITH_EDITORONLY_DATA
				Sprite->MICMaterial = nullptr; // 编辑器衍生MIC优先于Material，清掉副本旧缓存才会真实使用新材质。
#endif
				++Replaced;
			}
		}
		if (Replaced == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] 未找到FireMid火焰Renderer，拒绝显示橙火"));
			return nullptr;
		}
		// PostDuplicate在替换材质前已建立非反射缓存；只为安全副本更新布局/边界后再激活。
		Copy->CacheFromCompiledData();
		ColoredSystemCache.Add(World, Copy);
		UE_LOG(LogTemp, Log, TEXT("[EnrageVisual] 世界共享安全副本，替换%d个火焰Renderer为%s"), Replaced, *Material->GetPathName());
		return Copy;
	}
}

UEnemyEnrageVisual::UEnemyEnrageVisual()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fire(TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireMid_Loop_001.NS_Sub_FireMid_Loop_001"));
	if (Fire.Succeeded()) SourceFlameSystem = Fire.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Purple(TEXT("/Game/VFX/Enrage/MI_EnrageFlame.MI_EnrageFlame"));
	if (Purple.Succeeded()) EnrageMaterial = Purple.Object;
}

bool UEnemyEnrageVisual::BuildFlames()
{
	if (!SourceFlameSystem || !GetOwner() || !GetOwner()->GetRootComponent()) return false;
	FlameSystem = ColoredSystem(GetWorld(), SourceFlameSystem, EnrageMaterial);
	bSafeColoredSystem = FlameSystem != nullptr;
	if (!bSafeColoredSystem) return false;
	const FNiagaraParameterStore& Parameters = FlameSystem->GetExposedParameters();
	bCompatibleColor = Parameters.IndexOf(ColorParameter()) != INDEX_NONE;
	if (!bCompatibleColor)
	{
		TArray<FNiagaraVariable> Variables;
		Parameters.GetParameters(Variables);
		FString Actual;
		for (const FNiagaraVariable& Variable : Variables) Actual += Variable.GetName().ToString() + TEXT(":") + Variable.GetType().GetName() + TEXT(";");
		UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] FireMid缺Color类型User.Flame_Color，拒绝橙火假成功。actual=%s"), *Actual);
		return false;
	}
	// 基于胶囊坐标附着；继承变体体型，脚边与胸部两股火焰覆盖整个人形。
	const FVector Positions[] = { FVector(0, 0, -75), FVector(0, 0, -5) };
	for (const FVector& Position : Positions)
	{
		UNiagaraComponent* Flame = NewObject<UNiagaraComponent>(GetOwner());
		Flame->SetAutoActivate(false);
		Flame->SetAutoDestroy(false);
		Flame->SetAsset(FlameSystem);
		Flame->SetupAttachment(GetOwner()->GetRootComponent());
		Flame->SetRelativeLocation(Position);
		Flame->SetRelativeScale3D(FVector(FlameScale));
		Flame->SetCanEverAffectNavigation(false);
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->SetVariableLinearColor(FlameColorName, FlameColor);
		// 参数确实存在且类型相同才覆写；减少群怪的烟幕与飞散火星，保留主体火焰。
		const FName QuietParameters[] = { FName(TEXT("User.Spark_Rate")), FName(TEXT("User.Smoke Intensity")) };
		for (const FName& Name : QuietParameters)
			if (Parameters.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), Name)) != INDEX_NONE) Flame->SetVariableFloat(Name, 0.f);
		Flame->RegisterComponent();
		Flames.Add(Flame);
		Flame->Activate(true);
	}
	++BuildCount;
	return Flames.Num() == 2;
}

void UEnemyEnrageVisual::SetEnraged(bool bInEnraged)
{
	const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
	bInEnraged = bInEnraged && Health && !Health->IsDead();
	if (bInEnraged == bEnraged) return;
	bEnraged = bInEnraged;
	if (bEnraged) BuildFlames(); else DestroyFlames();
	SetComponentTickEnabled(bEnraged);
}

void UEnemyEnrageVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Health || Health->IsDead()) { SetEnraged(false); return; }
	const bool bFreeze = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
	for (UNiagaraComponent* Flame : Flames) if (IsValid(Flame)) Flame->SetPaused(bFreeze);
}

void UEnemyEnrageVisual::DestroyFlames()
{
	for (UNiagaraComponent* Flame : Flames)
	{
		if (!IsValid(Flame)) continue;
		Flame->DeactivateImmediate();
		Flame->DestroyComponent();
	}
	Flames.Reset();
}

void UEnemyEnrageVisual::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnraged = false;
	DestroyFlames();
	Super::EndPlay(Reason);
}

FString UEnemyEnrageVisual::DebugVisualState() const
{
	int32 Live=0, Active=0, Paused=0;
	FLinearColor Actual = FLinearColor::Black;
	FString Materials;
	for (const UNiagaraComponent* Flame : Flames)
	{
		if (!IsValid(Flame)) continue;
		++Live;
		if (Flame->IsActive()) ++Active;
		if (Flame->IsPaused()) ++Paused;
		Actual = Flame->GetOverrideParameters().GetParameterValueOrDefault<FLinearColor>(ColorParameter(), FLinearColor::Black);
		// 读取真实SystemInstance材质（包含用户绑定/MID），不能用Renderer字段或颜色参数冒充实图。
		TArray<UMaterialInterface*> Used;
		Flame->GetUsedMaterials(Used, false);
		for (const UMaterialInterface* Material : Used)
		{
			if (!Material) continue;
			FLinearColor Tint = FLinearColor::Black;
			const bool bHasTint = Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EnrageTint")), Tint);
			if (!Materials.IsEmpty()) Materials += TEXT(",");
			Materials += FString::Printf(TEXT("{\"path\":\"%s\",\"hasEnrageTint\":%s,\"tint\":[%.4f,%.4f,%.4f,%.4f]}"),
				*Material->GetPathName(), bHasTint ? TEXT("true") : TEXT("false"), Tint.R, Tint.G, Tint.B, Tint.A);
		}
	}
	return FString::Printf(TEXT("{\"enraged\":%s,\"systemLoaded\":%s,\"compatibleColor\":%s,\"safeColoredSystem\":%s,\"enrageMaterialLoaded\":%s,\"liveFlameComponents\":%d,\"activeFlameComponents\":%d,\"pausedFlameComponents\":%d,\"buildCount\":%d,\"actualColor\":[%.4f,%.4f,%.4f,%.4f],\"usedMaterials\":[%s]}"),
		bEnraged ? TEXT("true") : TEXT("false"), SourceFlameSystem ? TEXT("true") : TEXT("false"), bCompatibleColor ? TEXT("true") : TEXT("false"),
		bSafeColoredSystem ? TEXT("true") : TEXT("false"), EnrageMaterial ? TEXT("true") : TEXT("false"),
		Live, Active, Paused, BuildCount, Actual.R, Actual.G, Actual.B, Actual.A, *Materials);
}
