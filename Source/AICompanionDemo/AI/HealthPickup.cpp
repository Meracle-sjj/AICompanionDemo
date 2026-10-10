/**
 * [INPUT]: 依赖 HealthPickup.h 的组件契约、Engine 的 StaticMesh/MaterialInstanceDynamic（/Engine 基础几何 + **显式挂 BasicShapeMaterial 再染 BaseColor**——圆柱资产槽 0 是无参数 DefaultMaterial，直接染=沉默失败，2026-09-12 蛋糕丑案实证）、DrawDebugString（+N 飘字）、Kismet 的 GetPlayerPawn、本模块 HealthComponent 的 Heal、本模块 Audio/SFXLibrary（Pickup_Heal 事件音效）
 * [OUTPUT]: 对外提供 AHealthPickup 的实现——三件套蛋糕拼装（柠檬黄蛋糕体 60⌀ + 奶油白外悬圈 68⌀ + 红樱桃球 16⌀）；Tick 双段式（满血待机：浮动自转 / 受伤圈内：接触即吃）；收集 = 玩家 Heal + 头顶绿色飘字 + 自毁
 * [POS]: AI/ 的拾取物家族实现（XpPickup 姊妹件，复用其浮动/距离判定/自毁范式；差异：无磁吸段、满血拒收——蛋糕是决策不是自动收入）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "HealthPickup.h"

#include "AICompanionDemo.h"
#include "Audio/SFXLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 柠檬黄蛋糕体 / 奶油白圈 / 樱桃红（BasicShapeMaterial 真参数名 "Color"——09-12 实证）：
	// 撞色改判（用户复测三追加裁决 2026-09-13）：不占柠檬黄，宝箱改宝蓝——蛋糕黄保留
	const FLinearColor kLemonColor(0.95f, 0.82f, 0.15f);
	const FLinearColor kCreamColor(0.98f, 0.96f, 0.90f);
	const FLinearColor kCherryColor(0.85f, 0.12f, 0.10f);

	// 待机表现参数：浮动幅度/频率与自转速率——"可拾取物"通用语言，节奏比经验晶体慢半拍（食物不着急）
	constexpr float kHealthBobAmplitude = 8.f;
	constexpr float kHealthBobFrequency = 2.5f;
	constexpr float kHealthSpinRate = 90.f;

	// 换装分色（09-20 二审：材质槽探针实证 cake_roll 三槽 Rabit 兔子/RollCake 蛋糕主体/Plate 瓷盘）
	const FLinearColor kCakeRollColor(0.92f, 0.55f, 0.18f);   // 烘焙橙黄——蛋糕卷海绵体
	const FLinearColor kRabbitColor(0.93f, 0.88f, 0.82f);     // 兔子奶白
	const FLinearColor kPlateColor(0.92f, 0.92f, 0.90f);      // 瓷盘白
}

AHealthPickup::AHealthPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// 三件套蛋糕（/Engine 基础几何零资产拼装，柱体原生 100⌀×100h、球体 100⌀ 中心轴心）：
	// 蛋糕体 70⌀×36h 黄 + 奶油圈 78⌀×18h 白（外扩 4cm 悬边）+ 樱桃 26⌀ 红顶点——
	// 09-12 二次调形：首版三件太小远看不分层（用户观感"一坨黄东西"），比例拉大让剪影一读即蛋糕
	CakeBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CakeBody"));
	RootComponent = CakeBody;
	CakeBody->SetRelativeScale3D(FVector(0.7f, 0.7f, 0.36f));

	CreamTop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CreamTop"));
	CreamTop->SetupAttachment(RootComponent);
	CreamTop->SetRelativeLocation(FVector(0.f, 0.f, 27.f));
	CreamTop->SetRelativeScale3D(FVector(0.78f, 0.78f, 0.18f));

	Cherry = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cherry"));
	Cherry->SetupAttachment(RootComponent);
	Cherry->SetRelativeLocation(FVector(0.f, 0.f, 44.f));
	Cherry->SetRelativeScale3D(FVector(0.26f, 0.26f, 0.26f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CherryMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BasicMat = BasicMatFinder.Succeeded() ? BasicMatFinder.Object : nullptr;   // 换装染色的备用材质（成员持有）
	if (CylinderFinder.Succeeded())
	{
		CakeBody->SetStaticMesh(CylinderFinder.Object);
		CreamTop->SetStaticMesh(CylinderFinder.Object);
	}
	if (CherryMeshFinder.Succeeded())
	{
		Cherry->SetStaticMesh(CherryMeshFinder.Object);
	}

	// 染色管线：显式换上 BasicShapeMaterial（含 BaseColor 参数）再生成 MID——圆柱资产的默认材质无参数可染
	if (BasicMatFinder.Succeeded())
	{
		CakeBody->SetMaterial(0, BasicMatFinder.Object);
		CreamTop->SetMaterial(0, BasicMatFinder.Object);
		Cherry->SetMaterial(0, BasicMatFinder.Object);
	}

	// 碰撞全关（XpPickup 同款哲学）：纯距离判定，不污染队友 LOS/感知 trace
	CakeBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CreamTop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cherry->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 无人拾取自毁（VS 同款防堆积；食物比晶体多留 15s——掉率低，值得多等）
	SetLifeSpan(Lifespan);
}

void AHealthPickup::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 2.f * PI;

	// 换装即用 Fab 原生材质（蛋糕白案根治，同 XpPickup）——跳过三件套染色
	if (ApplyMeshOverride())
	{
		return;
	}

	// 运行时 MID 克隆再染色（XpPickup 同范式；材质已在构造期显式换成含 BaseColor 的 BasicShapeMaterial）
	struct FCakePart
	{
		UStaticMeshComponent* Part;
		const FLinearColor* Color;
	};
	const FCakePart Parts[] = {
		{CakeBody, &kLemonColor},
		{CreamTop, &kCreamColor},
		{Cherry, &kCherryColor},
	};
	for (const FCakePart& Item : Parts)
	{
		if (Item.Part && Item.Part->GetStaticMesh() && Item.Part->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = Item.Part->CreateAndSetMaterialInstanceDynamic(0))
			{
				// 参数真名 "Color"（2026-09-12 MaterialEditingLibrary 实证——"BaseColor" 是以讹传讹，
				// 两代拾取物的染色静默失败皆源于此）
				Mid->SetVectorParameterValue(TEXT("Color"), *Item.Color);
			}
		}
	}
}

void AHealthPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}

	// 满血拒收（蛋糕留着等你受伤——"省着用"的微型决策；Lifespan 到点自毁兜底）
	const UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	if (PlayerHealth && !PlayerHealth->IsDead()
		&& PlayerHealth->GetCurrentHealth() < PlayerHealth->GetMaxHealth())
	{
		const float DistCm = FVector(PlayerPawn->GetActorLocation() - GetActorLocation()).Size2D();
		if (DistCm <= CollectRadius)
		{
			Collect();
			return;
		}
	}

	// 待机：浮动自转（叠加在 RestLocation 上，不逐帧漂移；樱桃随根组件同转）
	BobPhase += DeltaSeconds * kHealthBobFrequency;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, FMath::Sin(BobPhase) * kHealthBobAmplitude));
	CakeBody->AddWorldRotation(FRotator(0.f, kHealthSpinRate * DeltaSeconds, 0.f));
}

bool AHealthPickup::ApplyMeshOverride()
{
	if (!MeshOverride.ToSoftObjectPath().IsValid())
	{
		// 09-20 蛋糕白案诊断：走到这里 = Config 没应用（ini 段/类声明问题）
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Heal][Skin] MeshOverride 为空（Config 未应用？）——回退三件套"));
		return false;
	}
	UStaticMesh* Mesh = MeshOverride.LoadSynchronous();
	if (!Mesh)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Heal][Skin] 加载失败：%s"), *MeshOverride.ToSoftObjectPath().ToString());
		return false;
	}
	CakeBody->SetStaticMesh(Mesh);
	CakeBody->SetRelativeScale3D(FVector(MeshOverrideScale));
	for (int32 Slot = 0; Slot < CakeBody->GetNumMaterials(); ++Slot)
	{
		CakeBody->SetMaterial(Slot, nullptr);
	}
	// 组件侧槽名 API 是复数数组版（单数 GetMaterialSlotName 不存在，C2039 实证）；日志段在下方共用
	const TArray<FName> MeshSlotNames = CakeBody->GetMaterialSlotNames();

	// 09-20 蛋糕白案二审：cake_roll 三槽（Rabit/RollCake/Plate）槽材质全是缺贴图的 Fab 材质实例
	// （包内无 Textures 目录）渲染默认白；一审只染槽 0 → 兔子橙黄、蛋糕主体仍白的半治状态
	// （用户截图定罪）。按槽名语义逐槽分色——形状真模型 + 颜色手动近似；
	// 领到带贴图的蛋糕包后删除本段即可恢复原生材质
	if (BasicMat)
	{
		for (int32 Slot = 0; Slot < CakeBody->GetNumMaterials(); ++Slot)
		{
			const FName SlotName = MeshSlotNames.IsValidIndex(Slot) ? MeshSlotNames[Slot] : FName();
			const FLinearColor* Tint = &kCakeRollColor;   // 名字对不上的槽一律染蛋糕橙黄兜底
			if (SlotName == FName("Rabit"))
			{
				Tint = &kRabbitColor;
			}
			else if (SlotName == FName("Plate"))
			{
				Tint = &kPlateColor;
			}
			CakeBody->SetMaterial(Slot, BasicMat);
			if (UMaterialInstanceDynamic* Mid = CakeBody->CreateAndSetMaterialInstanceDynamic(Slot))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), *Tint);
			}
		}
	}
	// 拼装件隐藏：三件套几何与真模型同屏即穿帮（visibility 而非销毁——组件结构保持稳定）
	CreamTop->SetVisibility(false);
	Cherry->SetVisibility(false);
	// 诊断日志：打印全部槽名（09-20 二审教训——一审日志只报槽 0，漏看另外两槽的白）
	FString SlotNameText;
	for (const FName& Name : MeshSlotNames)
	{
		SlotNameText += (SlotNameText.IsEmpty() ? TEXT("") : TEXT("/")) + Name.ToString();
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Heal][Skin] 换装 %s ×%.2f（槽 %s → 逐槽分色）"),
		*Mesh->GetName(), MeshOverrideScale, *SlotNameText);
	return true;
}

void AHealthPickup::Collect()
{
	// 回血直入玩家血量组件：不经过 WaveDirector 账本（回血是即时消耗，账本只管 XP）
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (UHealthComponent* PlayerHealth = PlayerPawn ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		const float Before = PlayerHealth->GetCurrentHealth();
		PlayerHealth->Heal(HealAmount);
		const float Healed = PlayerHealth->GetCurrentHealth() - Before;

		// 拾取反馈：+N 飘在玩家头顶（DrawDebugString 跟随 pawn，与经验晶体同语言，绿色=回血）
		if (APawn* FeedbackPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 160.f), FString::Printf(TEXT("+%.0f"), Healed),
				FeedbackPawn, FColor::Green, 0.9f, /*bDrawShadow*/ false, /*FontScale*/ 1.f);
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Heal] 拾取柠檬蛋糕 +%.0f（余血 %.0f/%.0f）"),
			Healed, PlayerHealth->GetCurrentHealth(), PlayerHealth->GetMaxHealth());
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Pickup_Heal);   // [SFX] 回血收集（实际吃到才响——满血拒收天然静默）
	}

	Destroy();
}
