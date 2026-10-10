/**
 * [INPUT]: Character实际SpringArm/FollowCamera基线，BossBase家族存活/退出事件与导演活虫事件，Health死亡与GameMode终局状态。
 * [OUTPUT]: 任意活Boss期间实际臂长平滑拉远1.6倍，虫破土/入土0.55s强震与巨像敲地短震/受击合成；暂停清震，最后宿主退出平滑复原，死亡/Ending/EndPlay立即复原。
 * [POS]: 玩家相机局部实现，只修改TargetArmLength/TargetOffset；不改控制旋转、移动、瞄准、FOV或碰撞开关。
 * [PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md
 */
#include "AICompanionDemoCharacter.h"
#include "AICompanionDemoGameMode.h"
#include "AI/BossBase.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

void AAICompanionDemoCharacter::InitializeWurmCamera()
{
	if (bWurmCameraInitialized || !CameraBoom) return;
	// 读BP/实例完成装配后的真实值，不能把C++默认400写成所有玩家的恢复值。
	NormalCameraArmLength = CameraBoom->TargetArmLength;
	NormalCameraTargetOffset = CameraBoom->TargetOffset;
	bWurmCameraInitialized = true;
}

void AAICompanionDemoCharacter::SetWurmEncounterActive(bool bActive)
{
	InitializeWurmCamera();
	bWurmEncounterActive = bActive && Health && !Health->IsDead();
	RefreshBossEncounterCamera();
}

void AAICompanionDemoCharacter::RefreshBossEncounterCamera(const AActor* Leaving)
{
	InitializeWurmCamera();
	BossCameraAliveCount = 0;
	if (GetWorld()) for (TActorIterator<ABossBase> It(GetWorld()); It; ++It)
		if (*It != Leaving && !It->IsActorBeingDestroyed() && !It->IsDead()) ++BossCameraAliveCount;
	bBossEncounterActive = BossCameraAliveCount > 0 && Health && !Health->IsDead();
	if (!bBossEncounterActive)
	{
		WurmCameraShakeTime = 0.f;
		BossCameraShakeXY = BossCameraShakeZ = 0.f;
		if (CameraBoom && CameraShakeTime <= 0.f) CameraBoom->TargetOffset = NormalCameraTargetOffset;
	}
}

void AAICompanionDemoCharacter::PulseWurmCameraShake(float Duration)
{
	if (bWurmEncounterActive) PulseBossCameraShake(Duration, WurmCameraShakeXY, WurmCameraShakeZ);
}

void AAICompanionDemoCharacter::PulseBossCameraShake(float Duration, float XYAmplitude, float ZAmplitude)
{
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	if (!bBossEncounterActive || !Health || Health->IsDead() || !GetWorld() || GetWorld()->IsPaused()
		|| UGameplayStatics::GetGlobalTimeDilation(this) < .01f || (GM && GM->IsEndingShown())) return;
	if (WurmCameraShakeTime <= 0.f) BossCameraShakeXY = BossCameraShakeZ = 0.f;
	BossCameraShakeXY = FMath::Max(BossCameraShakeXY, FMath::Clamp(XYAmplitude, 0.f, 150.f));
	BossCameraShakeZ = FMath::Max(BossCameraShakeZ, FMath::Clamp(ZAmplitude, 0.f, 100.f));
	WurmCameraShakeDuration = FMath::Max(WurmCameraShakeTime, FMath::Clamp(Duration, .05f, .8f));
	WurmCameraShakeTime = FMath::Max(WurmCameraShakeTime, WurmCameraShakeDuration);
	++WurmCameraShakePulses;
}

void AAICompanionDemoCharacter::ResetWurmEncounterCamera()
{
	bWurmEncounterActive = false;
	bBossEncounterActive = false;
	BossCameraAliveCount = 0;
	WurmCameraShakeTime = 0.f;
	BossCameraShakeXY = BossCameraShakeZ = 0.f;
	CameraShakeTime = 0.f;
	if (CameraBoom && bWurmCameraInitialized)
	{
		CameraBoom->TargetArmLength = NormalCameraArmLength;
		CameraBoom->TargetOffset = NormalCameraTargetOffset;
	}
}

void AAICompanionDemoCharacter::TickWurmCamera(float DeltaSeconds)
{
	InitializeWurmCamera();
	if (!CameraBoom || !GetWorld()) return;
	const AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>();
	if (!Health || Health->IsDead() || (GM && GM->IsEndingShown()))
	{
		ResetWurmEncounterCamera();
		return;
	}
	if (GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f)
	{
		// 不把震动冻结到暂停画面，也不在复原流速后续播旧冲击。
		CameraShakeTime = WurmCameraShakeTime = 0.f;
		CameraBoom->TargetOffset = NormalCameraTargetOffset;
		if (!bBossEncounterActive) CameraBoom->TargetArmLength = NormalCameraArmLength;
		return;
	}
	const float Goal = NormalCameraArmLength * (bBossEncounterActive ? FMath::Max(1.f, WurmCameraDistanceScale) : 1.f);
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, Goal, DeltaSeconds, WurmCameraBlendSpeed);
	CameraShakeTime = FMath::Max(CameraShakeTime - DeltaSeconds, 0.f);
	WurmCameraShakeTime = FMath::Max(WurmCameraShakeTime - DeltaSeconds, 0.f);
	const float HurtStrength = CameraShakeTime / .3f;
	const float BossStrength = WurmCameraShakeTime / WurmCameraShakeDuration;
	const float XY = FMath::Max(16.f * HurtStrength, BossCameraShakeXY * BossStrength);
	const float Z = FMath::Max(9.f * HurtStrength, BossCameraShakeZ * BossStrength);
	CameraBoom->TargetOffset = NormalCameraTargetOffset + FVector(
		FMath::FRandRange(-1.f, 1.f) * XY, FMath::FRandRange(-1.f, 1.f) * XY, FMath::FRandRange(-1.f, 1.f) * Z);
}

void AAICompanionDemoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetWurmEncounterCamera();
	Super::EndPlay(EndPlayReason);
}

FString AAICompanionDemoCharacter::DebugWurmCameraState() const
{
#if WITH_EDITOR
	const FRotator Control = GetControlRotation();
	const FVector Offset = CameraBoom ? CameraBoom->TargetOffset - NormalCameraTargetOffset : FVector::ZeroVector;
	const FVector CameraLocation = FollowCamera ? FollowCamera->GetComponentLocation() : FVector::ZeroVector;
	return FString::Printf(TEXT("{\"active\":%s,\"liveBosses\":%d,\"wurmActive\":%s,\"shakeXY\":%.3f,\"shakeZ\":%.3f,\"initialized\":%s,\"normalArm\":%.3f,\"actualArm\":%.3f,\"goalArm\":%.3f,\"shakeRemaining\":%.4f,\"hurtRemaining\":%.4f,\"shakePulses\":%d,\"offsetX\":%.4f,\"offsetY\":%.4f,\"offsetZ\":%.4f,\"cameraX\":%.3f,\"cameraY\":%.3f,\"cameraZ\":%.3f,\"controlPitch\":%.3f,\"controlYaw\":%.3f,\"fov\":%.3f,\"collisionTest\":%s,\"paused\":%s}"),
		bBossEncounterActive ? TEXT("true") : TEXT("false"), BossCameraAliveCount, bWurmEncounterActive ? TEXT("true") : TEXT("false"), BossCameraShakeXY, BossCameraShakeZ,
		bWurmCameraInitialized ? TEXT("true") : TEXT("false"),
		NormalCameraArmLength, CameraBoom ? CameraBoom->TargetArmLength : 0.f,
		NormalCameraArmLength * (bBossEncounterActive ? FMath::Max(1.f, WurmCameraDistanceScale) : 1.f),
		WurmCameraShakeTime, CameraShakeTime, WurmCameraShakePulses, Offset.X, Offset.Y, Offset.Z,
		CameraLocation.X, CameraLocation.Y, CameraLocation.Z, Control.Pitch, Control.Yaw,
		FollowCamera ? FollowCamera->FieldOfView : 0.f, CameraBoom && CameraBoom->bDoCollisionTest ? TEXT("true") : TEXT("false"),
		GetWorld() && GetWorld()->IsPaused() ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}
