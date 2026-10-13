/**
 * [INPUT]: 预Cook的NS_ColoredFireMid、User.Flame_Material材质与User.Flame_Color颜色参数、独立MI_EnrageFlame、Niagara组件参数/暂停API。
 * [OUTPUT]: 实例参数绑定紫红材质，每宿主两份贴身火焰；14分钟敌人和队友紫炎共用Z高度1.8倍，生命来源死亡/失效即清理，暂停冻结。
 * [POS]: 共享身体覆盖层独立于狂暴来源/伤害与寒霜/猎印；显式外部Health仅提供无Health队友的退场边界，不改变原敌人规则。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "EnemyEnrageVisual.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float HeightenedScale = 1.8f;
	const FName FlameColorName(TEXT("User.Flame_Color"));
	const FName FlameMaterialName(TEXT("User.Flame_Material"));
	FNiagaraVariable ColorParameter() { return FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), FlameColorName); }
}

UEnemyEnrageVisual::UEnemyEnrageVisual()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fire(TEXT("/Game/VFX/Enrage/NS_ColoredFireMid.NS_ColoredFireMid"));
	if (Fire.Succeeded()) SourceFlameSystem = Fire.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Purple(TEXT("/Game/VFX/Enrage/MI_EnrageFlame.MI_EnrageFlame"));
	if (Purple.Succeeded()) EnrageMaterial = Purple.Object;
}

bool UEnemyEnrageVisual::BuildFlames()
{
	bSafeColoredSystem = false;
	bCompatibleColor = false;
	if (!GetWorld() || !GetOwner() || !GetOwner()->GetRootComponent()) return false;
	if (!SourceFlameSystem || !EnrageMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] 缺预Cook的NS_ColoredFireMid或MI_EnrageFlame，拒绝显示未绑定火焰。system=%s material=%s"),
			SourceFlameSystem ? TEXT("true") : TEXT("false"), EnrageMaterial ? TEXT("true") : TEXT("false"));
		return false;
	}
	FlameSystem = SourceFlameSystem;
	const FNiagaraParameterStore& Parameters = FlameSystem->GetExposedParameters();
	bCompatibleColor = Parameters.IndexOf(ColorParameter()) != INDEX_NONE;
	const bool bCompatibleMaterial = Parameters.IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetUMaterialDef(), FlameMaterialName)) != INDEX_NONE;
	if (!bCompatibleColor || !bCompatibleMaterial)
	{
		TArray<FNiagaraVariable> Variables;
		Parameters.GetParameters(Variables);
		FString Actual;
		for (const FNiagaraVariable& Variable : Variables) Actual += Variable.GetName().ToString() + TEXT(":") + Variable.GetType().GetName() + TEXT(";");
		UE_LOG(LogTemp, Error, TEXT("[EnrageVisual] NS_ColoredFireMid缺必需用户参数或类型不符，拒绝显示。User.Flame_Color(Color)=%s User.Flame_Material(MaterialInterface)=%s actual=%s"),
			bCompatibleColor ? TEXT("true") : TEXT("false"), bCompatibleMaterial ? TEXT("true") : TEXT("false"), *Actual);
		return false;
	}
	bSafeColoredSystem = true;
	// 基于胶囊坐标附着；继承变体体型，脚边与胸部两股火焰覆盖整个人形。
	const FVector Positions[] = { FVector(0, 0, -75), FVector(0, 0, -5) };
	for (const FVector& Position : Positions)
	{
		UNiagaraComponent* Flame = NewObject<UNiagaraComponent>(GetOwner());
		Flame->SetAutoActivate(false);
		Flame->SetAutoDestroy(false);
		Flame->SetAsset(FlameSystem);
		// SetAsset会重置默认参数，必须随后绑定实例材质，再注册/激活组件。
		Flame->SetVariableMaterial(FlameMaterialName, EnrageMaterial);
		Flame->SetupAttachment(GetOwner()->GetRootComponent());
		Flame->SetRelativeLocation(Position);
		Flame->SetRelativeScale3D(FVector(FlameScale, FlameScale, FlameScale * (bHeightened ? HeightenedScale : 1.f)));
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
	const UHealthComponent* Health = GetLifeSource();
	bInEnraged = bInEnraged && Health && !Health->IsDead() && Health->GetCurrentHealth() > 0.f;
	if (bInEnraged == bEnraged) return;
	bEnraged = bInEnraged;
	if (bEnraged) BuildFlames(); else DestroyFlames();
	SetComponentTickEnabled(bEnraged);
}

void UEnemyEnrageVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	const UHealthComponent* Health = GetLifeSource();
	if (!Health || Health->IsDead() || Health->GetCurrentHealth() <= 0.f) { SetEnraged(false); return; }
	const bool bFreeze = GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f;
	for (UNiagaraComponent* Flame : Flames) if (IsValid(Flame)) Flame->SetPaused(bFreeze);
}

void UEnemyEnrageVisual::SetLifeSource(UHealthComponent* Health)
{
	bExternalLifeSource = true;
	LifeSource = Health;
	if (!Health || Health->IsDead()) SetEnraged(false);
}

UHealthComponent* UEnemyEnrageVisual::GetLifeSource() const
{
	return bExternalLifeSource ? LifeSource.Get()
		: GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
}

void UEnemyEnrageVisual::SetHeightened(bool bInHeightened)
{
	bInHeightened = bInHeightened && bEnraged;
	if (bHeightened == bInHeightened) return;
	bHeightened = bInHeightened;
	for (UNiagaraComponent* Flame : Flames)
		if (IsValid(Flame)) Flame->SetRelativeScale3D(FVector(FlameScale, FlameScale, FlameScale * (bHeightened ? HeightenedScale : 1.f)));
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
	FVector ActualScale = FVector::ZeroVector;
	FString Materials;
	for (const UNiagaraComponent* Flame : Flames)
	{
		if (!IsValid(Flame)) continue;
		++Live;
		ActualScale = Flame->GetRelativeScale3D();
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
	return FString::Printf(TEXT("{\"heightened\":%s,\"actualScale\":[%.4f,%.4f,%.4f],\"enraged\":%s,\"systemLoaded\":%s,\"compatibleColor\":%s,\"safeColoredSystem\":%s,\"enrageMaterialLoaded\":%s,\"liveFlameComponents\":%d,\"activeFlameComponents\":%d,\"pausedFlameComponents\":%d,\"buildCount\":%d,\"actualColor\":[%.4f,%.4f,%.4f,%.4f],\"usedMaterials\":[%s]}"),
		bHeightened ? TEXT("true") : TEXT("false"), ActualScale.X, ActualScale.Y, ActualScale.Z,
		bEnraged ? TEXT("true") : TEXT("false"), SourceFlameSystem ? TEXT("true") : TEXT("false"), bCompatibleColor ? TEXT("true") : TEXT("false"),
		bSafeColoredSystem ? TEXT("true") : TEXT("false"), EnrageMaterial ? TEXT("true") : TEXT("false"),
		Live, Active, Paused, BuildCount, Actual.R, Actual.G, Actual.B, Actual.A, *Materials);
}
