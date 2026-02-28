#include "PowerLineSystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SceneManagement.h"
#include "EngineUtils.h"            // TActorIterator
#include "UObject/UObjectIterator.h" // TObjectIterator

// ============================
// District Data Manager
// ============================

APowerLineDistrictDataManager::APowerLineDistrictDataManager()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DistrictRoot"));
	SetRootComponent(SceneRoot);

	EditorBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("DistrictBillboard"));
	EditorBillboard->SetupAttachment(SceneRoot);
	EditorBillboard->SetHiddenInGame(true);
	EditorBillboard->SetIsVisualizationComponent(true);

	AreaSphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("AreaSphere"));
	AreaSphereComponent->SetupAttachment(SceneRoot);
	AreaSphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaSphereComponent->SetGenerateOverlapEvents(false);
	AreaSphereComponent->SetHiddenInGame(true);

	AreaBoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("AreaBox"));
	AreaBoxComponent->SetupAttachment(SceneRoot);
	AreaBoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaBoxComponent->SetGenerateOverlapEvents(false);
	AreaBoxComponent->SetHiddenInGame(true);

	RefreshAreaVisualization();
}

uint32 APowerLineDistrictDataManager::HashLine(const FVector& A, const FVector& B, int32 LineId)
{
	// Quantize to reduce jitter when actors move by tiny amounts (also keeps hash stable in editor).
	auto Q = [](const FVector& V) {
		// 1 cm precision
		return FIntVector(
			FMath::RoundToInt(V.X),
			FMath::RoundToInt(V.Y),
			FMath::RoundToInt(V.Z));
		};

	const FIntVector QA = Q(A);
	const FIntVector QB = Q(B);

	uint32 H = 0;
	H = HashCombine(H, GetTypeHash(QA));
	H = HashCombine(H, GetTypeHash(QB));
	H = HashCombine(H, GetTypeHash(LineId));
	return H;
}

float APowerLineDistrictDataManager::GetSagForLine(const FVector& StartWS, const FVector& EndWS, int32 LineId) const
{
	const float MinS = FMath::Min(Sag.SagRangeCm.X, Sag.SagRangeCm.Y);
	const float MaxS = FMath::Max(Sag.SagRangeCm.X, Sag.SagRangeCm.Y);

	float Value = 0.f;
	if (Sag.bDeterministic)
	{
		const uint32 H = HashLine(StartWS, EndWS, LineId);
		FRandomStream R(Sag.Seed ^ (int32)H);
		Value = R.FRandRange(MinS, MaxS);
	}
	else
	{
		Value = FMath::FRandRange(MinS, MaxS);
	}

	return FMath::Max(0.f, Value * Sag.SagScale);
}

int32 APowerLineDistrictDataManager::GetSegmentsForLength(float LengthCm) const
{
	if (!Segments.bAutoSegments)
	{
		return FMath::Max(1, Segments.FixedSegments);
	}

	const float Step = FMath::Max(10.f, Segments.TargetSegmentLengthCm);
	const int32 Raw = FMath::CeilToInt(LengthCm / Step);
	return FMath::Clamp(Raw, FMath::Max(1, Segments.MinSegments), FMath::Max(1, Segments.MaxSegments));
}

bool APowerLineDistrictDataManager::GetHangingForLine(
	const FVector& StartWS,
	const FVector& EndWS,
	int32 LineId,
	UStaticMesh*& OutMesh,
	float& OutNormalizedDistance,
	float& OutYawDeg) const
{
	OutMesh = nullptr;
	OutNormalizedDistance = 0.5f;
	OutYawDeg = 0.f;

	if (Hanging.MeshPool.Num() == 0) return false;
	if (Hanging.ChancePerWire <= 0.f) return false;

	const float MinN = FMath::Clamp(FMath::Min(Hanging.NormalizedDistanceRange.X, Hanging.NormalizedDistanceRange.Y), 0.f, 1.f);
	const float MaxN = FMath::Clamp(FMath::Max(Hanging.NormalizedDistanceRange.X, Hanging.NormalizedDistanceRange.Y), 0.f, 1.f);
	if (MaxN <= MinN) return false;

	FRandomStream R;
	if (Hanging.bDeterministic)
	{
		const uint32 H = HashLine(StartWS, EndWS, LineId);
		R.Initialize(Hanging.Seed ^ (int32)H);
	}
	else
	{
		R.GenerateNewSeed();
	}

	if (R.FRand() > Hanging.ChancePerWire) return false;

	const int32 MeshIdx = R.RandRange(0, Hanging.MeshPool.Num() - 1);
	OutMesh = Hanging.MeshPool[MeshIdx].Get();
	if (!OutMesh) return false;

	OutNormalizedDistance = R.FRandRange(MinN, MaxN);
	OutYawDeg = (Hanging.RandomYawDeg > 0.f) ? R.FRandRange(-Hanging.RandomYawDeg, Hanging.RandomYawDeg) : 0.f;
	return true;
}

bool APowerLineDistrictDataManager::AffectsWorldLocation(const FVector& LocationWS) const
{
	if (!bUseArea)
	{
		return true;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(LocationWS);
	if (AreaShape == EPowerLineDistrictAreaShape::Sphere)
	{
		const float Radius = FMath::Max(1.f, SphereRadiusCm);
		return Local.SizeSquared() <= FMath::Square(Radius);
	}

	const FVector Extent = FVector(
		FMath::Max(1.f, BoxExtentCm.X),
		FMath::Max(1.f, BoxExtentCm.Y),
		FMath::Max(1.f, BoxExtentCm.Z));
	return FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

void APowerLineDistrictDataManager::RefreshAreaVisualization()
{
	if (AreaSphereComponent)
	{
		AreaSphereComponent->SetSphereRadius(FMath::Max(1.f, SphereRadiusCm));
		AreaSphereComponent->SetVisibility(bUseArea && AreaShape == EPowerLineDistrictAreaShape::Sphere);
	}

	if (AreaBoxComponent)
	{
		AreaBoxComponent->SetBoxExtent(FVector(
			FMath::Max(1.f, BoxExtentCm.X),
			FMath::Max(1.f, BoxExtentCm.Y),
			FMath::Max(1.f, BoxExtentCm.Z)));
		AreaBoxComponent->SetVisibility(bUseArea && AreaShape == EPowerLineDistrictAreaShape::Box);
	}
}

void APowerLineDistrictDataManager::MarkAllDistrictWiresDirty()
{
	UWorld* W = GetWorld();
	if (!W) return;

	for (TObjectIterator<UPowerLineComponent> It; It; ++It)
	{
		UPowerLineComponent* Line = *It;
		if (!IsValid(Line)) continue;
		if (Line->GetWorld() != W) continue;

		const bool bDirect = (Line->DistrictManager == this);

		const bool bAutoSameId =
			(!Line->DistrictManager && Line->bAutoFindDistrictDataManager &&
				(Line->DistrictId == NAME_None
					? (Line->ResolveDistrictManager() == this)
					: (Line->DistrictId == DistrictId)));

		if (bDirect || bAutoSameId)
		{
			if (bDirect || !bUseArea || AffectsWorldLocation(Line->GetComponentLocation()))
			{
				Line->MarkDirty();
			}
		}
	}
}

#if WITH_EDITOR
void APowerLineDistrictDataManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshAreaVisualization();
	MarkAllDistrictWiresDirty();
}
#endif

APowerLine_Pole::APowerLine_Pole()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	EditorArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("EditorArrow"));
	EditorArrow->SetupAttachment(Root);
	EditorArrow->SetHiddenInGame(true);
	EditorArrow->SetIsVisualizationComponent(true);
	EditorArrow->ArrowColor = FColor::Yellow;
	EditorArrow->ArrowSize = 1.0f;
}

void APowerLine_Pole::MarkChildWiresDirty()
{
	TArray<UPowerLineComponent*> Lines;
	GetComponents<UPowerLineComponent>(Lines);

	for (UPowerLineComponent* Line : Lines)
	{
		if (!Line) continue;
		Line->RefreshTargetBinding();
	}
}

#if WITH_EDITOR
void APowerLine_Pole::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropName == GET_MEMBER_NAME_CHECKED(APowerLine_Pole, DefaultTargetActor))
	{
		MarkChildWiresDirty();
	}
}
#endif

// ============================
// SceneProxy
// ============================

class FPowerLineSceneProxy final : public FPrimitiveSceneProxy
{
public:
	TArray<FPowerLineSegment> Segments;
	FBoxSphereBounds Bounds;

	explicit FPowerLineSceneProxy(const UPrimitiveComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
	{
		const UPowerLineRenderComponent* Comp = Cast<UPowerLineRenderComponent>(InComponent);
		if (Comp)
		{
			Segments = Comp->FrontBuffer;
			Bounds = Comp->CachedBounds;
		}
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static int32 Unique;
		return reinterpret_cast<SIZE_T>(&Unique);
	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if ((VisibilityMap & (1u << ViewIndex)) == 0) continue;

			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
			for (const FPowerLineSegment& S : Segments)
			{
				PDI->DrawLine(
					S.Start,
					S.End,
					S.Color,
					SDPG_World,
					S.Thickness,
					S.DepthBias,
					S.bScreenSpace);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance R;
		R.bDrawRelevance = IsShown(View);
		R.bDynamicRelevance = true;
		R.bShadowRelevance = false;
		R.bRenderInMainPass = true;
		return R;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + Segments.GetAllocatedSize();
	}

	// Render-thread update
	void Update_RenderThread(TArray<FPowerLineSegment>&& NewSegs, const FBoxSphereBounds& NewBounds)
	{
		Segments = MoveTemp(NewSegs);
		Bounds = NewBounds;
	}
};

// ============================
// Render Component
// ============================

UPowerLineRenderComponent::UPowerLineRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);

	// Must be visible
	SetVisibility(true, true);
	SetHiddenInGame(false, true);

	CachedBounds = FBoxSphereBounds(FSphere(FVector::ZeroVector, 1.f));
}

void UPowerLineRenderComponent::RebuildCachedBounds_GT()
{
	FBox Box(EForceInit::ForceInit);

	for (const auto& S : FrontBuffer)
	{
		Box += S.Start;
		Box += S.End;
	}

	// Avoid invalid bounds (engine can cull everything if invalid)
	if (!Box.IsValid)
	{
		Box = FBox(GetComponentLocation() - FVector(1), GetComponentLocation() + FVector(1));
	}

	CachedBounds = FBoxSphereBounds(Box);
}

void UPowerLineRenderComponent::UpdateSegments_GameThread(const TArray<FPowerLineSegment>& Segs)
{
	{
		FScopeLock Lock(&Mutex);
		BackBuffer = Segs;
		Swap(FrontBuffer, BackBuffer);
		RebuildCachedBounds_GT();
	}

	// This triggers SendRenderDynamicData_Concurrent (no proxy recreate)
	MarkRenderDynamicDataDirty();

	// Make sure bounds are refreshed on GT too
	UpdateBounds();
	MarkRenderTransformDirty();
}

FPrimitiveSceneProxy* UPowerLineRenderComponent::CreateSceneProxy()
{
	return new FPowerLineSceneProxy(this);
}

FBoxSphereBounds UPowerLineRenderComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// We store world-space bounds in CachedBounds already
	return CachedBounds;
}

void UPowerLineRenderComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();

	FPrimitiveSceneProxy* Proxy = SceneProxy;
	if (!Proxy) return;

	// Copy segments safely
	TArray<FPowerLineSegment> Copy;
	FBoxSphereBounds CopyBounds;

	{
		FScopeLock Lock(&Mutex);
		Copy = FrontBuffer;
		CopyBounds = CachedBounds;
	}

	ENQUEUE_RENDER_COMMAND(PowerLine_UpdateProxy)(
		[Proxy, Segs = MoveTemp(Copy), B = CopyBounds](FRHICommandListImmediate& RHICmdList) mutable {
			auto* PLProxy = static_cast<FPowerLineSceneProxy*>(Proxy);
			PLProxy->Update_RenderThread(MoveTemp(Segs), B);
		});
}

// ============================
// Helpers (local)
// ============================

static FName GetKeyFromSceneComponent(const USceneComponent* Comp)
{
	if (!Comp) return NAME_None;

	// Prefer UPowerLineComponent::AttachId if it is that type
	if (const UPowerLineComponent* PLC = Cast<UPowerLineComponent>(Comp))
	{
		if (PLC->AttachId != NAME_None)
		{
			return PLC->AttachId;
		}
	}

	// Fallback to first tag
	if (Comp->ComponentTags.Num() > 0)
	{
		return Comp->ComponentTags[0];
	}

	// Fallback to name
	return Comp->GetFName();
}

static USceneComponent* FindAttachOnActor(AActor* Actor, FName Key, EPowerLineAttachLookup LookupMode)
{
	if (!Actor || Key == NAME_None) return nullptr;

	// Collect all scene components
	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);

	auto MatchByAttachId = [&](UActorComponent* C) -> USceneComponent* {
		if (UPowerLineComponent* PLC = Cast<UPowerLineComponent>(C))
		{
			const FName K = (PLC->AttachId != NAME_None) ? PLC->AttachId : GetKeyFromSceneComponent(PLC);
			if (K == Key)
			{
				return PLC;
			}
		}
		return nullptr;
		};

	auto MatchByTag = [&](UActorComponent* C) -> USceneComponent* {
		if (USceneComponent* SC = Cast<USceneComponent>(C))
		{
			if (SC->ComponentHasTag(Key))
			{
				return SC;
			}
		}
		return nullptr;
		};

	auto MatchByName = [&](UActorComponent* C) -> USceneComponent* {
		if (USceneComponent* SC = Cast<USceneComponent>(C))
		{
			if (SC->GetFName() == Key)
			{
				return SC;
			}
		}
		return nullptr;
		};

	// 1) Try strict mode
	for (UActorComponent* C : Comps)
	{
		if (!C) continue;

		USceneComponent* Found = nullptr;
		switch (LookupMode)
		{
		case EPowerLineAttachLookup::ByAttachId:
			Found = MatchByAttachId(C);
			break;
		case EPowerLineAttachLookup::ByComponentTag:
			Found = MatchByTag(C);
			break;
		case EPowerLineAttachLookup::ByComponentName:
			Found = MatchByName(C);
			break;
		default:
			break;
		}

		if (Found) return Found;
	}

	// 2) Fallback: if user picked AttachId mode but target uses tags/names (or vice-versa),
	// try "smart" fallback using GetKeyFromSceneComponent()
	for (UActorComponent* C : Comps)
	{
		USceneComponent* SC = Cast<USceneComponent>(C);
		if (!SC) continue;

		if (GetKeyFromSceneComponent(SC) == Key)
		{
			return SC;
		}
	}

	return nullptr;
}

// ============================
// PowerLineComponent
// ============================

UPowerLineComponent::UPowerLineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPowerLineComponent::OnRegister()
{
	Super::OnRegister();

	// subscribe to own transform changes (no Tick)
	if (!TransformChangedHandle.IsValid())
	{
		TransformChangedHandle = TransformUpdated.AddUObject(
			this, &UPowerLineComponent::HandleTransformChanged);
	}

	BindToTarget();

	if (auto* Sub = GetWorld()->GetSubsystem<UPowerLineSubsystem>())
	{
		Sub->RegisterPowerLine(this);
	}
}

void UPowerLineComponent::OnUnregister()
{
	UnbindFromTarget();

	// unsubscribe own transform
	if (TransformChangedHandle.IsValid())
	{
		TransformUpdated.Remove(TransformChangedHandle);
		TransformChangedHandle.Reset();
	}

	if (auto* Sub = GetWorld()->GetSubsystem<UPowerLineSubsystem>())
	{
		Sub->UnregisterPowerLine(this);
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UPowerLineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// If target/attach mapping changed in editor, rebind movement delegate.
	if (PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetActor) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetLookup) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, AttachId) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetAttachIdOverride) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, ManualEndPointWS))
	{
		BindToTarget();
	}

	// Any editable property can affect rendering (segments/sag/color/thickness/district),
	// so always request rebuild in editor.
	MarkDirty();
}
#endif

void UPowerLineComponent::BindToTarget()
{
	UnbindFromTarget();
	if (!GetWorld()) return;

	AActor* EffectiveTarget = ResolveEffectiveTargetActor();
	if (!EffectiveTarget) return;
	BoundTargetActor = EffectiveTarget;

	USceneComponent* TargetRoot = EffectiveTarget->GetRootComponent();
	if (!TargetRoot) return;

	// subscribe to target root transform updates
	TargetTransformChangedHandle = TargetRoot->TransformUpdated.AddUObject(
		this, &UPowerLineComponent::HandleTargetTransformChanged);
}

void UPowerLineComponent::UnbindFromTarget()
{
	if (!TargetTransformChangedHandle.IsValid()) return;

	if (AActor* Bound = BoundTargetActor.Get())
	{
		if (USceneComponent* TargetRoot = Bound->GetRootComponent())
		{
			TargetRoot->TransformUpdated.Remove(TargetTransformChangedHandle);
		}
	}

	BoundTargetActor = nullptr;
	TargetTransformChangedHandle.Reset();
}

void UPowerLineComponent::HandleTransformChanged(
	USceneComponent* InComponent,
	EUpdateTransformFlags UpdateTransformFlags,
	ETeleportType Teleport)
{
	MarkDirty();
}

void UPowerLineComponent::HandleTargetTransformChanged(
	USceneComponent* InComponent,
	EUpdateTransformFlags UpdateTransformFlags,
	ETeleportType Teleport)
{
	MarkDirty();
}

void UPowerLineComponent::MarkDirty()
{
	if (!GetWorld()) return;

	if (auto* Sub = GetWorld()->GetSubsystem<UPowerLineSubsystem>())
	{
		Sub->MarkPowerLineDirty(this);
	}
}

void UPowerLineComponent::RefreshTargetBinding()
{
	BindToTarget();
	MarkDirty();
}

FName UPowerLineComponent::GetAttachKey() const
{
	if (AttachId != NAME_None)
	{
		return AttachId;
	}

	if (ComponentTags.Num() > 0)
	{
		return ComponentTags[0];
	}

	return GetFName();
}

AActor* UPowerLineComponent::ResolveEffectiveTargetActor() const
{
	if (TargetActor)
	{
		return TargetActor;
	}

	if (const APowerLine_Pole* PoleOwner = Cast<APowerLine_Pole>(GetOwner()))
	{
		return PoleOwner->DefaultTargetActor.Get();
	}

	return nullptr;
}

bool UPowerLineComponent::ResolveEndPoint(FVector& OutEnd) const
{
	// 1) If we have a target actor, draw only when a matching attach exists.
	if (AActor* EffectiveTarget = ResolveEffectiveTargetActor())
	{
		const FName MyKey = GetAttachKey();
		const FName WantedKey = (TargetAttachIdOverride != NAME_None) ? TargetAttachIdOverride : MyKey;

		if (USceneComponent* TargetComp = FindAttachOnActor(EffectiveTarget, WantedKey, TargetLookup))
		{
			OutEnd = TargetComp->GetComponentLocation();
			return true;
		}

		OutEnd = FVector::ZeroVector;
		return false;
	}

	// 2) No target actor: use manual endpoint only when it is explicitly set.
	if (!ManualEndPointWS.IsNearlyZero())
	{
		OutEnd = ManualEndPointWS;
		return true;
	}

	OutEnd = FVector::ZeroVector;
	return false;
}

bool UPowerLineComponent::GetResolvedEndPointWS(FVector& OutEnd) const
{
	return ResolveEndPoint(OutEnd);
}

APowerLineDistrictDataManager* UPowerLineComponent::ResolveDistrictManager() const
{
	const FVector MyLocation = GetComponentLocation();

	if (DistrictManager)
	{
		return DistrictManager.Get();
	}

	if (!bAutoFindDistrictDataManager)
	{
		return nullptr;
	}

	UWorld* W = GetWorld();
	if (!W) return nullptr;

	APowerLineDistrictDataManager* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<APowerLineDistrictDataManager> It(W); It; ++It)
	{
		APowerLineDistrictDataManager* M = *It;
		if (!M) continue;

		if (DistrictId != NAME_None && M->DistrictId != DistrictId)
		{
			continue;
		}

		if (!M->AffectsWorldLocation(MyLocation))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLocation, M->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = M;
		}
	}

	return Best;
}

void UPowerLineComponent::BuildSegments(TArray<FPowerLineSegment>& Out) const
{
	FVector EndWS;
	const bool bConnected = ResolveEndPoint(EndWS);
	if (!bConnected)
	{
		return;
	}

	const FVector StartWS = GetComponentLocation();
	const FVector Delta = EndWS - StartWS;
	const float Length = Delta.Size();

	APowerLineDistrictDataManager* DM = ResolveDistrictManager();

	float EffectiveSag = SagAmount;
	int32 EffectiveSegments = FMath::Max(2, NumSegments);

	if (DM)
	{
		EffectiveSag = DM->GetSagForLine(StartWS, EndWS, LineId);
		// Use both sources so per-wire NumSegments can always increase detail,
		// while district auto/fixed policy still affects baseline segmentation.
		const int32 DistrictSegments = DM->GetSegmentsForLength(Length);
		EffectiveSegments = FMath::Max(2, FMath::Max(NumSegments, DistrictSegments));
	}

	auto PointAt = [&](float T) {
		const FVector P = FMath::Lerp(StartWS, EndWS, T);
		const float SagFactor = FMath::Clamp(4.f * T * (1.f - T), 0.f, 1.f);
		return P - FVector(0, 0, EffectiveSag * SagFactor);
		};

	// Build equal-length segments along the sagged curve (arc-length parameterization).
	const int32 SampleCount = FMath::Clamp(EffectiveSegments * 8, 32, 512);
	TArray<FVector> Samples;
	Samples.Reserve(SampleCount + 1);

	TArray<float> CumLen;
	CumLen.Reserve(SampleCount + 1);

	float TotalLen = 0.f;
	FVector Prev = PointAt(0.f);
	Samples.Add(Prev);
	CumLen.Add(0.f);

	for (int32 i = 1; i <= SampleCount; ++i)
	{
		const float T = (float)i / (float)SampleCount;
		const FVector Cur = PointAt(T);
		TotalLen += FVector::Dist(Prev, Cur);
		Samples.Add(Cur);
		CumLen.Add(TotalLen);
		Prev = Cur;
	}

	if (TotalLen <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	auto EvalAtDistance = [&](float TargetLen) {
		const float ClampedTarget = FMath::Clamp(TargetLen, 0.f, TotalLen);
		for (int32 Idx = 1; Idx < CumLen.Num(); ++Idx)
		{
			if (CumLen[Idx] < ClampedTarget)
			{
				continue;
			}

			const float L0 = CumLen[Idx - 1];
			const float L1 = CumLen[Idx];
			const float A = (L1 > L0) ? ((ClampedTarget - L0) / (L1 - L0)) : 0.f;
			return FMath::Lerp(Samples[Idx - 1], Samples[Idx], A);
		}

		return Samples.Last();
		};

	for (int32 i = 0; i < EffectiveSegments; ++i)
	{
		const float L0 = (TotalLen * (float)i) / (float)EffectiveSegments;
		const float L1 = (TotalLen * (float)(i + 1)) / (float)EffectiveSegments;

		FPowerLineSegment S;
		S.Start = EvalAtDistance(L0);
		S.End = EvalAtDistance(L1);
		S.Color = LineColor;
		S.Thickness = LineThickness;
		S.DepthBias = 0.f;
		S.bScreenSpace = true;
		Out.Add(S);
	}
}

// ============================
// Subsystem
// ============================

AActor* UPowerLineSubsystem::EnsureRenderHost()
{
	if (RenderHost.IsValid())
	{
		return RenderHost.Get();
	}

	UWorld* W = GetWorld();
	if (!W) return nullptr;

	FActorSpawnParameters P;
	UObject* NameOuter = W->PersistentLevel ? static_cast<UObject*>(W->PersistentLevel) : static_cast<UObject*>(W);
	P.Name = MakeUniqueObjectName(NameOuter, AActor::StaticClass(), TEXT("PowerLine_RenderHost"));
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.bHideFromSceneOutliner = true;

	AActor* Host = W->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, P);
	if (!Host) return nullptr;

	Host->SetActorHiddenInGame(true);
	Host->SetActorEnableCollision(false);

	// Ensure root exists
	if (!Host->GetRootComponent())
	{
		USceneComponent* Root = NewObject<USceneComponent>(Host);
		Root->RegisterComponent();
		Host->SetRootComponent(Root);
	}

	RenderHost = Host;
	return Host;
}

FPowerLineChunkKey UPowerLineSubsystem::CalcKey(const FVector& Pos) const
{
	const float CS = FMath::Max(1.f, ChunkSize);
	const int32 X = FMath::FloorToInt(Pos.X / CS);
	const int32 Y = FMath::FloorToInt(Pos.Y / CS);
	return FPowerLineChunkKey{ FIntPoint(X, Y) };
}

void UPowerLineSubsystem::EnsureRenderComponent(const FPowerLineChunkKey& Key)
{
	if (RenderComponents.Contains(Key))
	{
		if (RenderComponents[Key].IsValid())
		{
			return;
		}
	}

	AActor* Host = EnsureRenderHost();
	if (!Host) return;

	UPowerLineRenderComponent* RC = NewObject<UPowerLineRenderComponent>(Host);
	RC->SetupAttachment(Host->GetRootComponent());
	RC->RegisterComponent();

	RenderComponents.Add(Key, RC);
}

void UPowerLineSubsystem::UpdateLineChunk(UPowerLineComponent* Line, const FPowerLineChunkKey& NewKey)
{
	if (!Line) return;

	// remove from old
	if (Line->bHasKey)
	{
		if (FPowerLineChunk* Old = Chunks.Find(Line->CurrentKey))
		{
			Old->Lines.Remove(Line);
			DirtyChunks.Add(Line->CurrentKey);
		}
	}

	// add to new
	{
		FPowerLineChunk& Chunk = Chunks.FindOrAdd(NewKey);
		Chunk.Lines.Add(Line);
		DirtyChunks.Add(NewKey);
	}

	Line->CurrentKey = NewKey;
	Line->bHasKey = true;
	Line->bRegistered = true;
}

void UPowerLineSubsystem::RegisterPowerLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	const FPowerLineChunkKey Key = CalcKey(Line->GetComponentLocation());
	UpdateLineChunk(Line, Key);
	MarkPowerLineDirty(Line);
}

void UPowerLineSubsystem::UnregisterPowerLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	RemoveHangingForLine(Line);

	if (Line->bHasKey)
	{
		if (FPowerLineChunk* Chunk = Chunks.Find(Line->CurrentKey))
		{
			Chunk->Lines.Remove(Line);
			DirtyChunks.Add(Line->CurrentKey);
		}
	}

	Line->bRegistered = false;
	Line->bHasKey = false;
}

void UPowerLineSubsystem::MarkPowerLineDirty(UPowerLineComponent* Line)
{
	if (!Line) return;

	const FPowerLineChunkKey NewKey = CalcKey(Line->GetComponentLocation());

	// Move between chunks if needed
	if (!Line->bHasKey || !(Line->CurrentKey == NewKey))
	{
		UpdateLineChunk(Line, NewKey);
	}

	DirtyChunks.Add(Line->CurrentKey);
}

void UPowerLineSubsystem::RemoveHangingForLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	if (TWeakObjectPtr<UStaticMeshComponent>* C = HangingByLine.Find(Line))
	{
		if (UStaticMeshComponent* Comp = C->Get())
		{
			Comp->DestroyComponent();
		}
		HangingByLine.Remove(Line);
	}
}

void UPowerLineSubsystem::UpdateHangingForLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	APowerLineDistrictDataManager* DM = Line->ResolveDistrictManager();
	if (!DM)
	{
		RemoveHangingForLine(Line);
		return;
	}

	FVector EndWS;
	if (!Line->ResolveEndPoint(EndWS))
	{
		RemoveHangingForLine(Line);
		return;
	}

	UStaticMesh* Mesh = nullptr;
	float N = 0.5f;
	float YawDeg = 0.f;

	if (!DM->GetHangingForLine(Line->GetComponentLocation(), EndWS, Line->LineId, Mesh, N, YawDeg))
	{
		RemoveHangingForLine(Line);
		return;
	}

	AActor* Host = EnsureRenderHost();
	if (!Host) return;

	UStaticMeshComponent* Comp = nullptr;
	if (TWeakObjectPtr<UStaticMeshComponent>* Existing = HangingByLine.Find(Line))
	{
		Comp = Existing->Get();
	}
	if (!Comp)
	{
		Comp = NewObject<UStaticMeshComponent>(Host);
		Comp->SetupAttachment(Host->GetRootComponent());
		Comp->RegisterComponent();
		HangingByLine.Add(Line, Comp);
	}

	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);

	// Place along wire, accounting for sag at sample point N.
	const FVector StartWS = Line->GetComponentLocation();
	const FVector Pos = FMath::Lerp(StartWS, EndWS, N);
	const float EffectiveSag = DM->GetSagForLine(StartWS, EndWS, Line->LineId);
	const float SagFactor = FMath::Clamp(4.f * N * (1.f - N), 0.f, 1.f);
	const FVector SaggedPos = Pos - FVector(0, 0, EffectiveSag * SagFactor);

	// Rotate around tangent
	const FVector Tangent = (EndWS - StartWS).GetSafeNormal();
	const FRotator Rot = FRotationMatrix::MakeFromX(Tangent).Rotator() + FRotator(0.f, YawDeg, 0.f);

	FTransform T;
	T.SetLocation(SaggedPos - FVector(0, 0, DM->Hanging.DownOffsetCm));
	T.SetRotation(Rot.Quaternion());
	T.SetScale3D(FVector(1));

	Comp->SetWorldTransform(T);
}

void UPowerLineSubsystem::Tick(float)
{
	// Process poles even if no line chunks are dirty.
	if (DirtyChunks.Num() == 0 && DirtyPoles.Num() == 0) return;

	for (const FPowerLineChunkKey& Key : DirtyChunks)
	{
		FPowerLineChunk* Chunk = Chunks.Find(Key);
		if (!Chunk) continue;

		Chunk->BatchedSegments.Reset();

		// Build
		for (int32 i = Chunk->Lines.Num() - 1; i >= 0; --i)
		{
			TWeakObjectPtr<UPowerLineComponent> WLine = Chunk->Lines[i];
			UPowerLineComponent* Line = WLine.Get();
			if (!Line)
			{
				Chunk->Lines.RemoveAtSwap(i);
				continue;
			}

			Line->BuildSegments(Chunk->BatchedSegments);
			UpdateHangingForLine(Line);
		}

		EnsureRenderComponent(Key);

		if (TWeakObjectPtr<UPowerLineRenderComponent>* RCW = RenderComponents.Find(Key))
		{
			if (UPowerLineRenderComponent* RC = RCW->Get())
			{
				RC->UpdateSegments_GameThread(Chunk->BatchedSegments);
			}
		}
	}

	// Cleanup hanging comps for destroyed lines
	for (auto It = HangingByLine.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			if (UStaticMeshComponent* C = It->Value.Get())
			{
				C->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}

	DirtyChunks.Reset();

	// ===== process dirty poles =====
	if (DirtyPoles.Num() > 0)
	{
		TArray<TWeakObjectPtr<UPowerLinePoleComponent>> ToProcess;
		ToProcess.Reserve(DirtyPoles.Num());
		for (const auto& P : DirtyPoles)
		{
			ToProcess.Add(P);
		}
		DirtyPoles.Reset();

		for (const auto& WeakPole : ToProcess)
		{
			if (UPowerLinePoleComponent* Pole = WeakPole.Get())
			{
				UpdatePoleInstance(Pole);
			}
		}
	}
}

// ============================
// Pole Component
// ============================

UPowerLinePoleComponent::UPowerLinePoleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

UStaticMesh* UPowerLinePoleComponent::ResolveMeshAndMaybeHideSource()
{
	if (PoleMesh)
	{
		return PoleMesh.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	UStaticMeshComponent* Found = nullptr;
	TArray<UStaticMeshComponent*> SMComps;
	Owner->GetComponents<UStaticMeshComponent>(SMComps);

	for (UStaticMeshComponent* SMC : SMComps)
	{
		if (SMC && SMC->GetStaticMesh())
		{
			Found = SMC;
			break;
		}
	}

	if (Found)
	{
		if (bHideSourceStaticMeshComponent)
		{
			Found->SetVisibility(false, true);
			Found->SetHiddenInGame(true, true);
		}
		return Found->GetStaticMesh();
	}

	return nullptr;
}

FTransform UPowerLinePoleComponent::GetInstanceTransformWS() const
{
	FTransform Xf = GetComponentTransform();
	Xf.SetScale3D(Xf.GetScale3D() * InstanceScale);
	return Xf;
}

void UPowerLinePoleComponent::OnRegister()
{
	Super::OnRegister();

	// subscribe to own transform changes (no Tick)
	if (!TransformChangedHandle.IsValid())
	{
		TransformChangedHandle = TransformUpdated.AddUObject(
			this, &UPowerLinePoleComponent::HandleTransformChanged);
	}

	if (UWorld* W = GetWorld())
	{
		if (UPowerLineSubsystem* Sub = W->GetSubsystem<UPowerLineSubsystem>())
		{
			Sub->RegisterPole(this);
		}
	}
}

void UPowerLinePoleComponent::OnUnregister()
{
	// unsubscribe
	if (TransformChangedHandle.IsValid())
	{
		TransformUpdated.Remove(TransformChangedHandle);
		TransformChangedHandle.Reset();
	}

	if (UWorld* W = GetWorld())
	{
		if (UPowerLineSubsystem* Sub = W->GetSubsystem<UPowerLineSubsystem>())
		{
			Sub->UnregisterPole(this);
		}
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UPowerLinePoleComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkDirty();
}
#endif

void UPowerLinePoleComponent::HandleTransformChanged(
	USceneComponent*,
	EUpdateTransformFlags,
	ETeleportType)
{
	MarkDirty();
}

void UPowerLinePoleComponent::MarkDirty()
{
	if (UWorld* W = GetWorld())
	{
		if (UPowerLineSubsystem* Sub = W->GetSubsystem<UPowerLineSubsystem>())
		{
			Sub->MarkPoleDirty(this);
		}
	}
}

void UPowerLinePoleComponent::ReRegisterPole()
{
	if (UWorld* W = GetWorld())
	{
		if (UPowerLineSubsystem* Sub = W->GetSubsystem<UPowerLineSubsystem>())
		{
			Sub->UnregisterPole(this);
			Sub->RegisterPole(this);
		}
	}
}

// ============================
// Subsystem - Poles batching
// ============================

uint64 UPowerLineSubsystem::MakePoleHISMKey(const FPowerLineChunkKey& Key, const UStaticMesh* Mesh) const
{
	uint32 H = 0;
	H = HashCombine(H, GetTypeHash(Key));
	H = HashCombine(H, GetTypeHash(Mesh));
	return (uint64)H;
}

UHierarchicalInstancedStaticMeshComponent* UPowerLineSubsystem::GetOrCreatePoleHISM(const FPowerLineChunkKey& Key, UStaticMesh* Mesh)
{
	if (!Mesh) return nullptr;

	const uint64 HKey = MakePoleHISMKey(Key, Mesh);
	if (FPoleHISMData* Existing = PoleHISMs.Find(HKey))
	{
		if (UHierarchicalInstancedStaticMeshComponent* C = Existing->HISM.Get())
		{
			return C;
		}
	}

	AActor* Host = EnsureRenderHost();
	if (!Host) return nullptr;

	UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Host);
	HISM->SetStaticMesh(Mesh);
	HISM->SetMobility(EComponentMobility::Movable);
	HISM->SetupAttachment(Host->GetRootComponent());
	HISM->RegisterComponent();

	// Defaults for poles
	HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HISM->SetGenerateOverlapEvents(false);
	HISM->CastShadow = true;

	FPoleHISMData Data;
	Data.HISM = HISM;
	PoleHISMs.Add(HKey, MoveTemp(Data));

	return HISM;
}

void UPowerLineSubsystem::AddPoleInstance(
	UPowerLinePoleComponent* Pole,
	const FPowerLineChunkKey& Key,
	UStaticMesh* Mesh,
	const FTransform& XfWS)
{
	if (!Pole || !Mesh) return;

	UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreatePoleHISM(Key, Mesh);
	if (!HISM) return;

	const uint64 HKey = MakePoleHISMKey(Key, Mesh);
	FPoleHISMData& HData = PoleHISMs.FindChecked(HKey);

	const int32 NewIndex = HISM->AddInstanceWorldSpace(XfWS);
	if (NewIndex == INDEX_NONE) return;

	if (HData.Owners.Num() <= NewIndex)
	{
		HData.Owners.SetNum(NewIndex + 1);
	}
	HData.Owners[NewIndex] = Pole;

	FPoleInstanceRef Ref;
	Ref.Key = Key;
	Ref.Mesh = Mesh;
	Ref.HISM = HISM;
	Ref.Index = NewIndex;
	PoleRefs.Add(Pole, Ref);

	Pole->bRegistered = true;
	Pole->bHasKey = true;
	Pole->CurrentKey = Key;
	Pole->CurrentHISM = HISM;
	Pole->InstanceIndex = NewIndex;
}

void UPowerLineSubsystem::RemovePoleInstance(UPowerLinePoleComponent* Pole)
{
	if (!Pole) return;

	FPoleInstanceRef* Ref = PoleRefs.Find(Pole);
	if (!Ref)
	{
		Pole->bRegistered = false;
		Pole->bHasKey = false;
		Pole->CurrentHISM = nullptr;
		Pole->InstanceIndex = INDEX_NONE;
		return;
	}

	UHierarchicalInstancedStaticMeshComponent* HISM = Ref->HISM.Get();
	UStaticMesh* Mesh = Ref->Mesh.Get();
	if (!HISM || !Mesh)
	{
		PoleRefs.Remove(Pole);
		return;
	}

	const int32 RemoveIdx = Ref->Index;
	const int32 LastIdx = HISM->GetInstanceCount() - 1;

	const uint64 HKey = MakePoleHISMKey(Ref->Key, Mesh);
	FPoleHISMData* HData = PoleHISMs.Find(HKey);

	// Handle swap-last behavior
	if (HData && HData->Owners.IsValidIndex(RemoveIdx))
	{
		if (RemoveIdx != LastIdx && HData->Owners.IsValidIndex(LastIdx))
		{
			TWeakObjectPtr<UPowerLinePoleComponent> SwappedOwner = HData->Owners[LastIdx];
			HData->Owners[RemoveIdx] = SwappedOwner;
			HData->Owners[LastIdx] = nullptr;

			if (UPowerLinePoleComponent* SwappedPole = SwappedOwner.Get())
			{
				if (FPoleInstanceRef* SwappedRef = PoleRefs.Find(SwappedPole))
				{
					SwappedRef->Index = RemoveIdx;
					SwappedPole->InstanceIndex = RemoveIdx;
				}
			}
		}
		else
		{
			HData->Owners[RemoveIdx] = nullptr;
		}
	}

	HISM->RemoveInstance(RemoveIdx);

	if (HData)
	{
		while (HData->Owners.Num() > 0 && !HData->Owners.Last().IsValid())
		{
			HData->Owners.Pop();
		}
	}

	PoleRefs.Remove(Pole);

	Pole->bRegistered = false;
	Pole->bHasKey = false;
	Pole->CurrentHISM = nullptr;
	Pole->InstanceIndex = INDEX_NONE;
}

void UPowerLineSubsystem::UpdatePoleInstance(UPowerLinePoleComponent* Pole)
{
	if (!Pole || !IsValid(Pole)) return;

	UStaticMesh* Mesh = Pole->ResolveMeshAndMaybeHideSource();
	if (!Mesh)
	{
		RemovePoleInstance(Pole);
		return;
	}

	const FTransform XfWS = Pole->GetInstanceTransformWS();
	const FPowerLineChunkKey NewKey = CalcKey(XfWS.GetLocation());

	FPoleInstanceRef* Ref = PoleRefs.Find(Pole);
	if (!Ref)
	{
		AddPoleInstance(Pole, NewKey, Mesh, XfWS);
		return;
	}

	const bool bChunkChanged = !(Ref->Key == NewKey);
	const bool bMeshChanged = (Ref->Mesh.Get() != Mesh);
	const bool bHISMInvalid = !Ref->HISM.IsValid();

	if (bChunkChanged || bMeshChanged || bHISMInvalid)
	{
		RemovePoleInstance(Pole);
		AddPoleInstance(Pole, NewKey, Mesh, XfWS);
		return;
	}

	if (UHierarchicalInstancedStaticMeshComponent* HISM2 = Ref->HISM.Get())
	{
		HISM2->UpdateInstanceTransform(Ref->Index, XfWS, true, true, true);
	}
}

void UPowerLineSubsystem::RegisterPole(UPowerLinePoleComponent* Pole)
{
	if (!Pole) return;
	MarkPoleDirty(Pole);
}

void UPowerLineSubsystem::UnregisterPole(UPowerLinePoleComponent* Pole)
{
	if (!Pole) return;
	DirtyPoles.Remove(Pole);
	RemovePoleInstance(Pole);
}

void UPowerLineSubsystem::MarkPoleDirty(UPowerLinePoleComponent* Pole)
{
	if (!Pole) return;
	DirtyPoles.Add(Pole);
}

// ============================
// Multi Pole Component
// ============================

UPowerLineMultiPoleComponent::UPowerLineMultiPoleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void UPowerLineMultiPoleComponent::OnRegister()
{
	Super::OnRegister();

	if (!TransformChangedHandle.IsValid())
	{
		TransformChangedHandle = TransformUpdated.AddUObject(
			this, &UPowerLineMultiPoleComponent::HandleTransformChanged);
	}

	EnsureRuntimeComponents();
	RebuildNow();
}

void UPowerLineMultiPoleComponent::OnUnregister()
{
	if (TransformChangedHandle.IsValid())
	{
		TransformUpdated.Remove(TransformChangedHandle);
		TransformChangedHandle.Reset();
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UPowerLineMultiPoleComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildNow();
}
#endif

void UPowerLineMultiPoleComponent::HandleTransformChanged(
	USceneComponent*,
	EUpdateTransformFlags,
	ETeleportType)
{
	RebuildNow();
}

void UPowerLineMultiPoleComponent::EnsureRuntimeComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	if (!PoleHISM)
	{
		PoleHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner);
		PoleHISM->SetupAttachment(this);
		PoleHISM->SetMobility(EComponentMobility::Movable);
		PoleHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PoleHISM->SetGenerateOverlapEvents(false);
		PoleHISM->RegisterComponent();
	}

	if (!WireRender)
	{
		WireRender = NewObject<UPowerLineRenderComponent>(Owner);
		WireRender->SetupAttachment(this);
		WireRender->RegisterComponent();
	}
}

FVector UPowerLineMultiPoleComponent::GetWirePointWS(const FPowerLinePoleNode& Node) const
{
	const FVector Local = Node.LocalPosition + FVector(0.f, 0.f, WireAttachHeightCm);
	return GetComponentTransform().TransformPosition(Local);
}

void UPowerLineMultiPoleComponent::RebuildNow()
{
	EnsureRuntimeComponents();
	if (!PoleHISM || !WireRender) return;

	PoleHISM->SetStaticMesh(PoleMesh);
	PoleHISM->ClearInstances();

	for (const FPowerLinePoleNode& Node : Nodes)
	{
		FTransform T(FQuat::Identity, Node.LocalPosition, PoleScale);
		PoleHISM->AddInstance(T);
	}

	TArray<FPowerLineSegment> Segs;
	const int32 NodeCount = Nodes.Num();
	if (NodeCount < 2)
	{
		WireRender->UpdateSegments_GameThread(Segs);
		return;
	}

	const int32 EffectiveSegments = FMath::Max(2, NumSegments);
	const int32 PairCount = bClosedLoop ? NodeCount : (NodeCount - 1);
	Segs.Reserve(PairCount * EffectiveSegments);

	for (int32 PairIdx = 0; PairIdx < PairCount; ++PairIdx)
	{
		const int32 NextIdx = (PairIdx + 1) % NodeCount;
		const FVector StartWS = GetWirePointWS(Nodes[PairIdx]);
		const FVector EndWS = GetWirePointWS(Nodes[NextIdx]);

		auto PointAt = [&](float T) {
			const FVector P = FMath::Lerp(StartWS, EndWS, T);
			const float SagFactor = FMath::Clamp(4.f * T * (1.f - T), 0.f, 1.f);
			return P - FVector(0, 0, SagAmount * SagFactor);
			};

		const int32 SampleCount = FMath::Clamp(EffectiveSegments * 8, 32, 512);
		TArray<FVector> Samples;
		Samples.Reserve(SampleCount + 1);

		TArray<float> CumLen;
		CumLen.Reserve(SampleCount + 1);

		float TotalLen = 0.f;
		FVector Prev = PointAt(0.f);
		Samples.Add(Prev);
		CumLen.Add(0.f);

		for (int32 i = 1; i <= SampleCount; ++i)
		{
			const float T = (float)i / (float)SampleCount;
			const FVector Cur = PointAt(T);
			TotalLen += FVector::Dist(Prev, Cur);
			Samples.Add(Cur);
			CumLen.Add(TotalLen);
			Prev = Cur;
		}

		if (TotalLen <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		auto EvalAtDistance = [&](float TargetLen) {
			const float ClampedTarget = FMath::Clamp(TargetLen, 0.f, TotalLen);
			for (int32 Idx = 1; Idx < CumLen.Num(); ++Idx)
			{
				if (CumLen[Idx] < ClampedTarget)
				{
					continue;
				}

				const float L0 = CumLen[Idx - 1];
				const float L1 = CumLen[Idx];
				const float A = (L1 > L0) ? ((ClampedTarget - L0) / (L1 - L0)) : 0.f;
				return FMath::Lerp(Samples[Idx - 1], Samples[Idx], A);
			}

			return Samples.Last();
			};

		for (int32 i = 0; i < EffectiveSegments; ++i)
		{
			const float L0 = (TotalLen * (float)i) / (float)EffectiveSegments;
			const float L1 = (TotalLen * (float)(i + 1)) / (float)EffectiveSegments;

			FPowerLineSegment S;
			S.Start = EvalAtDistance(L0);
			S.End = EvalAtDistance(L1);
			S.Color = LineColor;
			S.Thickness = LineThickness;
			S.DepthBias = 0.f;
			S.bScreenSpace = true;
			Segs.Add(S);
		}
	}

	WireRender->UpdateSegments_GameThread(Segs);
}
