/** [INPUT]: ShockArcActor、SplineComponent世界坐标点、NiagaraDataInterfaceSpline真实绑定优先级。
 * [OUTPUT]: 九点首尾连接两怪胸口；死亡/失效端点冻结最后坐标至0.35秒到期，有效User参数逐名绑定自有Spline。
 * [POS]: 单次短命显示；所有Niagara覆盖参数属于本组件，原System/DI不改。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md */
#include "ShockArcActor.h"
#include "AI/Combatant.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceSpline.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectHash.h"

namespace
{
	bool Living(const TWeakObjectPtr<AActor>& Actor)
	{
		ICombatant* Combatant=Actor.IsValid()?Cast<ICombatant>(Actor.Get()):nullptr;
		return Combatant && !Combatant->IsDead();
	}
	FVector Chest(AActor* Actor) { return Actor->GetActorLocation()+FVector(0,0,40); }
}

AShockArcActor::AShockArcActor()
{
	PrimaryActorTick.bCanEverTick=true;
	PrimaryActorTick.TickGroup=TG_PrePhysics;
	ArcPath=CreateDefaultSubobject<USplineComponent>(TEXT("ShockArcSpline"));
	SetRootComponent(ArcPath);
	ArcPath->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArcPath->SetGenerateOverlapEvents(false); ArcPath->SetCanEverAffectNavigation(false);
	Lightning=CreateDefaultSubobject<UNiagaraComponent>(TEXT("ShockArcNiagara"));
	Lightning->SetupAttachment(ArcPath); Lightning->SetAutoActivate(false);
	Lightning->SetCanEverAffectNavigation(false); Lightning->SetCastShadow(false);
	Lightning->AddTickPrerequisiteActor(this);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Asset(
		TEXT("/Game/_SplineVFX/NS/NS_Spline_ElectricLightning.NS_Spline_ElectricLightning"));
	if (Asset.Succeeded()) LightningSystem=Asset.Object;
}

bool AShockArcActor::BindSplineInterfaces()
{
	if (!LightningSystem) return false;
	bool Compatible=true;
	TArray<UObject*> Objects;
	GetObjectsWithOuter(LightningSystem,Objects,true);
	for (UObject* Object:Objects)
	{
		UNiagaraDataInterfaceSpline* DI=Cast<UNiagaraDataInterfaceSpline>(Object);
		if (!DI) continue;
		++SplineInterfaceCount;
		const FNiagaraVariable& Parameter=DI->SplineUserParameter.Parameter;
		UE_LOG(LogTemp,Log,TEXT("[ShockArc] DI=%s User=%s Valid=%d SoftSource=%s"),
			*DI->GetPathName(),*Parameter.GetName().ToString(),Parameter.IsValid(),*DI->SoftSourceActor.ToString());
		if (Parameter.IsValid())
		{
			// 绑定名只来自真实DI，绝不猜User.Spline之类不存在的参数。
			BindingNames.AddUnique(Parameter.GetName());
			Lightning->SetVariableObject(Parameter.GetName(),ArcPath);
		}
		else if (!DI->SoftSourceActor.IsNull()) Compatible=false;
		// 无User/SoftSource的DI按本机NiagaraDataInterfaceSpline.cpp:639取AttachmentRootActor的Spline。
	}
	return Compatible && SplineInterfaceCount>0;
}

bool AShockArcActor::Connect(AActor* From, AActor* To)
{
	FromActor=From; ToActor=To;
	if (!Living(FromActor) || !Living(ToActor) || !LightningSystem) return false;
	LastFromPoint=Chest(From); LastToPoint=Chest(To);
	bFromFrozen=false; bToFrozen=false;
	SetActorLocation((LastFromPoint+LastToPoint)*.5f);
	UpdatePath();
	Lightning->SetAsset(LightningSystem);
	bBindingReady=BindSplineInterfaces();
	if (!bBindingReady) return false;
	// 只设置实际存在且Float类型的既有参数；Extent/Jitter抑制游离电片，不改源资产。
	const FNiagaraParameterStore& Store=LightningSystem->GetExposedParameters();
	const auto SetFloat=[&](const TCHAR* Name,float Value)
	{
		const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(),FName(Name));
		if (Store.IndexOf(Parameter)!=INDEX_NONE) Lightning->SetVariableFloat(FName(Name),Value);
	};
	SetFloat(TEXT("User.Progress"),1.f);
	SetFloat(TEXT("User.Extent"),0.f);
	SetFloat(TEXT("User.Jitter"),.15f);
	Lightning->Activate(true);
	return true;
}

void AShockArcActor::UpdatePath()
{
	// ---------- 端点快照：敌人致死/销毁后保留完整本拍电弧 ----------
	if (!bFromFrozen)
	{
		if (Living(FromActor)) LastFromPoint=Chest(FromActor.Get());
		else bFromFrozen=true;
	}
	if (!bToFrozen)
	{
		if (Living(ToActor)) LastToPoint=Chest(ToActor.Get());
		else bToFrozen=true;
	}
	const FVector From=LastFromPoint,To=LastToPoint;
	SetActorLocation((From+To)*.5f);
	FVector Side=FVector::CrossProduct((To-From).GetSafeNormal(),FVector::UpVector).GetSafeNormal();
	if (Side.IsNearlyZero()) Side=FVector::RightVector;
	TArray<FVector> Points;
	for (int32 Index=0;Index<9;++Index)
	{
		const float T=Index/8.f;
		const float Envelope=(Index==0||Index==8)?0.f:1.f;
		const float Jitter=(Index%2?1.f:-1.f)*(9.f+3.f*FMath::Sin(Age*34.f+Index));
		Points.Add(FMath::Lerp(From,To,T)+Envelope*(Side*Jitter+FVector::UpVector*FMath::Sin(Index*1.7f+Age*29.f)*5.f));
	}
	ArcPath->SetSplinePoints(Points,ESplineCoordinateSpace::World,false);
	for (int32 Index=0;Index<9;++Index) ArcPath->SetSplinePointType(Index,ESplinePointType::Linear,false);
	ArcPath->UpdateSpline();
}

void AShockArcActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetWorld()->IsPaused()||UGameplayStatics::GetGlobalTimeDilation(this)<.01f) return;
	Age+=DeltaSeconds; Remaining-=DeltaSeconds;
	if (Remaining<=0.f) { Destroy(); return; }
	UpdatePath();
}
void AShockArcActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Lightning) { Lightning->DeactivateImmediate(); Lightning->DestroyComponent(); }
	FromActor.Reset(); ToActor.Reset(); Super::EndPlay(Reason);
}
FString AShockArcActor::DebugArcState() const
{
	const FVector A=ArcPath->GetLocationAtSplinePoint(0,ESplineCoordinateSpace::World);
	const FVector B=ArcPath->GetLocationAtSplinePoint(ArcPath->GetNumberOfSplinePoints()-1,ESplineCoordinateSpace::World);
	FString Names;
	for (const FName Name:BindingNames) { if (!Names.IsEmpty()) Names+=TEXT(";"); Names+=Name.ToString(); }
	return FString::Printf(TEXT("{\"points\":%d,\"remaining\":%.4f,\"active\":%s,\"binding_ready\":%s,\"spline_di_count\":%d,\"user_bindings\":\"%s\",\"root_spline_fallback\":%s,\"start\":[%.3f,%.3f,%.3f],\"end\":[%.3f,%.3f,%.3f],\"start_error\":%.5f,\"end_error\":%.5f,\"from_frozen\":%s,\"to_frozen\":%s}"),
		ArcPath->GetNumberOfSplinePoints(),Remaining,Lightning&&Lightning->IsActive()?TEXT("true"):TEXT("false"),bBindingReady?TEXT("true"):TEXT("false"),
		SplineInterfaceCount,*Names,BindingNames.Num()==0?TEXT("true"):TEXT("false"),A.X,A.Y,A.Z,B.X,B.Y,B.Z,
		FVector::Dist(A,LastFromPoint),FVector::Dist(B,LastToPoint),
		bFromFrozen?TEXT("true"):TEXT("false"),bToFrozen?TEXT("true"):TEXT("false"));
}
