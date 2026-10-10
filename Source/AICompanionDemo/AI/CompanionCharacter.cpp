/**
 * [INPUT]: 依赖 CompanionCharacter.h 的组件契约、Engine 的胶囊/移动组件与 UWidgetComponent（UMG）、UI/SpeechBubble 的气泡 widget、Kismet 的 GetPlayerController
 * [OUTPUT]: 对外提供 ACompanionCharacter 构造——放置即接管（AIControllerClass + AutoPossessAI）+ 头顶气泡组件（Screen 模式：420×120/底中锚定/35m 剔除/无碰撞）与 ShowSpeechBubble（惰性 CreateWidget——BeginPlay 期 PlayerController 可能未就绪）
 * [POS]: Source/AICompanionDemo/AI 的成员，M1 跟随主体；与玩家 AICompanionDemoCharacter 平行不继承（玩家类绑相机弹簧臂与输入，队友职责隔离）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "AICompanionDemo.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/SpeechBubble.h"

ACompanionCharacter::ACompanionCharacter()
{
	// 胶囊与玩家同规格：既能走玩家的路，也不会被门口卡住
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// 队友不受控制器旋转驱动，朝移动方向转身（与玩家模板一致的手感来源）
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 500.f;

	// 语义正确的旗标（寻路直移优先走加速度路径），但探针实证它 alone 不足以点亮 ABP 步态——
	// 真正的步态驱动在 CompanionAIController::TickComponent 的每帧输入馈送，此处保留作为行为一致性设置
	GetCharacterMovement()->bRequestedMoveUseAcceleration = true;

	// 拖进关卡即被自研 AIController 接管，免任何手动 Controller 配置
	AIControllerClass = ACompanionAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// 网格骨位基线（人偶原点在盆骨，需下沉 90cm 并转 -90° 面向前方）；
	// 最终外观（SKM_Manny + ABP_Manny）按模板约定在 BP 子类装配，C++ 不做内容引用
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// 头顶气泡装载（09-26 夜轮，检索定稿：Screen 空间覆盖层——WoW/ESO 名牌语义，恒定字号不被遮挡；
	// 挂胶囊不挂 mesh 插槽——不依赖同款骨骼，复用到敌人/玩家时零前提）
	SpeechBubble = CreateDefaultSubobject<UWidgetComponent>(TEXT("SpeechBubble"));
	SpeechBubble->SetupAttachment(GetCapsuleComponent());
	SpeechBubble->SetRelativeLocation(FVector(0.f, 0.f, 120.f));   // 胶囊半高 96 + 头顶余量
}

void ACompanionCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 气泡组件参数（Screen 模式检索定稿）：DrawSize 必须显式（默认 0 即不可见）；底中锚定向上生长；
	// 无碰撞不挡线（5.6 UWidgetComponent 无 MaxDrawDistance 属性——检索报告此处失实，Screen 模式随角色投影无需剔除）
	SpeechBubble->SetWidgetSpace(EWidgetSpace::Screen);
	SpeechBubble->SetDrawSize(FVector2D(420.f, 120.f));
	SpeechBubble->SetPivot(FVector2D(0.5f, 1.0f));
	SpeechBubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechBubble->SetVisibility(false);   // ShowSpeechBubble 首词才亮

	// 诊断日志：确认角色实体已入场及其控制器接管状态（若 controller=空 = AutoPossessAI 未生效）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] Character BeginPlay: %s loc=%s controller=%s"),
		*GetNameSafe(this), *GetActorLocation().ToCompactString(), *GetNameSafe(GetController()));
}

void ACompanionCharacter::ShowSpeechBubble(const FString& Line)
{
	// 惰性建 widget（纯 C++ 类零蓝图资产）：BeginPlay 期 PlayerController 可能未就绪，首次台词必在开演后
	if (!BubbleWidget)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			BubbleWidget = CreateWidget<USpeechBubble>(PC, USpeechBubble::StaticClass());
			if (BubbleWidget)
			{
				SpeechBubble->SetWidget(BubbleWidget);
				SpeechBubble->SetVisibility(true);
			}
		}
	}
	if (BubbleWidget)
	{
		BubbleWidget->ShowLine(Line);
	}
}
