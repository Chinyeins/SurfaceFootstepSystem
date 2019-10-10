// Copyright 2019 Urszula Kustra. All Rights Reserved.

#include "AnimNotify_SurfaceFootstep.h"
#include "FootstepInterface.h"
#include "FootstepComponent.h"
#include "FootstepActor.h"
#include "FootstepDataAsset.h"
#include "SurfaceFootstepSystemSettings.h"
#include "FoostepPoolingManagerComponent.h"
#include "FootstepTypes.h"
#include "Engine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/SkeletalMeshComponent.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "FAnimNotify_SurfaceFootstep"

UAnimNotify_SurfaceFootstep::UAnimNotify_SurfaceFootstep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(0, 188, 0, 255);
#endif

	FootstepSettings = USurfaceFootstepSystemSettings::Get();

	if (FootstepSettings)
	{
		FootstepCategory = FootstepSettings->GetCategoriesNum() > 0 ? FootstepSettings->GetCategoryName(0) : FGameplayTag::EmptyTag;
	}
}

void UAnimNotify_SurfaceFootstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	Super::Notify(MeshComp, Animation);

	// Check the most important conditions
	if ( !(FootstepSettings && MeshComp && MeshComp->GetWorld() && MeshComp->GetWorld()->GetNetMode() != NM_DedicatedServer && MeshComp->GetOwner() && MeshComp->GetOwner()->GetClass()->ImplementsInterface(UFootstepInterface::StaticClass())) ) { return; }

	if (FootstepSettings->GetCategoriesNum() == 0)
	{
		FMessageLog("PIE").Error(LOCTEXT("InvalidCategory", "There is no Footstep Category. Add any Footstep Category in the Surface Footstep System Settings in the Project Settings."));
		return;
	}
	else if (!FootstepSettings->ContainsCategory(FootstepCategory))
	{
		FMessageLog("PIE").Error( FText::Format(LOCTEXT("InvalidCategory", "\"{0}\" category is invalid. Add this Footstep Category in the Surface Footstep System Settings in the Project Settings or use a proper Footstep Category in the Surface Footstep Anim Notify."), FText::FromName(FootstepCategory.GetTagName())) );
		return;
	}

	// Ensure the World Settings implements Footstep Interface
	UFoostepPoolingManagerComponent* PoolingManager = nullptr;
	if (const IFootstepInterface* FootstepInterface = Cast<IFootstepInterface>(MeshComp->GetWorld()->GetWorldSettings()))
	{
		PoolingManager = FootstepInterface->GetPoolingManagerComponent();
	}

	if (!PoolingManager)
	{
		FMessageLog("PIE").Error(LOCTEXT("InvalidWorldSettings", "Your Worlds Settings class doesn't implement a Footstep Interface. Change the World Settings class to the FootstepWorldSettings in the Project Settings or implement a Footstep Interface and override the \"GetPoolingManagerComponent\" function in your World Settings C++ class."));
		return;
	}

	UFootstepComponent* FootstepComponent = IFootstepInterface::Execute_GetFootstepComponent(MeshComp->GetOwner());

	if (!FootstepComponent) { return; }

	// Prepare tracing
	const bool bUseFootSocketLocation = TraceFromFootSocket() && MeshComp->DoesSocketExist(FootSocket);
	const FVector StartTrace = bUseFootSocketLocation ? MeshComp->GetSocketLocation(FootSocket) : MeshComp->GetComponentLocation();

	FVector DirectionVector = FVector(0.f);
	if (bUseFootSocketLocation)
	{
		const FRotator SocketRotation = MeshComp->GetSocketRotation(FootSocket);

		switch (FootstepTraceDirection)
		{
		case EFootstepTraceDirection::Down:
			DirectionVector = FRotationMatrix(SocketRotation).GetScaledAxis(EAxis::Z) * -1.f;
			break;
		case EFootstepTraceDirection::Up:
			DirectionVector = FRotationMatrix(SocketRotation).GetScaledAxis(EAxis::Z);
			break;
		case EFootstepTraceDirection::Forward:
			DirectionVector = SocketRotation.Vector();
			break;
		case EFootstepTraceDirection::Backward:
			DirectionVector = SocketRotation.Vector() * -1.f;
			break;
		case EFootstepTraceDirection::Right:
			DirectionVector = FRotationMatrix(SocketRotation).GetScaledAxis(EAxis::Y);
			break;
		case EFootstepTraceDirection::Left:
			DirectionVector = FRotationMatrix(SocketRotation).GetScaledAxis(EAxis::Y) * -1.f;
			break;
		}
	}
	else
	{
		switch (FootstepTraceDirection)
		{
		case EFootstepTraceDirection::Down:
			DirectionVector = MeshComp->GetUpVector() * -1.f;
			break;
		case EFootstepTraceDirection::Up:
			DirectionVector = MeshComp->GetUpVector();
			break;
		case EFootstepTraceDirection::Forward:
			DirectionVector = MeshComp->GetForwardVector();
			break;
		case EFootstepTraceDirection::Backward:
			DirectionVector = MeshComp->GetForwardVector() * -1.f;
			break;
		case EFootstepTraceDirection::Right:
			DirectionVector = MeshComp->GetRightVector();
			break;
		case EFootstepTraceDirection::Left:
			DirectionVector = MeshComp->GetRightVector() * -1.f;
			break;
		}
	}

	FHitResult TraceHitResult;
	FootstepComponent->CreateFootstepLineTrace(StartTrace, DirectionVector, TraceHitResult);

	if (!TraceHitResult.bBlockingHit) { return; }

	// Get the Physical Material from the TraceHitResult
	UPhysicalMaterial* PhysMat = nullptr;
	if (TraceHitResult.PhysMaterial.IsValid())
	{
		PhysMat = TraceHitResult.PhysMaterial.Get();
	}
	else if (TraceHitResult.GetComponent())
	{
		const FBodyInstance* BodyInstance = TraceHitResult.GetComponent()->GetBodyInstance();
		if (BodyInstance)
		{
			PhysMat = BodyInstance->GetSimplePhysicalMaterial();
		}
	}

	if (!PhysMat) { return; }

	// Get data from a Data Asset
	if (const UFootstepDataAsset* FootstepData = FootstepComponent->GetFootstepData(PhysMat->SurfaceType))
	{
		if (GEngine && FootstepComponent->GetShowDebug())
		{
			const FString PhysMatName = PhysMat->GetName();
			const FString DataAssetName = FootstepData->GetName();
			const FString AnimationName = Animation->GetName();
			const FString CategoryName = FootstepCategory.ToString();
			const FString SocketName = TraceFromFootSocket() ? FootSocket.ToString() : TEXT("ROOT");
			const FString OwnerName = GetActorName(MeshComp->GetOwner());

			const FString DebugMessage = TEXT("PhysMat: ") + PhysMatName + TEXT(", DataAsset: ") + DataAssetName + TEXT(", Anim: ") + AnimationName + TEXT(", Category: ") + CategoryName + TEXT(", Socket: ") + SocketName + TEXT(", Owner: ") + OwnerName;

			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, DebugMessage);
			UE_LOG(LogFootstep, Log, TEXT("%s"), *DebugMessage);
		}

		USoundBase* FootstepSound = FootstepData->GetSound(FootstepCategory);
		UObject* FootstepParticle = FootstepData->GetParticle(FootstepCategory);
		
		// Finally, activate a footstep actor
		if (FootstepSound || FootstepParticle)
		{
			PoolingManager->SafeSpawnPooledActor();

			if (AFootstepActor* FootstepActor = PoolingManager->GetPooledActor())
			{
				FootstepActor->SetPoolingActive(false);

				const FTransform WorldTransform = FTransform(FRotationMatrix::MakeFromZ(TraceHitResult.ImpactNormal).Rotator(), TraceHitResult.ImpactPoint, FVector(1.f));
				const FVector RelLocationVFX = FootstepData->GetRelScaleParticle();

				FootstepActor->SetActorTransform(WorldTransform);

				FootstepActor->InitSound(FootstepSound, FootstepData->GetVolume(), FootstepData->GetPitch(), FootstepComponent->GetPlaySound2D(), FootstepData->GetAttenuationOverride(), FootstepData->GetConcurrencyOverride());
				FootstepActor->InitParticle(FootstepParticle, RelLocationVFX);

				FootstepActor->SetLifeSpan(FootstepData->GetFootstepLifeSpan());
				FootstepActor->SetPoolingActive(true);
			}
		}
	}
}

FString UAnimNotify_SurfaceFootstep::GetNotifyName_Implementation() const
{
	return TraceFromFootSocket() ? Super::GetNotifyName_Implementation() + TEXT("_") + FootSocket.ToString() : Super::GetNotifyName_Implementation();
}

bool UAnimNotify_SurfaceFootstep::TraceFromFootSocket() const
{
	return bTraceFromFootSocket && FootSocket != NAME_None;
}

FString UAnimNotify_SurfaceFootstep::GetActorName(const AActor* Actor) const
{
	if (!Actor) { return FString(); }

#if WITH_EDITOR
	return Actor->GetActorLabel();
#else
	return Actor->GetName();
#endif

}

#undef LOCTEXT_NAMESPACE
