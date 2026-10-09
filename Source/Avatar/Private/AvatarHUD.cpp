#include "AvatarHUD.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/BendingAttributeSet.h"
#include "CanvasItem.h"
#include "Components/BendingComponent.h"
#include "Data/BendingMoveDefinition.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingTechniqueComponent.h"
#include "Sandbox/BendingTechniqueMove.h"
#include "Sandbox/BendingTechniqueTypes.h"

namespace
{
	const FLinearColor TextColor(0.92f, 0.94f, 0.97f);
	const FLinearColor DimTextColor(0.6f, 0.64f, 0.7f);
	const FLinearColor KeyColor(1.f, 0.8f, 0.35f);
	const FLinearColor PanelColor(0.02f, 0.03f, 0.05f, 0.55f);
	const FLinearColor StartupColor(1.f, 0.72f, 0.15f);
	const FLinearColor ActiveColor(0.95f, 0.22f, 0.18f);
	const FLinearColor RecoveryColor(0.25f, 0.5f, 1.f);
	const FLinearColor CancelColor(0.3f, 1.f, 0.45f);
	const FLinearColor MudColor(0.85f, 0.6f, 0.35f);

	/** Continuous reactions arriving within this long of the last one add up on the same line. */
	constexpr double ReactionMergeSeconds = 1.5;
	constexpr int32 MaxReactionLines = 8;
	/** The frame bar keeps showing a finished move this long, fading over the second half. */
	constexpr double FrameBarHoldSeconds = 1.5;

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}

	const TCHAR* GetElementName(EBendingElement Element)
	{
		switch (Element)
		{
		case EBendingElement::Water: return TEXT("Water");
		case EBendingElement::Earth: return TEXT("Earth");
		case EBendingElement::Fire:  return TEXT("Fire");
		case EBendingElement::Air:   return TEXT("Air");
		default:                     return TEXT("None");
		}
	}

	const TCHAR* GetPhaseName(EBendingPhase Phase)
	{
		switch (Phase)
		{
		case EBendingPhase::Startup:  return TEXT("STARTUP");
		case EBendingPhase::Active:   return TEXT("ACTIVE");
		case EBendingPhase::Recovery: return TEXT("RECOVERY");
		default:                      return TEXT("DONE");
		}
	}

	FString FormatMass(double MassKg)
	{
		return MassKg < 1.0 ? FString::Printf(TEXT("%.0f g"), MassKg * 1000.0) : FString::Printf(TEXT("%.1f kg"), MassKg);
	}

	FString FormatEnergy(double EnergyJ)
	{
		if (EnergyJ < 1.0e3)
		{
			return FString::Printf(TEXT("%.0f J"), EnergyJ);
		}
		return EnergyJ < 1.0e6 ? FString::Printf(TEXT("%.1f kJ"), EnergyJ / 1.0e3) : FString::Printf(TEXT("%.2f MJ"), EnergyJ / 1.0e6);
	}

	FString DescribeReaction(EElementalReactionType Type, double MassKg, double EnergyJ)
	{
		switch (Type)
		{
		case EElementalReactionType::Evaporation:  return FString::Printf(TEXT("Evaporation: %s of water to steam"), *FormatMass(MassKg));
		case EElementalReactionType::Condensation: return FString::Printf(TEXT("Condensation: %s of steam to water"), *FormatMass(MassKg));
		case EElementalReactionType::Melting:      return FString::Printf(TEXT("Ice melted: %s"), *FormatMass(MassKg));
		case EElementalReactionType::Freezing:     return FString::Printf(TEXT("Water froze: %s"), *FormatMass(MassKg));
		case EElementalReactionType::Oxygenation:  return FString::Printf(TEXT("Air fed the fire: %s of air, %s released"), *FormatMass(MassKg), *FormatEnergy(EnergyJ));
		case EElementalReactionType::Saturation:   return FString::Printf(TEXT("Soil soaked up %s of water"), *FormatMass(MassKg));
		case EElementalReactionType::MudFormed:    return TEXT("Mud formed");
		case EElementalReactionType::Deflection:   return FString::Printf(TEXT("Air pushed matter: %s"), *FormatEnergy(EnergyJ));
		case EElementalReactionType::Erosion:      return FString::Printf(TEXT("Erosion: %s"), *FormatMass(MassKg));
		case EElementalReactionType::Extinguished: return TEXT("Fire extinguished");
		default:                                   return FString();
		}
	}

	FLinearColor GetReactionColor(EElementalReactionType Type)
	{
		switch (Type)
		{
		case EElementalReactionType::Evaporation:
		case EElementalReactionType::Condensation: return FLinearColor(0.85f, 0.88f, 0.92f);
		case EElementalReactionType::Melting:
		case EElementalReactionType::Freezing:     return FLinearColor(0.6f, 0.9f, 1.f);
		case EElementalReactionType::Oxygenation:  return FLinearColor(1.f, 0.6f, 0.2f);
		case EElementalReactionType::Saturation:
		case EElementalReactionType::MudFormed:
		case EElementalReactionType::Erosion:      return MudColor;
		case EElementalReactionType::Extinguished: return FLinearColor(1.f, 0.4f, 0.3f);
		default:                                   return TextColor;
		}
	}
}

void AAvatarHUD::BeginPlay()
{
	Super::BeginPlay();
	if (UBendingInteractionSubsystem* Subsystem = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>())
	{
		Interaction = Subsystem;
		ReactionHandle = Subsystem->OnReactionNative.AddUObject(this, &AAvatarHUD::HandleReaction);
	}
}

void AAvatarHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBendingInteractionSubsystem* Subsystem = Interaction.Get())
	{
		Subsystem->OnReactionNative.Remove(ReactionHandle);
	}
	ReactionHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

double AAvatarHUD::GetTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void AAvatarHUD::HandleReaction(const FElementalReactionEvent& Event)
{
	if (Event.Type == EElementalReactionType::None)
	{
		return;
	}
	const double Now = GetTimeSeconds();
	// Continuous reactions arrive every few frames: one growing line per kind, moved to the top when it grows.
	for (int32 Index = 0; Index < Reactions.Num(); ++Index)
	{
		if (Reactions[Index].Type == Event.Type && Now - Reactions[Index].LastTimeSeconds < ReactionMergeSeconds)
		{
			FReactionLine Line = Reactions[Index];
			Reactions.RemoveAt(Index);
			Line.MassKg += Event.MassKg;
			Line.EnergyJ += Event.EnergyJ;
			Line.LastTimeSeconds = Now;
			Reactions.Add(Line);
			return;
		}
	}
	FReactionLine Line;
	Line.Type = Event.Type;
	Line.MassKg = Event.MassKg;
	Line.EnergyJ = Event.EnergyJ;
	Line.LastTimeSeconds = Now;
	Reactions.Add(Line);
	if (Reactions.Num() > MaxReactionLines)
	{
		Reactions.RemoveAt(0);
	}
}

// ---------------------------------------------------------------------------------------------------- Drawing

void AAvatarHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UIScale = FMath::Max(static_cast<float>(Canvas->ClipY) / 1080.f, 0.6f);

	const APawn* Pawn = GetOwningPawn();
	const UBendingComponent* Bending = UBendingComponent::FindBendingComponent(Pawn);

	DrawCrosshair();
	DrawResources(Pawn);
	if (Bending)
	{
		DrawStance(Bending);
		DrawFrameBar(Bending);
	}
	DrawStatus(Pawn);
	DrawFeed(Pawn);
	DrawControls();
}

void AAvatarHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, const UFont* Font, float Scale)
{
	if (Text.IsEmpty() || !Font)
	{
		return;
	}
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale * UIScale, Scale * UIScale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f * Color.A));
	Canvas->DrawItem(Item);
}

float AAvatarHUD::GetTextWidth(const FString& Text, const UFont* Font, float Scale) const
{
	return Font ? static_cast<float>(Font->GetStringSize(*Text)) * Scale * UIScale : 0.f;
}

void AAvatarHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, const FString& Label)
{
	const float Border = 2.f * UIScale;
	DrawRect(PanelColor, X - Border, Y - Border, Width + 2.f * Border, Height + 2.f * Border);
	DrawRect(Fill, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height);
	DrawLabel(Label, X + 6.f * UIScale, Y + 1.f * UIScale, TextColor, GEngine->GetSmallFont());
}

void AAvatarHUD::DrawCrosshair()
{
	const float CenterX = 0.5f * static_cast<float>(Canvas->ClipX);
	const float CenterY = 0.5f * static_cast<float>(Canvas->ClipY);
	const float Gap = 5.f * UIScale;
	const float Length = 9.f * UIScale;
	const float Thickness = 2.f * UIScale;
	const FLinearColor Color(1.f, 1.f, 1.f, 0.9f);
	DrawLine(CenterX - Gap - Length, CenterY, CenterX - Gap, CenterY, Color, Thickness);
	DrawLine(CenterX + Gap, CenterY, CenterX + Gap + Length, CenterY, Color, Thickness);
	DrawLine(CenterX, CenterY - Gap - Length, CenterX, CenterY - Gap, Color, Thickness);
	DrawLine(CenterX, CenterY + Gap, CenterX, CenterY + Gap + Length, Color, Thickness);
	DrawRect(Color, CenterX - UIScale, CenterY - UIScale, 2.f * UIScale, 2.f * UIScale);
}

void AAvatarHUD::DrawResources(const APawn* Pawn)
{
	const UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!AbilitySystem)
	{
		return;
	}
	const float Chi = AbilitySystem->GetNumericAttribute(UBendingAttributeSet::GetChiAttribute());
	const float MaxChi = AbilitySystem->GetNumericAttribute(UBendingAttributeSet::GetMaxChiAttribute());
	const float Stamina = AbilitySystem->GetNumericAttribute(UBendingAttributeSet::GetStaminaAttribute());
	const float MaxStamina = AbilitySystem->GetNumericAttribute(UBendingAttributeSet::GetMaxStaminaAttribute());

	const float X = 30.f * UIScale;
	const float Width = 320.f * UIScale;
	const float Height = 18.f * UIScale;
	const float Y = static_cast<float>(Canvas->ClipY) - 86.f * UIScale;
	DrawBar(X, Y, Width, Height, MaxChi > 0.f ? Chi / MaxChi : 0.f, FLinearColor(0.15f, 0.65f, 1.f, 0.9f),
		FString::Printf(TEXT("Chi  %.0f / %.0f   (pays for the joules each technique moves)"), Chi, MaxChi));
	DrawBar(X, Y + 30.f * UIScale, Width, Height, MaxStamina > 0.f ? Stamina / MaxStamina : 0.f, FLinearColor(0.35f, 0.85f, 0.3f, 0.9f),
		FString::Printf(TEXT("Stamina  %.0f / %.0f"), Stamina, MaxStamina));
}

void AAvatarHUD::DrawStance(const UBendingComponent* Bending)
{
	const EBendingElement Element = Bending->GetActiveElement();
	const UFont* Medium = GEngine->GetMediumFont();
	const UFont* Small = GEngine->GetSmallFont();
	const float X = 30.f * UIScale;
	float Y = 26.f * UIScale;
	DrawRect(PanelColor, X - 12.f * UIScale, Y - 10.f * UIScale, 760.f * UIScale, 234.f * UIScale);

	// Stances on 1-4, the active one in its colour.
	static constexpr EBendingElement Stances[] = { EBendingElement::Water, EBendingElement::Earth, EBendingElement::Fire, EBendingElement::Air };
	float TabX = X;
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Stances)); ++Index)
	{
		const bool bActive = Stances[Index] == Element;
		const FLinearColor Color = bActive ? UBendingSandboxLibrary::LerpColor(UBendingSandboxLibrary::GetElementColor(Stances[Index]), FLinearColor::White, 0.35f) : DimTextColor;
		DrawLabel(FString::Printf(TEXT("[%d] %s"), Index + 1, GetElementName(Stances[Index])), TabX, Y, Color, Medium, bActive ? 1.15f : 1.f);
		TabX += 130.f * UIScale;
	}
	Y += 36.f * UIScale;

	// The stance's five techniques, the running one highlighted.
	const UBendingTechniqueMove* Running = Cast<UBendingTechniqueMove>(Bending->GetCurrentMove());
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(BendingSim::ETechniqueSlot::Count); ++SlotIndex)
	{
		const BendingSim::ETechniqueSlot Slot = static_cast<BendingSim::ETechniqueSlot>(SlotIndex);
		const EBendingTechnique Technique = BendingTechnique::FindTechnique(Element, Slot);
		const bool bRunning = Technique != EBendingTechnique::None && Running && Running->Technique == Technique;
		DrawLabel(BendingTechnique::GetKeyLabel(Slot), X, Y, bRunning ? StartupColor : KeyColor, Medium);
		if (Technique == EBendingTechnique::None)
		{
			DrawLabel(TEXT("-"), X + 64.f * UIScale, Y, DimTextColor, Medium);
		}
		else
		{
			const BendingSim::FTechniqueInfo& Info = BendingTechnique::GetInfo(Technique);
			const FString Name = BendingTechnique::GetDisplayName(Technique) + (Info.bHold ? TEXT("  (hold)") : TEXT(""));
			DrawLabel(Name, X + 64.f * UIScale, Y, bRunning ? StartupColor : TextColor, Medium);
			DrawLabel(BendingTechnique::GetDescription(Technique), X + 64.f * UIScale, Y + 19.f * UIScale, DimTextColor, Small);
		}
		Y += 38.f * UIScale;
	}
}

void AAvatarHUD::DrawFrameBar(const UBendingComponent* Bending)
{
	const double Now = GetTimeSeconds();
	const UBendingMoveDefinition* Current = Bending->GetCurrentMove();
	const EBendingPhase CurrentPhase = Bending->GetCurrentPhase();
	if (Current && CurrentPhase != EBendingPhase::None)
	{
		LastMove = Current;
		LastPhase = CurrentPhase;
		LastPhaseFrame = Bending->GetCurrentPhaseFrame();
		LastMoveSeenSeconds = Now;
	}
	const UBendingMoveDefinition* Move = LastMove.Get();
	const double SinceSeen = Now - LastMoveSeenSeconds;
	if (!Move || SinceSeen > FrameBarHoldSeconds)
	{
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>((FrameBarHoldSeconds - SinceSeen) / (0.5 * FrameBarHoldSeconds)), 0.f, 1.f);

	const FBendingFrameData& Frames = Move->FrameData;
	const int32 Startup = Frames.StartupFrames;
	const int32 Active = Frames.ActiveFrames;
	const int32 Recovery = Frames.RecoveryFrames;
	const int32 Total = FMath::Max(Frames.GetTotalFrames(), 1);
	int32 Elapsed = LastPhaseFrame;
	if (LastPhase == EBendingPhase::Active)
	{
		Elapsed += Startup;
	}
	else if (LastPhase == EBendingPhase::Recovery)
	{
		Elapsed += Startup + Active;
	}
	Elapsed = FMath::Clamp(Elapsed, 0, Total);

	const float Width = 560.f * UIScale;
	const float Height = 16.f * UIScale;
	const float X = 0.5f * (static_cast<float>(Canvas->ClipX) - Width);
	const float Y = static_cast<float>(Canvas->ClipY) - 128.f * UIScale;
	const float FrameWidth = Width / static_cast<float>(Total);
	const UFont* Medium = GEngine->GetMediumFont();
	const UFont* Small = GEngine->GetSmallFont();

	DrawRect(WithAlpha(PanelColor, Alpha), X - 10.f * UIScale, Y - 40.f * UIScale, Width + 20.f * UIScale, Height + 68.f * UIScale);
	DrawLabel(FString::Printf(TEXT("%s    %s  frame %d / %d"), *Move->DisplayName.ToString(), GetPhaseName(LastPhase),
		FMath::Min(LastPhaseFrame + 1, FMath::Max(Frames.GetPhaseFrames(LastPhase), 1)), Frames.GetPhaseFrames(LastPhase)),
		X, Y - 32.f * UIScale, WithAlpha(TextColor, Alpha), Medium);

	// Startup, active and recovery as one bar, one tick per 60 Hz frame.
	DrawRect(WithAlpha(StartupColor, Alpha), X, Y, FrameWidth * Startup, Height);
	DrawRect(WithAlpha(ActiveColor, Alpha), X + FrameWidth * Startup, Y, FrameWidth * Active, Height);
	DrawRect(WithAlpha(RecoveryColor, Alpha), X + FrameWidth * (Startup + Active), Y, FrameWidth * Recovery, Height);
	for (int32 Frame = 1; Frame < Total; ++Frame)
	{
		const float TickX = X + FrameWidth * Frame;
		DrawLine(TickX, Y, TickX, Y + Height, FLinearColor(0.f, 0.f, 0.f, 0.35f * Alpha), 1.f);
	}

	// Where recovery may be cancelled into the next move.
	if (Frames.RecoveryCancelFrame < Recovery)
	{
		const float CancelX = X + FrameWidth * (Startup + Active + Frames.RecoveryCancelFrame);
		DrawRect(WithAlpha(CancelColor, Alpha), CancelX, Y + Height + 2.f * UIScale, X + Width - CancelX, 3.f * UIScale);
	}

	// The frame being played.
	DrawRect(FLinearColor(1.f, 1.f, 1.f, Alpha), X + FrameWidth * Elapsed - 1.5f * UIScale, Y - 5.f * UIScale, 3.f * UIScale, Height + 10.f * UIScale);

	FString Legend = FString::Printf(TEXT("Startup %d   Active %d   Recovery %d   (frames at 60 Hz)"), Startup, Active, Recovery);
	if (Current && Bending->IsInCancelWindow())
	{
		Legend += TEXT("   CANCEL WINDOW");
	}
	DrawLabel(Legend, X, Y + Height + 8.f * UIScale, WithAlpha(DimTextColor, Alpha), Small);
}

void AAvatarHUD::DrawStatus(const APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	const UFont* Medium = GEngine->GetMediumFont();
	const float CenterX = 0.5f * static_cast<float>(Canvas->ClipX);
	float Y = 0.5f * static_cast<float>(Canvas->ClipY) + 40.f * UIScale;

	if (const UBendingTechniqueComponent* Techniques = Pawn->FindComponentByClass<UBendingTechniqueComponent>())
	{
		const FString Status = Techniques->GetStatusText();
		if (!Status.IsEmpty())
		{
			DrawLabel(Status, CenterX - 0.5f * GetTextWidth(Status, Medium, 1.f), Y, TextColor, Medium);
			Y += 24.f * UIScale;
		}
	}

	// Mud: the ground under the feet has lost traction.
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const UBendingInteractionSubsystem* Simulation = Interaction.Get();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Simulation && Movement && Movement->IsMovingOnGround())
	{
		const FVector Feet = Character->GetActorLocation() - FVector(0.0, 0.0, Character->GetSimpleCollisionHalfHeight());
		const float Traction = Simulation->GetSurfaceTractionMultiplierAt(Feet);
		if (Traction < 0.95f)
		{
			const FString Text = FString::Printf(TEXT("Mud underfoot: traction %.0f%%"), 100.f * Traction);
			DrawLabel(Text, CenterX - 0.5f * GetTextWidth(Text, Medium, 1.f), Y, MudColor, Medium);
		}
	}
}

void AAvatarHUD::DrawFeed(const APawn* Pawn)
{
	const double Now = GetTimeSeconds();
	const UFont* Font = GEngine->GetMediumFont();
	const float Right = static_cast<float>(Canvas->ClipX) - 30.f * UIScale;
	const float LineHeight = 22.f * UIScale;
	float Y = 30.f * UIScale;
	auto FadeAlpha = [this, Now](double TimeSeconds)
	{
		return FMath::Clamp(static_cast<float>(MessageSeconds - (Now - TimeSeconds)), 0.f, 1.f);
	};

	// What the techniques did, newest first.
	if (const UBendingTechniqueComponent* Techniques = Pawn ? Pawn->FindComponentByClass<UBendingTechniqueComponent>() : nullptr)
	{
		const TArray<FBendingTechniqueMessage>& Messages = Techniques->GetMessages();
		for (int32 Index = Messages.Num() - 1; Index >= 0; --Index)
		{
			const float Alpha = FadeAlpha(Messages[Index].TimeSeconds);
			if (Alpha > 0.f)
			{
				DrawLabel(Messages[Index].Text, Right - GetTextWidth(Messages[Index].Text, Font, 1.f), Y, WithAlpha(Messages[Index].Color, Alpha), Font);
				Y += LineHeight;
			}
		}
	}

	// Reactions between the elements, newest first.
	Y += 0.5f * LineHeight;
	Reactions.RemoveAll([this, Now](const FReactionLine& Line) { return Now - Line.LastTimeSeconds > MessageSeconds; });
	for (int32 Index = Reactions.Num() - 1; Index >= 0; --Index)
	{
		const FReactionLine& Line = Reactions[Index];
		const FString Text = DescribeReaction(Line.Type, Line.MassKg, Line.EnergyJ);
		DrawLabel(Text, Right - GetTextWidth(Text, Font, 1.f), Y, WithAlpha(GetReactionColor(Line.Type), FadeAlpha(Line.LastTimeSeconds)), Font);
		Y += LineHeight;
	}
}

void AAvatarHUD::DrawControls()
{
	const UFont* Medium = GEngine->GetMediumFont();
	const float LineHeight = 22.f * UIScale;
	const float Margin = 30.f * UIScale;
	if (!bShowControls)
	{
		const FString Hint = TEXT("H: controls");
		DrawLabel(Hint, static_cast<float>(Canvas->ClipX) - Margin - GetTextWidth(Hint, Medium, 1.f),
			static_cast<float>(Canvas->ClipY) - Margin - LineHeight, DimTextColor, Medium);
		return;
	}

	static const TCHAR* const Lines[][2] = {
		{ TEXT("WASD"), TEXT("move") },
		{ TEXT("Mouse"), TEXT("look / aim") },
		{ TEXT("Space"), TEXT("jump") },
		{ TEXT("Shift"), TEXT("sprint") },
		{ TEXT("1 2 3 4"), TEXT("water / earth / fire / air stance") },
		{ TEXT("LMB RMB Q E F"), TEXT("the stance's techniques (top left)") },
		{ TEXT("H"), TEXT("hide this panel") },
	};
	const int32 NumLines = static_cast<int32>(UE_ARRAY_COUNT(Lines));
	const float Width = 470.f * UIScale;
	const float Height = (NumLines + 1) * LineHeight + 20.f * UIScale;
	const float X = static_cast<float>(Canvas->ClipX) - Width - Margin;
	float Y = static_cast<float>(Canvas->ClipY) - Height - Margin;

	DrawRect(PanelColor, X, Y, Width, Height);
	Y += 8.f * UIScale;
	DrawLabel(TEXT("CONTROLS"), X + 12.f * UIScale, Y, TextColor, Medium);
	Y += LineHeight + 4.f * UIScale;
	for (int32 Index = 0; Index < NumLines; ++Index)
	{
		DrawLabel(Lines[Index][0], X + 12.f * UIScale, Y, KeyColor, Medium);
		DrawLabel(Lines[Index][1], X + 160.f * UIScale, Y, TextColor, Medium);
		Y += LineHeight;
	}
}
