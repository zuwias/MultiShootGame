// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiShootGameWeapon.h"
#include "BulletShell.h"
#include "MultiShootGameProjectile.h"
#include "MultiShootGame/Character/MultiShootGameCharacter.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "MultiShootGame/SaveGame/ChooseWeaponSaveGame.h"
#include "Particles/ParticleSystemComponent.h"

// Sets default values
AMultiShootGameWeapon::AMultiShootGameWeapon()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	WeaponMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponmeshComponent"));
	WeaponMeshComponent->SetupAttachment(RootComponent);
	WeaponMeshComponent->SetCastHiddenShadow(true);

	AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
	AudioComponent->SetupAttachment(RootComponent);
	AudioComponent->SetAutoActivate(false);
}

// Called when the game starts or when spawned
void AMultiShootGameWeapon::BeginPlay()
{
	Super::BeginPlay();

	const UChooseWeaponSaveGame* SaveGame = Cast<UChooseWeaponSaveGame>(
		UGameplayStatics::LoadGameFromSlot(TEXT("ChooseWeapon"), 0));
	if (SaveGame)
	{
		TArray<FWeaponInfo> WeaponInfoList;
		switch (CurrentWeaponMode)
		{
		case EWeaponMode::MainWeapon:
			WeaponInfoList = SaveGame->MainWeaponList;
			WeaponInfo = WeaponInfoList[SaveGame->MainWeaponIndex];
			break;
		case EWeaponMode::SecondWeapon:
			WeaponInfoList = SaveGame->SecondWeaponList;
			WeaponInfo = WeaponInfoList[SaveGame->SecondWeaponIndex];
			break;
		case EWeaponMode::ThirdWeapon:
			WeaponInfoList = SaveGame->ThirdWeaponList;
			WeaponInfo = WeaponInfoList[SaveGame->ThirdWeaponIndex];
			break;
		}
		WeaponMeshComponent->SetSkeletalMesh(WeaponInfo.WeaponMesh);
	}

	TimeBetweenShots = 60.0f / WeaponInfo.RateOfFire;
}

void AMultiShootGameWeapon::ShakeCamera()
{
	AMultiShootGameCharacter* MyOwner = Cast<AMultiShootGameCharacter>(GetOwner());
	if (MyOwner)
	{
		if (MyOwner->GetWeaponMode() == EWeaponMode::MainWeapon)
		{
			MyOwner->AddControllerYawInput(FMath::RandRange(-1 * WeaponInfo.CameraSpread, WeaponInfo.CameraSpread));
			MyOwner->AddControllerPitchInput(
				-1 * FMath::RandRange(-1 * WeaponInfo.CameraSpreadDown, WeaponInfo.CameraSpread));
		}

		APlayerController* PlayerController = Cast<APlayerController>(MyOwner->GetController());
		if (PlayerController)
		{
			PlayerController->ClientStartCameraShake(WeaponInfo.FireCameraShake);
		}
	}
}

void AMultiShootGameWeapon::Fire()
{
	AMultiShootGameCharacter* MyOwner = Cast<AMultiShootGameCharacter>(GetOwner());

	if (BulletCheck(MyOwner))
	{
		return;
	}

	if (MyOwner)
	{
		FVector EyeLocation;
		FRotator EyeRotation;

		if (MyOwner->GetAimed())
		{
			EyeLocation = MyOwner->CurrentFPSCamera->GetCameraComponent()->GetComponentLocation();
			EyeRotation = MyOwner->CurrentFPSCamera->GetCameraComponent()->GetComponentRotation();
		}
		else
		{
			EyeLocation = MyOwner->GetCameraComponent()->GetComponentLocation();
			EyeRotation = MyOwner->GetCameraComponent()->GetComponentRotation();
		}

		FVector ShotDirection = EyeRotation.Vector();

		const float HalfRad = FMath::DegreesToRadians(WeaponInfo.BulletSpread);
		ShotDirection = FMath::VRandCone(ShotDirection, HalfRad, HalfRad);

		const FVector TraceEnd = EyeLocation + (ShotDirection * 3000.f);

		if (Cast<AMultiShootGameFPSCamera>(this))
		{
			const FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(EyeLocation, TraceEnd);

			const FRotator TargetRotation = FRotator(0, LookAtRotation.Yaw - 90.f, LookAtRotation.Pitch * -1.f);

			Cast<AMultiShootGameCharacter>(GetOwner())->GetFPSCameraSceneComponent()->SetWorldRotation(TargetRotation);
		}

		if (MuzzleEffect)
		{
			UGameplayStatics::SpawnEmitterAttached(MuzzleEffect, WeaponMeshComponent, MuzzleSocketName);
		}

		if (WeaponInfo.ProjectileClass)
		{
			const FVector MuzzleLocation = WeaponMeshComponent->GetSocketLocation(MuzzleSocketName);
			const FRotator ShotTargetDirection = UKismetMathLibrary::FindLookAtRotation(MuzzleLocation, TraceEnd);

			AMultiShootGameProjectileBase* Projectile = GetWorld()->SpawnActor<AMultiShootGameProjectileBase>(
				WeaponInfo.ProjectileClass, MuzzleLocation, ShotTargetDirection);
			Projectile->SetOwner(GetOwner());
			Projectile->ProjectileInitialize(WeaponInfo.BaseDamage);
		}

		if (MyOwner->GetWeaponMode() != EWeaponMode::SecondWeapon && BulletShellClass)
		{
			const FVector BulletShellLocation = WeaponMeshComponent->GetSocketLocation(BulletShellName);
			const FRotator BulletShellRotation = WeaponMeshComponent->GetComponentRotation();

			ABulletShell* BulletShell = GetWorld()->SpawnActor<ABulletShell>(
				BulletShellClass, BulletShellLocation, BulletShellRotation);
			BulletShell->SetOwner(this);
			BulletShell->ThrowBulletShell();
		}

		ShakeCamera();

		BulletFire(MyOwner);

		AudioComponent->Play();

		LastFireTime = GetWorld()->TimeSeconds;
	}
}

void AMultiShootGameWeapon::StartFire()
{
	const float FirstDelay = FMath::Max(LastFireTime + TimeBetweenShots - GetWorld()->TimeSeconds, 0.0f);

	GetWorldTimerManager().SetTimer(TimerHandle, this, &AMultiShootGameWeapon::Fire, TimeBetweenShots, true,
	                                FirstDelay);
}

void AMultiShootGameWeapon::StopFire()
{
	GetWorldTimerManager().ClearTimer(TimerHandle);
}

void AMultiShootGameWeapon::FireOfDelay()
{
	if (LastFireTime == 0)
	{
		Fire();

		return;
	}

	const float CurrentFireTime = GetWorld()->GetTimeSeconds();

	if (CurrentFireTime - LastFireTime > WeaponInfo.DelayOfShotgun)
	{
		Fire();
	}
}

void AMultiShootGameWeapon::EnablePhysicsSimulate()
{
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	WeaponMeshComponent->SetCollisionProfileName("BlockAll");
	WeaponMeshComponent->SetSimulatePhysics(true);
}

void AMultiShootGameWeapon::ReloadShowMagazine(bool Enabled)
{
	if (Enabled)
	{
		WeaponMeshComponent->UnHideBoneByName(ClipBoneName);
	}
	else
	{
		WeaponMeshComponent->HideBoneByName(ClipBoneName, PBO_None);
		if (WeaponInfo.MagazineMesh)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AMagazine* CurrentMagazine = GetWorld()->SpawnActor<AMagazine>(
				MagazineClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
			CurrentMagazine->SetOwner(this);
			CurrentMagazine->AttachToComponent(WeaponMeshComponent,
			                                   FAttachmentTransformRules::SnapToTargetIncludingScale, FName("Magazine"));

			CurrentMagazine->ThrowMagazine(WeaponInfo.MagazineMesh);
		}
	}
}

bool AMultiShootGameWeapon::BulletCheck(AMultiShootGameCharacter* MyOwner)
{
	if (WeaponInfo.BulletNumber == 0 && WeaponInfo.MaxBulletNumber == 0)
	{
		return true;
	}

	return false;
}

void AMultiShootGameWeapon::BulletFire(AMultiShootGameCharacter* MyOwner)
{
	if (WeaponInfo.BulletNumber > 0)
	{
		WeaponInfo.BulletNumber--;
	}
	else
	{
		MyOwner->BeginReload();
	}
}

void AMultiShootGameWeapon::BulletReload()
{
	if (WeaponInfo.MaxBulletNumber > WeaponInfo.PerBulletNumber)
	{
		if (WeaponInfo.BulletNumber < WeaponInfo.PerBulletNumber)
		{
			const int TempNumber = WeaponInfo.PerBulletNumber - WeaponInfo.BulletNumber;
			WeaponInfo.BulletNumber = WeaponInfo.PerBulletNumber;
			WeaponInfo.MaxBulletNumber -= TempNumber;
		}
		else
		{
			WeaponInfo.BulletNumber += WeaponInfo.PerBulletNumber;
			WeaponInfo.MaxBulletNumber -= WeaponInfo.PerBulletNumber;
		}
	}
	else
	{
		if (WeaponInfo.BulletNumber + WeaponInfo.MaxBulletNumber > WeaponInfo.PerBulletNumber)
		{
			const int TempNumber = WeaponInfo.PerBulletNumber - WeaponInfo.BulletNumber;
			WeaponInfo.BulletNumber = WeaponInfo.PerBulletNumber;
			WeaponInfo.MaxBulletNumber -= TempNumber;;
		}
		else
		{
			WeaponInfo.BulletNumber += WeaponInfo.MaxBulletNumber;
			WeaponInfo.MaxBulletNumber = 0;
		}
	}
}

UAudioComponent* AMultiShootGameWeapon::GetAudioComponent() const
{
	return AudioComponent;
}

USkeletalMeshComponent* AMultiShootGameWeapon::GetWeaponMeshComponent() const
{
	return WeaponMeshComponent;
}
