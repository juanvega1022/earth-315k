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

	// A distinct colour for each line of the brain waves.
	FLinearColor WaveColor(int32 Index, int32 Count)
	{
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(255 * Index / FMath::Max(1, Count)), 170, 255);
	}

	const FLinearColor kGrey(0.8f, 0.8f, 0.8f);
	const FLinearColor kHighlight(1.f, 0.95f, 0.3f);
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
	if (!Sim || !Sim->GetLife())
	{
		return;
	}

	Font = GEngine->GetSmallFont();
	Scale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.7f, 1.6f);

	FLifeReadScope Scope(Sim);
	DrawJumpBar(*Sim);
	if (Sim->IsBrainShown())
	{
		if (const earth::Npc* P = Sim->GetFollowed())
		{
			DrawNpcPanel(*P, *Sim);
		}
		else if (const earth::AnimalGroup* G = Sim->GetFollowedGroup())
		{
			DrawAnimalPanel(*G, *Sim);
		}
	}
	if (Sim->ArePlantsShown())
	{
		DrawPlantPanel(Sim->GetPlantPatch(), *Sim);
	}
}

void AEarthHUD::DrawBar(const TCHAR* Label, float Value, const FLinearColor& Color, float Left, float& Y, float LabelWidth)
{
	DrawText(Label, kGrey, Left, Y - 2.f * Scale, Font, Scale * 0.9f);
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f), Left + LabelWidth * Scale, Y + 2.f * Scale, 160.f * Scale, 8.f * Scale);
	DrawRect(Color, Left + LabelWidth * Scale, Y + 2.f * Scale, 160.f * Scale * FMath::Clamp(Value, 0.f, 1.f), 8.f * Scale);
	Y += 17.f * Scale;
}

void AEarthHUD::DrawWaves(const TArray<TArray<float>>& Waves, float Left, float Top, float Width, float Height)
{
	DrawRect(FLinearColor(0.05f, 0.05f, 0.08f, 0.9f), Left, Top, Width, Height);
	DrawLine(Left, Top + Height * 0.5f, Left + Width, Top + Height * 0.5f, FLinearColor(0.25f, 0.25f, 0.25f), 1.f);
	DrawText(TEXT("Brain waves: each line is one hidden neuron, newest on the right"), kGrey, Left + 4.f * Scale, Top + 2.f * Scale, Font, Scale * 0.8f);
	if (Waves.Num() < 2)
	{
		return;
	}
	const int32 Lines = Waves.Last().Num();
	const float Dx = Width / FMath::Max(1, Waves.Num() - 1);
	auto Yof = [&](float V) { return Top + Height * 0.5f - FMath::Clamp(V, -1.f, 1.f) * Height * 0.42f; };
	for (int32 Line = 0; Line < Lines; ++Line)
	{
		const FLinearColor Color = WaveColor(Line, Lines);
		for (int32 i = 1; i < Waves.Num(); ++i)
		{
			if (Waves[i - 1].IsValidIndex(Line) && Waves[i].IsValidIndex(Line))
			{
				DrawLine(Left + (i - 1) * Dx, Yof(Waves[i - 1][Line]), Left + i * Dx, Yof(Waves[i][Line]), Color, 1.f);
			}
		}
	}
}

void AEarthHUD::DrawJumpBar(const ALifeSimulation& Sim)
{
	const float Progress = Sim.GetJumpProgress();
	if (Progress < 0.f)
	{
		return;
	}
	const float Width = 700.f * Scale;
	const float Left = (Canvas->ClipX - Width) * 0.5f;
	const float Top = Canvas->ClipY - 90.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), Left - 10.f * Scale, Top - 30.f * Scale, Width + 20.f * Scale, 60.f * Scale);
	DrawText(Sim.GetJumpText(), FLinearColor::White, Left, Top - 24.f * Scale, Font, Scale);
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f), Left, Top, Width, 14.f * Scale);
	DrawRect(FLinearColor(0.3f, 0.7f, 1.f), Left, Top, Width * Progress, 14.f * Scale);
}

void AEarthHUD::DrawNpcPanel(const earth::Npc& P, const ALifeSimulation& Sim)
{
	const float Width = 560.f * Scale;
	const float Height = 720.f * Scale;
	const float Left = Canvas->ClipX - Width - 20.f * Scale;
	const float Top = 20.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);

	// Header.
	const FString Who = FString::Printf(TEXT("NPC %u, %s, age %d, generation %d, %d children, band of %d"), P.Id,
		P.Female ? TEXT("female") : TEXT("male"), static_cast<int32>(P.AgeYears()), P.Generation, P.Children, P.BandSize);
	DrawText(Who, FLinearColor::White, Left + 10.f * Scale, Top + 8.f * Scale, Font, Scale);
	DrawText(FString::Printf(TEXT("Doing today: %s"), UTF8_TO_TCHAR(earth::ActionName(P.LastAction))), kHighlight,
		Left + 10.f * Scale, Top + 26.f * Scale, Font, Scale);

	// Layout of the three layers.
	const float NetTop = Top + 56.f * Scale;
	const float NetHeight = 340.f * Scale;
	const float InX = Left + 120.f * Scale;
	const float HiddenX = Left + 280.f * Scale;
	const float OutX = Left + 420.f * Scale;
	const float Box = 9.f * Scale;
	auto RowY = [&](int Index, int Count) { return NetTop + (Index + 0.5f) * NetHeight / Count; };

	const earth::Genome& G = *P.Genes;
	// Brains can grow or shrink from one generation to the next.
	const int Hidden = P.BrainSize();
	// Senses to hidden neurons (inborn wiring only; this layer doesn't learn within a life).
	for (int i = 0; i < earth::kBrainInputs; ++i)
	{
		for (int h = 0; h < Hidden; ++h)
		{
			const float W = G.W1[h * earth::kBrainInputs + i];
			if (FMath::Abs(W) > 0.25f)
			{
				DrawLine(InX + Box, RowY(i, earth::kBrainInputs), HiddenX, RowY(h, Hidden), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}
	// Hidden neurons to actions: inborn plus learned.
	for (int h = 0; h < Hidden; ++h)
	{
		for (int a = 0; a < earth::kBrainActions; ++a)
		{
			const float W = earth::People::OutputWeight(P, h, a);
			if (FMath::Abs(W) > 0.25f)
			{
				DrawLine(HiddenX + Box, RowY(h, Hidden), OutX, RowY(a, earth::kBrainActions), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}

	for (int i = 0; i < earth::kBrainInputs; ++i)
	{
		const float Y = RowY(i, earth::kBrainInputs);
		DrawRect(ActivityColor(P.LastInputs[i]), InX, Y - Box * 0.5f, Box, Box);
		DrawText(UTF8_TO_TCHAR(earth::InputName(i)), kGrey, Left + 10.f * Scale, Y - 7.f * Scale, Font, Scale * 0.9f);
	}
	const TArray<TArray<float>>& Waves = Sim.GetBrainWaves();
	for (int h = 0; h < Hidden; ++h)
	{
		const float Y = RowY(h, Hidden);
		DrawRect(ActivityColor(P.LastHidden[h]), HiddenX, Y - Box * 0.5f, Box, Box);
		// A dot in the neuron's brain-wave colour, to match the lines below.
		DrawRect(WaveColor(h, Hidden), HiddenX - 6.f * Scale, Y - 2.f * Scale, 4.f * Scale, 4.f * Scale);
	}
	DrawText(FString::Printf(TEXT("%d neurons"), Hidden), kGrey, HiddenX - 20.f * Scale, NetTop - 14.f * Scale, Font, Scale * 0.85f);
	for (int a = 0; a < earth::kBrainActions; ++a)
	{
		const float Y = RowY(a, earth::kBrainActions);
		const float Chance = P.LastChoice[a];
		const bool bChosen = a == P.LastAction;
		DrawRect(bChosen ? FLinearColor(1.f, 0.9f, 0.2f) : ActivityColor(Chance), OutX, Y - Box * 0.5f, Box, Box);
		DrawRect(FLinearColor(0.3f, 0.6f, 1.f, 0.8f), OutX + Box + 4.f * Scale, Y + 6.f * Scale, 60.f * Scale * Chance, 3.f * Scale);
		DrawText(FString::Printf(TEXT("%s %d%%"), UTF8_TO_TCHAR(earth::ActionName(a)), FMath::RoundToInt(Chance * 100.f)),
			bChosen ? kHighlight : FLinearColor::White, OutX + Box + 4.f * Scale, Y - 9.f * Scale, Font, Scale * 0.9f);
	}

	// Needs as bars, then what it has learned to do over its life.
	float Y = NetTop + NetHeight + 14.f * Scale;
	const float BarLeft = Left + 10.f * Scale;
	DrawBar(TEXT("Food in body"), P.Energy, FLinearColor(0.9f, 0.6f, 0.2f), BarLeft, Y);
	DrawBar(TEXT("Water"), P.Water, FLinearColor(0.2f, 0.5f, 1.f), BarLeft, Y);
	DrawBar(TEXT("Tiredness"), P.Fatigue, FLinearColor(0.6f, 0.6f, 0.6f), BarLeft, Y);
	DrawBar(TEXT("Health"), P.Health, FLinearColor(0.3f, 0.9f, 0.3f), BarLeft, Y);
	DrawBar(TEXT("Fear"), P.Fear, FLinearColor(0.9f, 0.2f, 0.2f), BarLeft, Y);
	DrawBar(TEXT("Hunting skill"), P.HuntSkill, FLinearColor(0.8f, 0.4f, 0.2f), BarLeft, Y);
	DrawBar(TEXT("Foraging skill"), P.ForageSkill, FLinearColor(0.5f, 0.8f, 0.3f), BarLeft, Y);

	// Lifetime habits, from how often it chose each action.
	int Total = 0;
	for (int Count : P.ActionCounts)
	{
		Total += Count;
	}
	const float HabitX = Left + 290.f * Scale;
	float HabitY = NetTop + NetHeight + 12.f * Scale;
	DrawText(TEXT("How it has spent its days:"), FLinearColor::White, HabitX, HabitY, Font, Scale * 0.9f);
	for (int a = 0; a < earth::kBrainActions && Total > 0; ++a)
	{
		HabitY += 16.f * Scale;
		DrawText(FString::Printf(TEXT("%s %d%%"), UTF8_TO_TCHAR(earth::ActionName(a)), FMath::RoundToInt(100.f * P.ActionCounts[a] / Total)),
			kGrey, HabitX + 10.f * Scale, HabitY, Font, Scale * 0.85f);
	}
	DrawText(FString::Printf(TEXT("Born with: learning rate %.3f, curiosity %.2f, copies others on %.0f%% of days"), G.LearningRate, G.Curiosity, G.Imitation * 100.f),
		kGrey, Left + 10.f * Scale, Y + 4.f * Scale, Font, Scale * 0.85f);
	FString Skills;
	for (size_t k = 0; k < earth::AllSkills().size(); ++k)
	{
		if (earth::Knows(P.Skills, static_cast<int>(k)))
		{
			Skills += (Skills.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(earth::AllSkills()[k].Name));
		}
	}
	DrawText(TEXT("Knows: ") + (Skills.IsEmpty() ? FString(TEXT("nothing yet")) : Skills), kGrey, Left + 10.f * Scale, Y + 20.f * Scale, Font, Scale * 0.85f);

	DrawWaves(Waves, Left + 10.f * Scale, Top + Height - 130.f * Scale, Width - 20.f * Scale, 120.f * Scale);
}

void AEarthHUD::DrawAnimalPanel(const earth::AnimalGroup& G, const ALifeSimulation& Sim)
{
	const earth::Species& S = Sim.GetLife()->GetSpecies()[G.SpeciesIndex];
	const float Width = 560.f * Scale;
	const float Height = 600.f * Scale;
	const float Left = Canvas->ClipX - Width - 20.f * Scale;
	const float Top = 20.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);

	DrawText(FString::Printf(TEXT("%s group %u: %s animals, generation %d, eats %s"), UTF8_TO_TCHAR(S.Name.c_str()), G.Id,
				 *FText::AsNumber(FMath::RoundToInt64(G.Count)).ToString(), G.Generation, S.Eats == earth::Diet::Plants ? TEXT("plants") : TEXT("meat")),
		FLinearColor::White, Left + 10.f * Scale, Top + 8.f * Scale, Font, Scale);
	DrawText(FString::Printf(TEXT("Doing now: %s"), UTF8_TO_TCHAR(earth::AnimalMoveName(G.LastMove))), kHighlight, Left + 10.f * Scale,
		Top + 26.f * Scale, Font, Scale);

	const float NetTop = Top + 56.f * Scale;
	const float NetHeight = 200.f * Scale;
	const float InX = Left + 130.f * Scale;
	const float HiddenX = Left + 280.f * Scale;
	const float OutX = Left + 400.f * Scale;
	const float Box = 10.f * Scale;
	auto RowY = [&](int Index, int Count) { return NetTop + (Index + 0.5f) * NetHeight / Count; };

	const earth::AnimalBrain& B = G.Brain;
	for (int i = 0; i < earth::kAnimalInputs; ++i)
	{
		for (int h = 0; h < earth::kAnimalHidden; ++h)
		{
			const float W = B.W1[i * earth::kAnimalHidden + h];
			if (FMath::Abs(W) > 0.1f)
			{
				DrawLine(InX + Box, RowY(i, earth::kAnimalInputs), HiddenX, RowY(h, earth::kAnimalHidden), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}
	for (int h = 0; h < earth::kAnimalHidden; ++h)
	{
		for (int o = 0; o < earth::kAnimalOutputs; ++o)
		{
			// These weights are small, so they are drawn ten times stronger.
			const float W = B.W2[h * earth::kAnimalOutputs + o] * 10.f;
			if (FMath::Abs(W) > 0.25f)
			{
				DrawLine(HiddenX + Box, RowY(h, earth::kAnimalHidden), OutX, RowY(o, earth::kAnimalOutputs), WeightColor(W), 1.f + FMath::Min(FMath::Abs(W), 3.f) * 0.5f);
			}
		}
	}
	for (int i = 0; i < earth::kAnimalInputs; ++i)
	{
		const float Y = RowY(i, earth::kAnimalInputs);
		DrawRect(ActivityColor(G.LastInputs[i]), InX, Y - Box * 0.5f, Box, Box);
		DrawText(FString::Printf(TEXT("%s %.2f"), UTF8_TO_TCHAR(earth::AnimalInputName(i)), G.LastInputs[i]), kGrey, Left + 10.f * Scale, Y - 7.f * Scale, Font, Scale * 0.9f);
	}
	for (int h = 0; h < earth::kAnimalHidden; ++h)
	{
		const float Y = RowY(h, earth::kAnimalHidden);
		DrawRect(ActivityColor(G.LastHidden[h]), HiddenX, Y - Box * 0.5f, Box, Box);
		DrawRect(WaveColor(h, earth::kAnimalHidden), HiddenX - 6.f * Scale, Y - 2.f * Scale, 4.f * Scale, 4.f * Scale);
	}
	for (int o = 0; o < earth::kAnimalOutputs; ++o)
	{
		const float Y = RowY(o, earth::kAnimalOutputs);
		const float V = G.LastOutputs[o];
		DrawRect(ActivityColor(FMath::Min(V, 1.f)), OutX, Y - Box * 0.5f, Box, Box);
		DrawRect(FLinearColor(0.3f, 0.6f, 1.f, 0.8f), OutX + Box + 4.f * Scale, Y + 6.f * Scale, 60.f * Scale * FMath::Clamp(V / 1.5f, 0.f, 1.f), 3.f * Scale);
		DrawText(FString::Printf(TEXT("%s %.2f"), UTF8_TO_TCHAR(earth::AnimalOutputName(o)), V), FLinearColor::White, OutX + Box + 4.f * Scale, Y - 9.f * Scale, Font,
			Scale * 0.9f);
	}

	float Y = NetTop + NetHeight + 14.f * Scale;
	const float BarLeft = Left + 10.f * Scale;
	DrawBar(TEXT("Well fed"), static_cast<float>(G.WellFed), FLinearColor(0.9f, 0.6f, 0.2f), BarLeft, Y);
	DrawBar(TEXT("Group size"), static_cast<float>(G.Count / (S.GroupSize * 4.0)), FLinearColor(0.6f, 0.6f, 0.9f), BarLeft, Y);
	DrawText(FString::Printf(TEXT("Body %s kg, moves up to %.0f km a day, lives about %.0f years"), *FText::AsNumber(FMath::RoundToInt(S.BodyMassKg)).ToString(),
				 S.SpeedKmPerDay, S.LifespanYears),
		kGrey, BarLeft, Y, Font, Scale * 0.85f);
	Y += 18.f * Scale;
	DrawText(TEXT("Each group's brain is passed to the groups that split from it, with small changes."), kGrey, BarLeft, Y, Font, Scale * 0.85f);
	Y += 16.f * Scale;
	DrawText(TEXT("H: next species   G: stop following   B: hide this"), kGrey, BarLeft, Y, Font, Scale * 0.85f);

	DrawWaves(Sim.GetBrainWaves(), Left + 10.f * Scale, Top + Height - 190.f * Scale, Width - 20.f * Scale, 180.f * Scale);
}

void AEarthHUD::DrawPlantPanel(const earth::PlantPatch& Patch, const ALifeSimulation& Sim)
{
	const float Width = 420.f * Scale;
	const float Height = 250.f * Scale;
	const float Left = 20.f * Scale;
	const float Top = Canvas->ClipY - Height - 120.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);

	const float Peak = static_cast<float>(FMath::Max(1.0, Patch.Peak));
	DrawText(FString::Printf(TEXT("Plants around %.1f%s %.1f%s (a %.0f-degree square)"), FMath::Abs(Patch.Lat), Patch.Lat >= 0.0 ? TEXT("N") : TEXT("S"),
				 FMath::Abs(Patch.Lon), Patch.Lon >= 0.0 ? TEXT("E") : TEXT("W"), Patch.SizeDegrees),
		FLinearColor::White, Left + 10.f * Scale, Top + 8.f * Scale, Font, Scale);
	if (Patch.Capacity <= 0.0)
	{
		DrawText(TEXT("Nothing grows here (sea, ice, high mountains or desert)."), kGrey, Left + 10.f * Scale, Top + 30.f * Scale, Font, Scale * 0.9f);
		return;
	}
	float Y = Top + 32.f * Scale;
	const float BarLeft = Left + 10.f * Scale;
	DrawBar(TEXT("Plant food now"), static_cast<float>(Patch.Plants) / Peak, FLinearColor(0.2f, 0.7f, 0.2f), BarLeft, Y, 130.f);
	DrawBar(TEXT("Most it can hold"), static_cast<float>(Patch.Capacity) / Peak, FLinearColor(0.1f, 0.4f, 0.1f), BarLeft, Y, 130.f);
	DrawBar(TEXT("Growing season"), static_cast<float>(Patch.Season), FLinearColor(0.9f, 0.8f, 0.2f), BarLeft, Y, 130.f);
	DrawBar(TEXT("Eaten per day"), static_cast<float>(Patch.EatenPerDay * 30.0) / Peak, FLinearColor(0.9f, 0.4f, 0.2f), BarLeft, Y, 130.f);
	DrawText(FString::Printf(TEXT("%s kg of plant food per km2 now, %s eaten per day"), *FText::AsNumber(FMath::RoundToInt(Patch.Plants)).ToString(),
				 *FText::AsNumber(FMath::RoundToInt(Patch.EatenPerDay)).ToString()),
		kGrey, BarLeft, Y, Font, Scale * 0.85f);

	// Plant food over the last days.
	const TArray<float>& History = Sim.GetPlantHistory();
	const float GLeft = Left + 10.f * Scale;
	const float GTop = Top + Height - 100.f * Scale;
	const float GWidth = Width - 20.f * Scale;
	const float GHeight = 90.f * Scale;
	DrawRect(FLinearColor(0.05f, 0.05f, 0.08f, 0.9f), GLeft, GTop, GWidth, GHeight);
	DrawText(TEXT("Plant food over recent days"), kGrey, GLeft + 4.f * Scale, GTop + 2.f * Scale, Font, Scale * 0.8f);
	const float Dx = GWidth / FMath::Max(1, History.Num() - 1);
	for (int32 i = 1; i < History.Num(); ++i)
	{
		DrawLine(GLeft + (i - 1) * Dx, GTop + GHeight * (1.f - FMath::Clamp(History[i - 1], 0.f, 1.f)), GLeft + i * Dx,
			GTop + GHeight * (1.f - FMath::Clamp(History[i], 0.f, 1.f)), FLinearColor(0.3f, 0.9f, 0.3f), 1.5f);
	}
}
