#include "PowerLineSystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SceneManagement.h"
#include "EngineUtils.h"              // TActorIterator
#include "UObject/UObjectIterator.h"  // TObjectIterator

// ============================
// District Data Manager
// ============================

APowerLineDistrictDataManager::APowerLineDistrictDataManager()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
}

uint32 APowerLineDistrictDataManager::HashLine(const FVector& A, const FVector& B, int32 LineId)
{
	auto Q = [](const FVector& V)
		{
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
	const FVector& StartWS, const FVector& EndWS, int32 LineId,
	UStaticMesh*& OutMesh, float& OutNormalizedDistance, float& OutYawDeg) const
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
			(!Line->DistrictManager &&
				Line->bAutoFindDistrictDataManager &&
				(Line->DistrictId == NAME_None
					? (Line->ResolveDistrictManager() == this)
					: (Line->DistrictId == DistrictId)));

		if (bDirect || bAutoSameId)
		{
			Line->MarkDirty();
		}
	}
}

#if WITH_EDITOR
void APowerLineDistrictDataManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkAllDistrictWiresDirty();
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
				PDI->DrawLine(S.Start, S.End, S.Color, SDPG_World, S.Thickness, S.DepthBias, S.bScreenSpace);
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

	MarkRenderDynamicDataDirty();
	UpdateBounds();
	MarkRenderTransformDirty();
}

FPrimitiveSceneProxy* UPowerLineRenderComponent::CreateSceneProxy()
{
	return new FPowerLineSceneProxy(this);
}

FBoxSphereBounds UPowerLineRenderComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return CachedBounds;
}

void UPowerLineRenderComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();

	FPrimitiveSceneProxy* Proxy = SceneProxy;
	if (!Proxy) return;

	TArray<FPowerLineSegment> Copy;
	FBoxSphereBounds CopyBounds;

	{
		FScopeLock Lock(&Mutex);
		Copy = FrontBuffer;
		CopyBounds = CachedBounds;
	}

	ENQUEUE_RENDER_COMMAND(PowerLine_UpdateProxy)(
		[Proxy, Segs = MoveTemp(Copy), B = CopyBounds](FRHICommandListImmediate& RHICmdList) mutable
		{
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

	if (const UPowerLineComponent* PLC = Cast<UPowerLineComponent>(Comp))
	{
		if (PLC->AttachId != NAME_None)
		{
			return PLC->AttachId;
		}
	}

	if (Comp->ComponentTags.Num() > 0)
	{
		return Comp->ComponentTags[0];
	}

	return Comp->GetFName();
}

static USceneComponent* FindAttachOnActor(AActor* Actor, FName Key, EPowerLineAttachLookup LookupMode)
{
	if (!Actor || Key == NAME_None) return nullptr;

	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);

	auto MatchByAttachId = [&](UActorComponent* C) -> USceneComponent*
		{
			if (UPowerLineComponent* PLC = Cast<UPowerLineComponent>(C))
			{
				const FName K = (PLC->AttachId != NAME_None) ? PLC->AttachId : GetKeyFromSceneComponent(PLC);
				if (K == Key) return PLC;
			}
			return nullptr;
		};

	auto MatchByTag = [&](UActorComponent* C) -> USceneComponent*
		{
			if (USceneComponent* SC = Cast<USceneComponent>(C))
			{
				if (SC->ComponentHasTag(Key)) return SC;
			}
			return nullptr;
		};

	auto MatchByName = [&](UActorComponent* C) -> USceneComponent*
		{
			if (USceneComponent* SC = Cast<USceneComponent>(C))
			{
				if (SC->GetFName() == Key) return SC;
			}
			return nullptr;
		};

	for (UActorComponent* C : Comps)
	{
		if (!C) continue;

		USceneComponent* Found = nullptr;
		switch (LookupMode)
		{
		case EPowerLineAttachLookup::ByAttachId: Found = MatchByAttachId(C); break;
		case EPowerLineAttachLookup::ByComponentTag: Found = MatchByTag(C); break;
		case EPowerLineAttachLookup::ByComponentName: Found = MatchByName(C); break;
		default: break;
		}
		if (Found) return Found;
	}

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

	if (!TransformChangedHandle.IsValid())
	{
		TransformChangedHandle = TransformUpdated.AddUObject(this, &UPowerLineComponent::HandleTransformChanged);
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

	if (PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetActor) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetLookup) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, AttachId) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, TargetAttachIdOverride) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, ManualEndPointWS) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLineComponent, bEnabled))
	{
		BindToTarget();
		MarkDirty();
	}
}
#endif

void UPowerLineComponent::BindToTarget()
{
	UnbindFromTarget();

	if (!TargetActor) return;
	if (!GetWorld()) return;

	USceneComponent* TargetRoot = TargetActor->GetRootComponent();
	if (!TargetRoot) return;

	TargetTransformChangedHandle = TargetRoot->TransformUpdated.AddUObject(this, &UPowerLineComponent::HandleTargetTransformChanged);
}

void UPowerLineComponent::UnbindFromTarget()
{
	if (!TargetTransformChangedHandle.IsValid()) return;

	if (TargetActor)
	{
		if (USceneComponent* TargetRoot = TargetActor->GetRootComponent())
		{
			TargetRoot->TransformUpdated.Remove(TargetTransformChangedHandle);
		}
	}

	TargetTransformChangedHandle.Reset();
}

void UPowerLineComponent::HandleTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	MarkDirty();
}

void UPowerLineComponent::HandleTargetTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
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

FName UPowerLineComponent::GetAttachKey() const
{
	if (AttachId != NAME_None) return AttachId;
	if (ComponentTags.Num() > 0) return ComponentTags[0];
	return GetFName();
}

bool UPowerLineComponent::ResolveEndPoint(FVector& OutEnd) const
{
	if (!TargetActor)
	{
		OutEnd = ManualEndPointWS;
		if (OutEnd.IsNearlyZero())
		{
			OutEnd = GetComponentLocation();
		}
		return false;
	}

	const FName MyKey = GetAttachKey();
	const FName KeyToSearch = (TargetAttachIdOverride != NAME_None) ? TargetAttachIdOverride : MyKey;

	if (KeyToSearch == NAME_None)
	{
		OutEnd = TargetActor->GetActorLocation();
		return true;
	}

	if (USceneComponent* TargetAttach = FindAttachOnActor(TargetActor, KeyToSearch, TargetLookup))
	{
		OutEnd = TargetAttach->GetComponentLocation();
		return true;
	}

	OutEnd = TargetActor->GetActorLocation();
	return true;
}

APowerLineDistrictDataManager* UPowerLineComponent::ResolveDistrictManager() const
{
	if (IsValid(DistrictManager)) return DistrictManager.Get();
	if (!bAutoFindDistrictDataManager) return nullptr;

	UWorld* W = GetWorld();
	if (!W) return nullptr;

	APowerLineDistrictDataManager* Found = nullptr;
	int32 Count = 0;

	for (TActorIterator<APowerLineDistrictDataManager> It(W); It; ++It)
	{
		APowerLineDistrictDataManager* M = *It;
		if (!IsValid(M)) continue;

		if (DistrictId != NAME_None)
		{
			if (M->DistrictId == DistrictId) return M;
			continue;
		}

		Found = M;
		++Count;
		if (Count > 1)
		{
			return nullptr; // ambiguous
		}
	}

	return Found;
}

bool UPowerLineComponent::GetResolvedEndPointWS(FVector& OutEnd) const
{
	return ResolveEndPoint(OutEnd);
}

void UPowerLineComponent::BuildSegments(TArray<FPowerLineSegment>& Out) const
{
	// NEW: allow disabling wires completely (used by PoleComponent when target has fewer wires).
	if (!bEnabled) return;

	const FVector Start = GetComponentLocation();
	FVector End = ManualEndPointWS;
	const bool bConnected = ResolveEndPoint(End);

	float EffectiveSag = bConnected ? SagAmount : 0.f;
	int32 Segs = FMath::Max(2, NumSegments);

	if (bConnected)
	{
		if (APowerLineDistrictDataManager* Mgr = ResolveDistrictManager())
		{
			EffectiveSag = Mgr->GetSagForLine(Start, End, LineId);
			const float LenCm = FVector::Distance(Start, End);
			Segs = FMath::Max(2, Mgr->GetSegmentsForLength(LenCm));
		}
	}

	Out.Reserve(Out.Num() + Segs);

	auto Eval = [&](float T)
		{
			FVector P = FMath::Lerp(Start, End, T);
			P.Z -= FMath::Sin(T * PI) * EffectiveSag;
			return P;
		};

	for (int32 i = 0; i < Segs; ++i)
	{
		const float T0 = (float)i / (float)Segs;
		const float T1 = (float)(i + 1) / (float)Segs;

		FPowerLineSegment S;
		S.Start = Eval(T0);
		S.End = Eval(T1);
		S.Color = LineColor;
		S.Thickness = LineThickness;
		S.DepthBias = 0.f;
		S.bScreenSpace = true;
		Out.Add(S);
	}
}

// ============================
// NEW: Pole component
// ============================

FName UPowerLinePoleComponent::AutoWireTag = TEXT("AutoPowerWire");

UPowerLinePoleComponent::UPowerLinePoleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPowerLinePoleComponent::OnRegister()
{
	Super::OnRegister();
	RebuildWires();
}

#if WITH_EDITOR
void UPowerLinePoleComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropName == GET_MEMBER_NAME_CHECKED(UPowerLinePoleComponent, NumWires) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLinePoleComponent, WireLocalOffsets) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLinePoleComponent, TargetPoleActor) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLinePoleComponent, AttachPrefix) ||
		PropName == GET_MEMBER_NAME_CHECKED(UPowerLinePoleComponent, bClampToTargetWireCount))
	{
		RebuildWires();
	}
}
#endif

void UPowerLinePoleComponent::EnsureOffsetsSize()
{
	NumWires = FMath::Max(0, NumWires);

	if (WireLocalOffsets.Num() != NumWires)
	{
		const int32 Old = WireLocalOffsets.Num();
		WireLocalOffsets.SetNum(NumWires);

		// Simple default layout if new wires were added and have zero offsets:
		// spread on Y axis by 25cm.
		for (int32 i = Old; i < NumWires; ++i)
		{
			WireLocalOffsets[i] = FVector(0.f, (float)i * 25.f, 300.f);
		}
	}
}

void UPowerLinePoleComponent::CollectAutoWires(TArray<UPowerLineComponent*>& OutWires) const
{
	OutWires.Reset();

	TArray<USceneComponent*> Children;
	GetChildrenComponents(true, Children);

	for (USceneComponent* C : Children)
	{
		UPowerLineComponent* Wire = Cast<UPowerLineComponent>(C);
		if (!Wire) continue;
		if (!Wire->ComponentHasTag(AutoWireTag)) continue;
		OutWires.Add(Wire);
	}

	OutWires.Sort([](const UPowerLineComponent& A, const UPowerLineComponent& B)
		{
			return A.LineId < B.LineId;
		});
}

UPowerLineComponent* UPowerLinePoleComponent::CreateAutoWire(int32 Index)
{
	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	UPowerLineComponent* Wire = NewObject<UPowerLineComponent>(Owner);
	if (!Wire) return nullptr;

	Wire->ComponentTags.Add(AutoWireTag);
	Wire->SetupAttachment(this);
	Wire->RegisterComponent();

	return Wire;
}

int32 UPowerLinePoleComponent::GetWireCountOnActor(const AActor* Actor)
{
	if (!Actor) return 0;

	if (const UPowerLinePoleComponent* Pole = Actor->FindComponentByClass<UPowerLinePoleComponent>())
	{
		return FMath::Max(0, Pole->NumWires);
	}

	// fallback: count manual wires
	TArray<UPowerLineComponent*> Wires;
	Actor->GetComponents<UPowerLineComponent>(Wires);
	return Wires.Num();
}

void UPowerLinePoleComponent::ApplyWireSettings(UPowerLineComponent* Wire, int32 Index, int32 EffectiveConnectedCount)
{
	if (!Wire) return;

	const FString PrefixStr = AttachPrefix.ToString();
	const FName Id(*FString::Printf(TEXT("%s_%d"), *PrefixStr, Index));

	Wire->AttachId = Id;
	Wire->TargetLookup = EPowerLineAttachLookup::ByAttachId;
	Wire->TargetAttachIdOverride = Id;
	Wire->TargetActor = TargetPoleActor;

	Wire->LineId = Index;
	Wire->SetRelativeLocation(WireLocalOffsets.IsValidIndex(Index) ? WireLocalOffsets[Index] : FVector::ZeroVector);

	if (bClampToTargetWireCount && TargetPoleActor)
	{
		Wire->bEnabled = (Index < EffectiveConnectedCount);
	}
	else
	{
		Wire->bEnabled = true;
	}

	Wire->MarkDirty();
}

void UPowerLinePoleComponent::RebuildWires()
{
	EnsureOffsetsSize();

	// Determine how many wires should actually be connected (min(A,B)).
	int32 ConnectedCount = NumWires;
	if (bClampToTargetWireCount && TargetPoleActor)
	{
		const int32 TargetCount = GetWireCountOnActor(TargetPoleActor);
		ConnectedCount = FMath::Min(NumWires, TargetCount);
	}

	TArray<UPowerLineComponent*> Existing;
	CollectAutoWires(Existing);

	// Create missing
	while (Existing.Num() < NumWires)
	{
		UPowerLineComponent* NewWire = CreateAutoWire(Existing.Num());
		if (!NewWire) break;
		Existing.Add(NewWire);
	}

	// Remove extra
	for (int32 i = Existing.Num() - 1; i >= NumWires; --i)
	{
		if (Existing[i])
		{
			Existing[i]->DestroyComponent();
		}
		Existing.RemoveAt(i);
	}

	// Apply settings
	for (int32 i = 0; i < Existing.Num(); ++i)
	{
		ApplyWireSettings(Existing[i], i, ConnectedCount);
	}
}

// ============================
// Subsystem
// ============================

FPowerLineChunkKey UPowerLineSubsystem::CalcKey(const FVector& Pos) const
{
	FPowerLineChunkKey K;
	K.Coord.X = FMath::FloorToInt(Pos.X / ChunkSize);
	K.Coord.Y = FMath::FloorToInt(Pos.Y / ChunkSize);
	return K;
}

AActor* UPowerLineSubsystem::EnsureRenderHost()
{
	if (RenderHost.IsValid()) return RenderHost.Get();

	UWorld* W = GetWorld();
	if (!W) return nullptr;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	const FTransform SpawnTM = FTransform::Identity;
	AActor* Host = W->SpawnActor(AActor::StaticClass(), &SpawnTM, Params);
	if (!Host) return nullptr;

	Host->SetActorHiddenInGame(true);
	Host->SetCanBeDamaged(false);
	Host->SetActorEnableCollision(false);

	RenderHost = Host;
	return Host;
}

void UPowerLineSubsystem::EnsureRenderComponent(const FPowerLineChunkKey& Key)
{
	if (RenderComponents.Contains(Key) && RenderComponents[Key].IsValid()) return;

	AActor* Host = EnsureRenderHost();
	if (!Host) return;

	UPowerLineRenderComponent* RC = NewObject<UPowerLineRenderComponent>(Host);
	RC->RegisterComponent();

	RenderComponents.Add(Key, RC);

	RC->UpdateBounds();
	RC->MarkRenderStateDirty();
}

void UPowerLineSubsystem::UpdateLineChunk(UPowerLineComponent* Line, const FPowerLineChunkKey& NewKey)
{
	if (Line->bHasKey)
	{
		if (FPowerLineChunk* OldChunk = Chunks.Find(Line->CurrentKey))
		{
			OldChunk->Lines.Remove(Line);
			DirtyChunks.Add(Line->CurrentKey);
		}
	}

	FPowerLineChunk& NewChunk = Chunks.FindOrAdd(NewKey);
	NewChunk.Lines.Add(Line);
	DirtyChunks.Add(NewKey);

	Line->CurrentKey = NewKey;
	Line->bHasKey = true;

	EnsureRenderComponent(NewKey);
}

void UPowerLineSubsystem::RegisterPowerLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	const FPowerLineChunkKey Key = CalcKey(Line->GetComponentLocation());
	UpdateLineChunk(Line, Key);
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

	Line->bHasKey = false;
}

static bool EvalWirePointAndTangent(
	const FVector& Start, const FVector& End,
	float SagCm, int32 Segments, float NormalizedDistance,
	FVector& OutPos, FVector& OutTangent)
{
	Segments = FMath::Max(2, Segments);
	NormalizedDistance = FMath::Clamp(NormalizedDistance, 0.f, 1.f);

	auto Eval = [&](float T)
		{
			FVector P = FMath::Lerp(Start, End, T);
			P.Z -= FMath::Sin(T * PI) * SagCm;
			return P;
		};

	TArray<FVector> Pts;
	Pts.SetNum(Segments + 1);

	for (int32 i = 0; i <= Segments; ++i)
	{
		const float T = (float)i / (float)Segments;
		Pts[i] = Eval(T);
	}

	float Total = 0.f;
	TArray<float> Cum;
	Cum.SetNum(Segments + 1);
	Cum[0] = 0.f;

	for (int32 i = 1; i <= Segments; ++i)
	{
		Total += FVector::Distance(Pts[i - 1], Pts[i]);
		Cum[i] = Total;
	}

	if (Total <= KINDA_SMALL_NUMBER)
	{
		OutPos = Start;
		OutTangent = (End - Start).GetSafeNormal();
		return false;
	}

	const float TargetDist = NormalizedDistance * Total;
	int32 SegIdx = 0;

	for (int32 i = 1; i <= Segments; ++i)
	{
		if (Cum[i] >= TargetDist)
		{
			SegIdx = i - 1;
			break;
		}
	}

	const float D0 = Cum[SegIdx];
	const float D1 = Cum[SegIdx + 1];
	const float Alpha = (D1 > D0) ? ((TargetDist - D0) / (D1 - D0)) : 0.f;

	OutPos = FMath::Lerp(Pts[SegIdx], Pts[SegIdx + 1], Alpha);
	OutTangent = (Pts[SegIdx + 1] - Pts[SegIdx]).GetSafeNormal();
	return true;
}

void UPowerLineSubsystem::RemoveHangingForLine(UPowerLineComponent* Line)
{
	if (!Line) return;

	TWeakObjectPtr<UPowerLineComponent> Key(Line);
	if (TWeakObjectPtr<UStaticMeshComponent>* Found = HangingByLine.Find(Key))
	{
		if (UStaticMeshComponent* C = Found->Get())
		{
			C->DestroyComponent();
		}
		HangingByLine.Remove(Key);
	}
}

void UPowerLineSubsystem::UpdateHangingForLine(UPowerLineComponent* Line)
{
	if (!Line) return;
	if (!Line->bEnabled) { RemoveHangingForLine(Line); return; }

	UWorld* W = GetWorld();
	if (!W) return;

	const FVector Start = Line->GetComponentLocation();
	FVector End = Line->ManualEndPointWS;
	const bool bConnected = Line->GetResolvedEndPointWS(End);

	if (!bConnected)
	{
		RemoveHangingForLine(Line);
		return;
	}

	float EffectiveSag = Line->SagAmount;
	int32 Segs = FMath::Max(2, Line->NumSegments);

	APowerLineDistrictDataManager* Mgr = Line->ResolveDistrictManager();
	if (Mgr)
	{
		EffectiveSag = Mgr->GetSagForLine(Start, End, Line->LineId);
		const float LenCm = FVector::Distance(Start, End);
		Segs = FMath::Max(2, Mgr->GetSegmentsForLength(LenCm));
	}

	UStaticMesh* ChosenMesh = nullptr;
	float Norm = 0.5f;
	float Yaw = 0.f;

	const bool bShouldHave = (Mgr && Mgr->GetHangingForLine(Start, End, Line->LineId, ChosenMesh, Norm, Yaw));
	if (!bShouldHave)
	{
		RemoveHangingForLine(Line);
		return;
	}

	FVector Pos, Tangent;
	EvalWirePointAndTangent(Start, End, EffectiveSag, Segs, Norm, Pos, Tangent);

	Pos.Z -= Mgr->Hanging.DownOffsetCm;

	FRotator Rot = FRotationMatrix::MakeFromX(Tangent).Rotator();
	Rot.Yaw += Yaw;

	AActor* Host = EnsureRenderHost();
	if (!Host) return;

	TWeakObjectPtr<UPowerLineComponent> Key(Line);

	UStaticMeshComponent* SM = nullptr;
	if (TWeakObjectPtr<UStaticMeshComponent>* Existing = HangingByLine.Find(Key))
	{
		SM = Existing->Get();
	}

	if (!SM)
	{
		SM = NewObject<UStaticMeshComponent>(Host);
		SM->SetMobility(EComponentMobility::Movable);
		SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SM->SetGenerateOverlapEvents(false);
		SM->SetCastShadow(false);
		SM->RegisterComponent();

		HangingByLine.Add(Key, SM);
	}

	if (SM->GetStaticMesh() != ChosenMesh)
	{
		SM->SetStaticMesh(ChosenMesh);
	}

	SM->SetWorldLocationAndRotation(Pos, Rot);
}

void UPowerLineSubsystem::MarkPowerLineDirty(UPowerLineComponent* Line)
{
	if (!Line) return;

	const FPowerLineChunkKey NewKey = CalcKey(Line->GetComponentLocation());

	if (!Line->bHasKey || !(Line->CurrentKey == NewKey))
	{
		UpdateLineChunk(Line, NewKey);
	}
	else
	{
		DirtyChunks.Add(NewKey);
	}
}

void UPowerLineSubsystem::Tick(float)
{
	if (DirtyChunks.Num() == 0) return;

	for (const FPowerLineChunkKey& Key : DirtyChunks)
	{
		FPowerLineChunk* Chunk = Chunks.Find(Key);
		if (!Chunk) continue;

		Chunk->BatchedSegments.Reset();

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
}