#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (ClampMin = "0"))
	FVector2D SagRangeCm = FVector2D(40.f, 120.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (ClampMin = "0"))
	float SagScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag")
	bool bDeterministic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Sag", meta = (EditCondition = "bDeterministic"))
	int32 Seed = 1337;
};

USTRUCT(BlueprintType)
struct FPowerLineSegmentsSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments")
	bool bAutoSegments = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "10"))
	float TargetSegmentLengthCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 MinSegments = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 MaxSegments = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Segments", meta = (ClampMin = "1"))
	int32 FixedSegments = 12;
};

USTRUCT(BlueprintType)
struct FPowerLineHangingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	TArray<TObjectPtr<UStaticMesh>> MeshPool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "1"))
	float ChancePerWire = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D NormalizedDistanceRange = FVector2D(0.2f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	float DownOffsetCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (ClampMin = "0", ClampMax = "180"))
	float RandomYawDeg = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging")
	bool bDeterministic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Hanging", meta = (EditCondition = "bDeterministic"))
	int32 Seed = 24601;
};

UCLASS(BlueprintType)
class PROGRAMM_API APowerLineDistrictDataManager : public AActor
{
	GENERATED_BODY()
public:
	APowerLineDistrictDataManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FName DistrictId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineSagSettings Sag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineSegmentsSettings Segments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	FPowerLineHangingSettings Hanging;

	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	float GetSagForLine(const FVector& StartWS, const FVector& EndWS, int32 LineId = 0) const;

	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	int32 GetSegmentsForLength(float LengthCm) const;

	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	bool GetHangingForLine(const FVector& StartWS, const FVector& EndWS, int32 LineId,
		UStaticMesh*& OutMesh, float& OutNormalizedDistance, float& OutYawDeg) const;

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

	friend uint32 GetTypeHash(const FPowerLineChunkKey& K)
	{
		return GetTypeHash(K.Coord);
	}
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

	TArray<FPowerLineSegment> FrontBuffer;
	TArray<FPowerLineSegment> BackBuffer;
	FCriticalSection Mutex;

	void UpdateSegments_GameThread(const TArray<FPowerLineSegment>& Segs);

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void SendRenderDynamicData_Concurrent() override;

	mutable FBoxSphereBounds CachedBounds;
	void RebuildCachedBounds_GT();
};

// ============================
// Attach lookup
// ============================
UENUM(BlueprintType)
enum class EPowerLineAttachLookup : uint8
{
	ByAttachId UMETA(DisplayName = "By AttachId (recommended)"),
	ByComponentTag UMETA(DisplayName = "By Component Tag"),
	ByComponentName UMETA(DisplayName = "By Component Name"),
};

// ============================
// PowerLine Component (this IS the attach point)
// Each component draws one wire from itself to matching point on TargetActor.
// ============================
UCLASS(ClassGroup = (Power), meta = (BlueprintSpawnableComponent))
class PROGRAMM_API UPowerLineComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPowerLineComponent();

	// NEW: if false, the wire is not generated at all (no segments, no hanging meshes).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	FName AttachId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	EPowerLineAttachLookup TargetLookup = EPowerLineAttachLookup::ByAttachId;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Attach")
	FName TargetAttachIdOverride = NAME_None;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Target")
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Target")
	FVector ManualEndPointWS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District")
	TObjectPtr<APowerLineDistrictDataManager> DistrictManager = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District")
	bool bAutoFindDistrictDataManager = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|District", meta = (EditCondition = "bAutoFindDistrictDataManager"))
	FName DistrictId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape")
	float SagAmount = 50.f;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape")
	int32 LineId = 0;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Shape", meta = (ClampMin = "2"))
	int32 NumSegments = 8;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Render", meta = (ClampMin = "0.1"))
	float LineThickness = 2.f;

	UPROPERTY(EditAnywhere, Category = "PowerLine|Render")
	FColor LineColor = FColor::Black;

	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	void MarkDirty();

	UFUNCTION(BlueprintCallable, Category = "PowerLine|Attach")
	FName GetAttachKey() const;

	void BuildSegments(TArray<FPowerLineSegment>& Out) const;

	bool bRegistered = false;
	FPowerLineChunkKey CurrentKey;
	bool bHasKey = false;

private:
	FDelegateHandle TransformChangedHandle;
	void HandleTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	FDelegateHandle TargetTransformChangedHandle;
	void BindToTarget();
	void UnbindFromTarget();
	void HandleTargetTransformChanged(USceneComponent* InComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	bool ResolveEndPoint(FVector& OutEnd) const;

public:
	APowerLineDistrictDataManager* ResolveDistrictManager() const;

	UFUNCTION(BlueprintCallable, Category = "PowerLine")
	bool GetResolvedEndPointWS(FVector& OutEnd) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// ============================
// NEW: Pole component that auto-creates N wires and auto-connects by index.
// Put this on your pole actor instead of manually adding 4/8/12 UPowerLineComponent.
// ============================
UCLASS(ClassGroup = (Power), meta = (BlueprintSpawnableComponent))
class PROGRAMM_API UPowerLinePoleComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPowerLinePoleComponent();

	// How many wires exist on this pole.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole", meta = (ClampMin = "0"))
	int32 NumWires = 4;

	// Local offsets for each wire attach point (size should be NumWires).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	TArray<FVector> WireLocalOffsets;

	// Optional target pole actor to auto-connect to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	AActor* TargetPoleActor = nullptr;

	// AttachId prefix: Wire_0, Wire_1 ...
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	FName AttachPrefix = TEXT("Wire");

	// If true, when target has fewer wires, extra wires will be disabled (bEnabled=false).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PowerLine|Pole")
	bool bClampToTargetWireCount = true;

	// Rebuild child wire components now.
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "PowerLine|Pole")
	void RebuildWires();

	// Helper: returns number of wires on actor (PoleComponent preferred, fallback counts UPowerLineComponent).
	UFUNCTION(BlueprintCallable, Category = "PowerLine|Pole")
	static int32 GetWireCountOnActor(const AActor* Actor);

protected:
	virtual void OnRegister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Tag used to find auto-generated wire components.
	static FName AutoWireTag;

	void EnsureOffsetsSize();
	void CollectAutoWires(TArray<UPowerLineComponent*>& OutWires) const;
	UPowerLineComponent* CreateAutoWire(int32 Index);
	void ApplyWireSettings(UPowerLineComponent* Wire, int32 Index, int32 EffectiveConnectedCount);
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
	UPROPERTY(EditAnywhere, Category = "PowerLine")
	float ChunkSize = 10000.f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
	}

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPowerLineSubsystem, STATGROUP_Tickables); }

	void RegisterPowerLine(UPowerLineComponent* Line);
	void UnregisterPowerLine(UPowerLineComponent* Line);
	void MarkPowerLineDirty(UPowerLineComponent* Line);

	void UpdateHangingForLine(UPowerLineComponent* Line);
	void RemoveHangingForLine(UPowerLineComponent* Line);

private:
	AActor* EnsureRenderHost();
	FPowerLineChunkKey CalcKey(const FVector& Pos) const;
	void EnsureRenderComponent(const FPowerLineChunkKey& Key);
	void UpdateLineChunk(UPowerLineComponent* Line, const FPowerLineChunkKey& NewKey);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> RenderHost;

	TMap<FPowerLineChunkKey, FPowerLineChunk> Chunks;
	TMap<FPowerLineChunkKey, TWeakObjectPtr<UPowerLineRenderComponent>> RenderComponents;
	TSet<FPowerLineChunkKey> DirtyChunks;

	TMap<TWeakObjectPtr<UPowerLineComponent>, TWeakObjectPtr<UStaticMeshComponent>> HangingByLine;
};