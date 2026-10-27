#include "EarthHUD.h"

#include "LifeSimulation.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"

namespace
{
	FLinearColor WeightColor(float W)
	{
		const float Strength = FMath::Clamp(FMath::Abs(W) / 3.f, 0.f, 1.f);
		return W >= 0.f ? FLinearColor(0.1f, 0.25f + 0.75f * Strength, 0.1f) * Strength : FLinearColor(0.25f + 0.75f * Strength, 0.1f, 0.1f) * Strength;
	}

	// A neuron: a square whose brightness shows how active it is.
	FLinearColor ActivityColor(float A)
	{
		return A >= 0.f ? FLinearColor(0.3f, 0.6f, 1.f) * (0.25f + 0.75f * FMath::Clamp(A, 0.f, 1.f))
						: FLinearColor(0.6f, 0.3f, 0.9f) * (0.25f + 0.75f * FMath::Clamp(-A, 0.f, 1.f));
	}
}

void AEarthHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	const ALifeSimulation* Sim = nullptr;
	for (TActorIterator<ALifeSimulation> It(GetWorld()); It; ++It)
	{
		Sim = *It;
		break;
	}
	const earth::Npc* P = Sim ? Sim->GetFollowed() : nullptr;
	if (!P || !Sim->IsBrainShown())
	{
		return;
	}

	UFont* Font = GEngine->GetSmallFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.7f, 1.6f);
	const float Width = 560.f * Scale;
	const float Height = 600.f * Scale;
	const float Left = Canvas->ClipX - Width - 20.f * Scale;
	const float Top = 20.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);

	// Header.
	const FString Who = FString::Printf(TEXT("NPC %u, %s, age %d, generation %d, %d children, band of %d"), P->Id,
		P->Female ? TEXT("female") : TEXT("male"), static_cast<int32>(P->AgeYears()), P->Generation, P->Children, P->BandSize);
	DrawText(Who, FLinearColor::White, Left + 10.f * Scale, Top + 8.f * Scale, Font, Scale);
	DrawText(FString::Printf(TEXT("Doing today: %s"), UTF8_TO_TCHAR(earth::ActionName(P->LastAction))), FLinearColor(1.f, 0.95f, 0.3f),
		Left + 10.f * Scale, Top + 26.f * Scale, Font, Scale);

	// Layout of the three layers.
	const float NetTop = Top + 56.f * Scale;
	const float NetHeight = 340.f * Scale;
	const float InX = Left + 120.f * Scale;
	const float HiddenX = Left + 280.f * Scale;
	const float OutX = Left + 420.f * Scale;
	const float Box = 9.f * Scale;
	auto RowY = [&](int Index, int Count) { return NetTop + (Index + 0.5f) * NetHeight / Count; };

	const earth::Genome& G = *P->Genes;
	// Senses to hidden neurons (inborn wiring only; this layer doesn't learn within a life).
	for (int i = 0; i < earth::kBrainInputs; ++i)
	{
		for (int h = 0; h < earth::kBrainHidden; ++h)
		{
			const float W = G.W1[i * earth::kBrainHidden + h];
			if (FMath::Abs(W) > 0.25f)
			{
				DrawLine(InX + Box, RowY(i, earth::kBrainInputs), HiddenX, RowY(h, earth::kBrainHidden), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}
	// Hidden neurons to actions: inborn plus learned.
	for (int h = 0; h < earth::kBrainHidden; ++h)
	{
		for (int a = 0; a < earth::kBrainActions; ++a)
		{
			const float W = earth::People::OutputWeight(*P, h, a);
			if (FMath::Abs(W) > 0.25f)
			{
				DrawLine(HiddenX + Box, RowY(h, earth::kBrainHidden), OutX, RowY(a, earth::kBrainActions), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}

	for (int i = 0; i < earth::kBrainInputs; ++i)
	{
		const float Y = RowY(i, earth::kBrainInputs);
		DrawRect(ActivityColor(P->LastInputs[i]), InX, Y - Box * 0.5f, Box, Box);
		DrawText(UTF8_TO_TCHAR(earth::InputName(i)), FLinearColor(0.8f, 0.8f, 0.8f), Left + 10.f * Scale, Y - 7.f * Scale, Font, Scale * 0.9f);
	}
	for (int h = 0; h < earth::kBrainHidden; ++h)
	{
		const float Y = RowY(h, earth::kBrainHidden);
		DrawRect(ActivityColor(P->LastHidden[h]), HiddenX, Y - Box * 0.5f, Box, Box);
	}
	for (int a = 0; a < earth::kBrainActions; ++a)
	{
		const float Y = RowY(a, earth::kBrainActions);
		const float Chance = P->LastChoice[a];
		const bool bChosen = a == P->LastAction;
		DrawRect(bChosen ? FLinearColor(1.f, 0.9f, 0.2f) : ActivityColor(Chance), OutX, Y - Box * 0.5f, Box, Box);
		DrawRect(FLinearColor(0.3f, 0.6f, 1.f, 0.8f), OutX + Box + 4.f * Scale, Y + 6.f * Scale, 60.f * Scale * Chance, 3.f * Scale);
		DrawText(FString::Printf(TEXT("%s %d%%"), UTF8_TO_TCHAR(earth::ActionName(a)), FMath::RoundToInt(Chance * 100.f)),
			bChosen ? FLinearColor(1.f, 0.95f, 0.3f) : FLinearColor::White, OutX + Box + 4.f * Scale, Y - 9.f * Scale, Font, Scale * 0.9f);
	}

	// Needs as bars, then what it has learned to do over its life.
	float Y = NetTop + NetHeight + 14.f * Scale;
	auto Bar = [&](const TCHAR* Label, float Value, const FLinearColor& Color)
	{
		DrawText(Label, FLinearColor(0.8f, 0.8f, 0.8f), Left + 10.f * Scale, Y - 2.f * Scale, Font, Scale * 0.9f);
		DrawRect(FLinearColor(0.2f, 0.2f, 0.2f), Left + 110.f * Scale, Y + 2.f * Scale, 160.f * Scale, 8.f * Scale);
		DrawRect(Color, Left + 110.f * Scale, Y + 2.f * Scale, 160.f * Scale * FMath::Clamp(Value, 0.f, 1.f), 8.f * Scale);
		Y += 17.f * Scale;
	};
	Bar(TEXT("Food in body"), P->Energy, FLinearColor(0.9f, 0.6f, 0.2f));
	Bar(TEXT("Water"), P->Water, FLinearColor(0.2f, 0.5f, 1.f));
	Bar(TEXT("Tiredness"), P->Fatigue, FLinearColor(0.6f, 0.6f, 0.6f));
	Bar(TEXT("Health"), P->Health, FLinearColor(0.3f, 0.9f, 0.3f));
	Bar(TEXT("Fear"), P->Fear, FLinearColor(0.9f, 0.2f, 0.2f));
	Bar(TEXT("Hunting skill"), P->HuntSkill, FLinearColor(0.8f, 0.4f, 0.2f));
	Bar(TEXT("Foraging skill"), P->ForageSkill, FLinearColor(0.5f, 0.8f, 0.3f));

	// Lifetime habits, from how often it chose each action.
	int Total = 0;
	for (int Count : P->ActionCounts)
	{
		Total += Count;
	}
	const float HabitX = Left + 290.f * Scale;
	float HabitY = NetTop + NetHeight + 12.f * Scale;
	DrawText(TEXT("How it has spent its days:"), FLinearColor::White, HabitX, HabitY, Font, Scale * 0.9f);
	for (int a = 0; a < earth::kBrainActions && Total > 0; ++a)
	{
		HabitY += 16.f * Scale;
		DrawText(FString::Printf(TEXT("%s %d%%"), UTF8_TO_TCHAR(earth::ActionName(a)), FMath::RoundToInt(100.f * P->ActionCounts[a] / Total)),
			FLinearColor(0.8f, 0.8f, 0.8f), HabitX + 10.f * Scale, HabitY, Font, Scale * 0.85f);
	}
	DrawText(FString::Printf(TEXT("Born with: learning rate %.3f, curiosity %.2f, copies others on %.0f%% of days"), G.LearningRate, G.Curiosity, G.Imitation * 100.f),
		FLinearColor(0.8f, 0.8f, 0.8f), Left + 10.f * Scale, Top + Height - 22.f * Scale, Font, Scale * 0.85f);
}
