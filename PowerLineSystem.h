#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "PowerLineSystem.generated.h"

// ============================
// District Data Manager (per area)
// Place one APowerLineDistrictDataManager per district OR reference it manually from components.
// ============================

USTRUCT(BlueprintType)
struct FPowerLineSagSettings
{
	GENERATED_BODY()

	// Random sag amount in centimeters (applied downward, positive value).
	// Example: Min=40, Max=120
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (ClampMin = "0"))
	FVector2D SagRangeCm = FVector2D(40.f, 120.f);

	// Additional multiplier for the whole district.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (ClampMin = "0"))
	float SagScale = 1.0f;

	// If true, sag will be stable (not changing every rebuild) for the same line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag")
	bool bDeterministic = true;

	// Base seed for deterministic mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (EditCondition = "bDeterministic"))
	int32 Seed = 1337;
};

USTRUCT(BlueprintType)
struct FPowerLineSegmentsSettings
{
	GENERATED_BODY()

	// Auto compute segments from line length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments")
	bool bAutoSegments = true;

	// Desired segment length in cm (used when bAutoSegments=true).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "10"))
	float TargetSegmentLengthCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 MinSegments = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 MaxSegments = 64;

	// Used when bAutoSegments=false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 FixedSegments = 12;
};

// ============================
// Hanging objects (rare meshes placed on wires)
// ============================

USTRUCT(BlueprintType)
struct FPowerLineHangingSettings
{
	GENERATED_BODY()

	// Pool of meshes that can appear on a wire (ex: shoes). If empty -> feature disabled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	TArray<TObjectPtr<UStaticMesh>> MeshPool;

	// Chance to spawn ONE mesh on a wire. Typical values: 0.01 .. 0.10
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "1"))
	float ChancePerWire = 0.03f;

	// Place along wire in normalized distance range [0..1].
	// Use to avoid near-end placement. Example: 0.2 .. 0.8
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D NormalizedDistanceRange = FVector2D(0.2f, 0.8f);

	// Additional offset down from the wire point (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	float DownOffsetCm = 10.f;

	// Random yaw around wire tangent (degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "180"))
	float RandomYawDeg = 15.f;

	// If true, hanging placement will be stable (same wire => same result).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	bool bDeterministic = true;

	// Base seed for deterministic mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (EditCondition = "bDeterministic"))
	int32 Seed = 24601;
};

UCLASS(BlueprintType)
class PROGRAMM_API APowerLineDistrictDataManager : public AActor
{
	GENERATED_BODY()

public:
	APowerLineDistrictDataManager();

	// Optional district id, used for auto-find from components.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FName DistrictId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineSagSettings Sag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineSegmentsSettings Segments;

	// Rare meshes placed on wires.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineHangingSettings Hanging;

	// Get sag (cm, positive value -> downward sag).
	// LineId can be used to diversify multiple wires between same points.
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	float GetSagForLine(const FVector& StartWS, const FVector& EndWS, int32 LineId = 0) const;

	// Get number of segments for a given length (cm).
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	int32 GetSegmentsForLength(float LengthCm) const;

	// Decide if a wire should have a hanging mesh and produce deterministic placement.
	// Returns false if disabled or chance failed.
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	bool GetHangingForLine(
		const FVector& StartWS,
		const FVector& EndWS,
		int32 LineId,
		UStaticMesh*& OutMesh,
		float& OutNormalizedDistance,
		float& OutYawDeg) const;

	// Mark all wires that reference this manager dirty (useful after changing settings at runtime).
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	void MarkAllDistrictWiresDirty();

protected:
	static uint32 HashLine(const FVector& A, const FVector& B, int32 LineId);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// ============================
// Segment
// ============================

struct FPowerLineSegment
{
	FVector Start;
	FVector End;
	FColor Color = FColor::White;
	float Thickness = 1.f;
	float DepthBias = 0.f;
	bool bScreenSpace = true;
};

struct FPowerLineChunkKey
{
	FIntPoint Coord;

	bool operator==(const FPowerLineChunkKey& O) const { return Coord == O.Coord; }

	friend uint32 GetTypeHash(const FPowerLineChunkKey& K) { return GetTypeHash(K.Coord); }
};

class UPowerLineSubsystem;

// ============================
// Render Component (one per chunk)
// ============================

UCLASS()
class PROGRAMM_API UPowerLineRenderComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPowerLineRenderComponent();

	// GT buffers
	TArray<FPowerLineSegment> FrontBuffer;
	TArray<FPowerLineSegment> BackBuffer;

	FCriticalSection Mutex;

	// Called from Subsystem on GT
	void UpdateSegments_GameThread(const TArray<FPowerLineSegment>& Segs);

	// UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void SendRenderDynamicData_Concurrent() override;

	// Small helper: cached bounds to avoid scanning every CalcBounds call
	mutable FBoxSphereBounds CachedBounds;
	void RebuildCachedBounds_GT();
};

// ============================
// Attach lookup
// ============================

UENUM(BlueprintType)
enum class EPowerLineAttachLookup : uint8
{
	// Prefer AttachId on UPowerLineComponent; fallback to ComponentTags[0] then component name
	ByAttachId UMETA(DisplayName = "By AttachId (recommended)"),

	// Find target SceneComponent that has ComponentTag == Key
	ByComponentTag UMETA(DisplayName = "By Component Tag"),

	// Find target SceneComponent by exact component name
	ByComponentName UMETA(DisplayName = "By Component Name"),
};

// ============================
// PowerLine Component (this IS the attach point)
// Add several of these to a pole/building in Editor.
// Each component draws one wire from itself to matching point on TargetActor.
// ============================

UCLASS(ClassGroup = (Power), meta = (BlueprintSpawnableComponent))
class PROGRAMM_API UPowerLineComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPowerLineComponent();

	// Your attach id (key). If None -> fallback to ComponentTags[0], then component name.
	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	FName AttachId = NAME_None;

	// How to find matching point on TargetActor
	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	EPowerLineAttachLookup TargetLookup = EPowerLineAttachLookup::ByAttachId;

	// Optional override: if set, will search THIS key on target instead of this component key.
	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	FName TargetAttachIdOverride = NAME_None;

	// If TargetActor set => end = matching attach component on TargetActor (by key)
	UPROPERTY(EditAnywhere, Category = "PowerLine|Target")
	AActor* TargetActor = nullptr;

	// If TargetActor is null OR matching point not found => use ManualEndPointWS
	UPROPERTY(EditAnywhere, Category = "PowerLine|Target")
	FVector ManualEndPointWS = FVector::ZeroVector;

	// ============================
	// District Manager (per-area settings)
	// ============================

	// Optional direct reference to district settings manager.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District")
	TObjectPtr<APowerLineDistrictDataManager> DistrictManager = nullptr;

	// If DistrictManager is not set, component can try to auto-find it in the world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District")
	bool bAutoFindDistrictDataManager = true;

	// If set, auto-find will prefer managers with matching DistrictId.
	// If None, and only one manager exists in world, it will be used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District", meta = (EditCondition = "bAutoFindDistrictDataManager"))
	FName DistrictId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape")
	float SagAmount = 50.f;

	// Optional id to diversify deterministic random sag for multiple wires between same points.
	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape")
	int32 LineId = 0;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape", meta = (ClampMin = "2"))
	int32 NumSegments = 8;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Render", meta = (ClampMin = "0.1"))
	float LineThickness = 2.f;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Render")
	FColor LineColor = FColor::Black;

	// Call if you change params from code
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	void MarkDirty();

	// Returns effective key for this attach point
	UFUNCTION(BlueprintCallable, Category = "PowerLine|Attach")
	FName GetAttachKey() const;

	// Internal use
	void BuildSegments(TArray<FPowerLineSegment>& Out) const;

	// Current chunk tracking (so moving actor moves between chunks w/o Tick)
	bool bRegistered = false;
	FPowerLineChunkKey CurrentKey;
	bool bHasKey = false;

private:
	// Own transform changes
	FDelegateHandle TransformChangedHandle;
	void HandleTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// Target transform changes (so wires update when the other pole/building moves)
	FDelegateHandle TargetTransformChangedHandle;
	void BindToTarget();
	void UnbindFromTarget();
	void HandleTargetTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

public:
	// Resolve effective district manager (manual or auto-found)
	APowerLineDistrictDataManager* ResolveDistrictManager() const;

	// Get current endpoint (resolved target attach point or manual end).
	// Returns true if connected to TargetActor.
	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	bool GetResolvedEndPointWS(FVector& OutEnd) const;

	// Resolve endpoint
	bool ResolveEndPoint(FVector& OutEnd) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// ============================
// Pole Component (batched via HISM instances in subsystem)
// Add this to pole actors to render thousands of poles with a few draw calls.
// ============================

UCLASS(ClassGroup = (Power), meta = (BlueprintSpawnableComponent))
class PROGRAMM_API UPowerLinePoleComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPowerLinePoleComponent();

	// Mesh to instance. If null, component will try to find a UStaticMeshComponent on the same actor and use its mesh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	TObjectPtr<UStaticMesh> PoleMesh = nullptr;

	// If PoleMesh is null and a UStaticMeshComponent is found on the same actor, hide it to avoid double-render.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	bool bHideSourceStaticMeshComponent = true;

	// Optional per-instance scale multiplier.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	FVector InstanceScale = FVector(1, 1, 1);

	// Call if you changed mesh/settings from code.
	UFUNCTION(BlueprintCallable, Category = "PowerLine|Pole")
	void MarkDirty();

	// Rebuild (unregister + register) - useful when swapping meshes at runtime.
	UFUNCTION(BlueprintCallable, Category = "PowerLine|Pole")
	void ReRegisterPole();

	// Current chunk tracking (so moving actor moves between chunks w/o Tick)
	bool bRegistered = false;
	FPowerLineChunkKey CurrentKey;
	bool bHasKey = false;

private:
	// Own transform changes
	FDelegateHandle TransformChangedHandle;
	void HandleTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	UStaticMesh* ResolveMeshAndMaybeHideSource();
	FTransform GetInstanceTransformWS() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Subsystem instance bookkeeping (index inside HISM).
	UPROPERTY(Transient)
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> CurrentHISM;

	int32 InstanceIndex = INDEX_NONE;

	friend class UPowerLineSubsystem;
};

// ============================
// Chunk data
// ============================

struct FPowerLineChunk
{
	TArray<TWeakObjectPtr<UPowerLineComponent>> Lines;
	TArray<FPowerLineSegment> BatchedSegments;
	bool bDirty = true;
};

// ============================
// Subsystem (autonomous)
// ============================

UCLASS()
class PROGRAMM_API UPowerLineSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// Tune
	UPROPERTY(EditAnywhere, Category = "PowerLine")
	float ChunkSize = 10000.f;

	// UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
	}

	// Tick
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UPowerLineSubsystem, STATGROUP_Tickables);
	}

	// API for component
	void RegisterPowerLine(UPowerLineComponent* Line);
	void UnregisterPowerLine(UPowerLineComponent* Line);
	void MarkPowerLineDirty(UPowerLineComponent* Line);

	// Poles batching (HISM)
	void RegisterPole(UPowerLinePoleComponent* Pole);
	void UnregisterPole(UPowerLinePoleComponent* Pole);
	void MarkPoleDirty(UPowerLinePoleComponent* Pole);

	// Hanging mesh helpers
	void UpdateHangingForLine(UPowerLineComponent* Line);
	void RemoveHangingForLine(UPowerLineComponent* Line);

private:
	// Hidden host actor for render components (spawned once)
	AActor* EnsureRenderHost();
	FPowerLineChunkKey CalcKey(const FVector& Pos) const;
	void EnsureRenderComponent(const FPowerLineChunkKey& Key);

	// Move line between chunks if needed
	void UpdateLineChunk(UPowerLineComponent* Line, const FPowerLineChunkKey& NewKey);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> RenderHost;

	TMap<FPowerLineChunkKey, FPowerLineChunk> Chunks;
	TMap<FPowerLineChunkKey, TWeakObjectPtr<UPowerLineRenderComponent>> RenderComponents;
	TSet<FPowerLineChunkKey> DirtyChunks;

	// ===== Poles batching =====
	struct FPoleHISMData
	{
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM;
		// Owner for each instance index (needed because RemoveInstance swaps last).
		TArray<TWeakObjectPtr<UPowerLinePoleComponent>> Owners;
	};

	struct FPoleInstanceRef
	{
		FPowerLineChunkKey Key;
		TObjectPtr<UStaticMesh> Mesh = nullptr;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM;
		int32 Index = INDEX_NONE;
	};

	TMap<TWeakObjectPtr<UPowerLinePoleComponent>, FPoleInstanceRef> PoleRefs;
	TMap<uint64, FPoleHISMData> PoleHISMs; // (ChunkKey+Mesh) -> HISM data
	TSet<TWeakObjectPtr<UPowerLinePoleComponent>> DirtyPoles;

	uint64 MakePoleHISMKey(const FPowerLineChunkKey& Key, const UStaticMesh* Mesh) const;
	UHierarchicalInstancedStaticMeshComponent* GetOrCreatePoleHISM(const FPowerLineChunkKey& Key, UStaticMesh* Mesh);
	void AddPoleInstance(UPowerLinePoleComponent* Pole, const FPowerLineChunkKey& Key, UStaticMesh* Mesh, const FTransform& XfWS);
	void RemovePoleInstance(UPowerLinePoleComponent* Pole);
	void UpdatePoleInstance(UPowerLinePoleComponent* Pole);

	// One (optional) static mesh component per wire
	TMap<TWeakObjectPtr<UPowerLineComponent>, TWeakObjectPtr<UStaticMeshComponent>> HangingByLine;
};