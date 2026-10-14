#include "Data/AIPersonality.h"

void UAIPersonality::ApplyPreset(EAIStyle Style, EAIDifficulty Difficulty)
{
	Modify();

	switch (Style)
	{
	case EAIStyle::Builder:
		ProgressWeight = 1.0f;
		ThreatWeight = 0.3f;
		EconomyWeight = 0.4f;
		ExposureWeight = 0.2f;
		PointsWeight = 1.0f;
		break;
	case EAIStyle::Saboteur:
		ProgressWeight = 0.7f;
		ThreatWeight = 1.0f;
		EconomyWeight = 0.3f;
		ExposureWeight = 0.2f;
		PointsWeight = 1.0f;
		break;
	case EAIStyle::Tycoon:
		ProgressWeight = 0.6f;
		ThreatWeight = 0.3f;
		EconomyWeight = 1.0f;
		ExposureWeight = 0.3f;
		PointsWeight = 1.2f;
		break;
	}

	TopN = Difficulty == EAIDifficulty::Easy ? 3 : 1;
	ScoreNoise = Difficulty == EAIDifficulty::Easy ? 1.5f : 0.0f;
	bLookahead = Difficulty == EAIDifficulty::Hard;
	MarkPackageDirty();
}
