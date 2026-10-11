/**
 * [INPUT]: ChestGrounding.h、StaticMeshComponent::GetLocalBounds、根Transform与WorldStatic实际探地。
 * [OUTPUT]: 两轮九点坡面采样、局部Z顺平均法线、实际底面最小支撑修正及3–12cm埋沙；诊断真实bounds/法线/底面间隙。
 * [POS]: 宝箱显示层地形接触工具；只改变箱体旋转与Z，不改变XY摆点或业务资格。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "ChestGrounding.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"

namespace
{
	bool TraceFloor(const AActor* Chest, const FVector& Position, FHitResult& Hit)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ChestMeshGround), false, Chest);
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		return Chest->GetWorld()->LineTraceSingleByObjectType(Hit,
			Position + FVector(0.f, 0.f, 8000.f), Position - FVector(0.f, 0.f, 16000.f), Objects, Query);
	}

	TArray<FVector> BottomSamples(const FVector& Min, const FVector& Max)
	{
		TArray<FVector> Samples;
		Samples.Reserve(9);
		for (int32 Y = 0; Y < 3; ++Y)
			for (int32 X = 0; X < 3; ++X)
				Samples.Add(FVector(FMath::Lerp(Min.X, Max.X, X * .5f),
					FMath::Lerp(Min.Y, Max.Y, Y * .5f), Min.Z));
		return Samples;
	}
}

bool ChestGrounding::Place(AActor* Chest, UStaticMeshComponent* Body, FChestGroundContact& Contact)
{
	Contact = FChestGroundContact();
	if (!Chest || !Chest->GetWorld() || !Body || !Body->GetStaticMesh()) { return false; }
	Body->GetLocalBounds(Contact.LocalMin, Contact.LocalMax);
	if (Contact.LocalMin.ContainsNaN() || Contact.LocalMax.ContainsNaN()
		|| (Contact.LocalMax - Contact.LocalMin).GetMin() <= UE_SMALL_NUMBER) { return false; }
	FHitResult Center;
	if (!TraceFloor(Chest, Chest->GetActorLocation(), Center) || Center.ImpactNormal.Z < .25f) { return false; }
	const FRotator OldRotation = Chest->GetActorRotation();
	const FVector OldLocation = Chest->GetActorLocation();
	const FVector YawForward = FRotator(0.f, OldRotation.Yaw, 0.f).Vector();
	const TArray<FVector> Samples = BottomSamples(Contact.LocalMin, Contact.LocalMax);
	FVector Normal = Center.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	// 第一轮按中心坡面铺足迹；第二轮使用实际足迹九点法线，以避免边角沿旧水平面取样。
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Chest->SetActorRotation(FRotationMatrix::MakeFromZX(Normal, YawForward).Rotator());
		FVector NormalSum = FVector::ZeroVector;
		int32 Hits = 0;
		for (const FVector& Local : Samples)
		{
			FHitResult Hit;
			if (TraceFloor(Chest, Body->GetComponentTransform().TransformPosition(Local), Hit) && Hit.ImpactNormal.Z >= .25f)
			{
				NormalSum += Hit.ImpactNormal;
				++Hits;
			}
		}
		if (Hits != Samples.Num())
		{
			Chest->SetActorRotation(OldRotation);
			return false;
		}
		Normal = NormalSum.GetSafeNormal(UE_SMALL_NUMBER, Normal);
	}
	Chest->SetActorRotation(FRotationMatrix::MakeFromZX(Normal, YawForward).Rotator());
	Contact.GroundNormal = Normal;
	Contact.GroundPoint = Center.ImpactPoint;
	const FTransform Transform = Body->GetComponentTransform();
	float LowestCorrection = MAX_flt;
	TArray<float> Corrections;
	for (const FVector& Local : Samples)
	{
		const FVector Bottom = Transform.TransformPosition(Local);
		FHitResult Hit;
		if (TraceFloor(Chest, Bottom, Hit))
		{
			const float Correction = static_cast<float>(Hit.ImpactPoint.Z - Bottom.Z);
			Corrections.Add(Correction);
			LowestCorrection = FMath::Min(LowestCorrection, Correction);
		}
	}
	Contact.HitSamples = Corrections.Num();
	if (Contact.HitSamples != Samples.Num() || !FMath::IsFinite(LowestCorrection))
	{
		Chest->SetActorRotation(OldRotation);
		return false;
	}
	const float Height = static_cast<float>((Contact.LocalMax.Z - Contact.LocalMin.Z) * FMath::Abs(Transform.GetScale3D().Z));
	Contact.BurialDepth = FMath::Clamp(Height * .06f, 3.f, 12.f);
	const float Shift = LowestCorrection - Contact.BurialDepth;
	Chest->SetActorLocation(OldLocation + FVector(0.f, 0.f, Shift));
	Contact.MaxBottomGap = -MAX_flt;
	for (const float Correction : Corrections)
	{
		const float Gap = Shift - Correction;
		Contact.MaxBottomGap = FMath::Max(Contact.MaxBottomGap, Gap);
		Contact.MaxBottomBurial = FMath::Max(Contact.MaxBottomBurial, -Gap);
	}
	Contact.bGrounded = true;
	return true;
}

FVector ChestGrounding::MarkerAnchor(const UStaticMeshComponent* Body, float HeightAbove)
{
	if (!Body || !Body->GetStaticMesh()) { return FVector::ZeroVector; }
	FVector Min, Max;
	Body->GetLocalBounds(Min, Max);
	FBox WorldBox(ForceInit);
	for (int32 Z = 0; Z < 2; ++Z)
		for (int32 Y = 0; Y < 2; ++Y)
			for (int32 X = 0; X < 2; ++X)
				WorldBox += Body->GetComponentTransform().TransformPosition(FVector(X ? Max.X : Min.X, Y ? Max.Y : Min.Y, Z ? Max.Z : Min.Z));
	const FVector Center = WorldBox.GetCenter();
	return FVector(Center.X, Center.Y, WorldBox.Max.Z + HeightAbove);
}

FString FChestGroundContact::ToJson() const
{
	return FString::Printf(TEXT("{\"grounded\":%s,\"hitSamples\":%d,\"localMin\":[%.3f,%.3f,%.3f],\"localMax\":[%.3f,%.3f,%.3f],\"ground\":[%.3f,%.3f,%.3f],\"normal\":[%.5f,%.5f,%.5f],\"burialDepth\":%.3f,\"maxBottomGap\":%.3f,\"maxBottomBurial\":%.3f}"),
		bGrounded ? TEXT("true") : TEXT("false"), HitSamples, LocalMin.X, LocalMin.Y, LocalMin.Z,
		LocalMax.X, LocalMax.Y, LocalMax.Z, GroundPoint.X, GroundPoint.Y, GroundPoint.Z,
		GroundNormal.X, GroundNormal.Y, GroundNormal.Z, BurialDepth, MaxBottomGap, MaxBottomBurial);
}
