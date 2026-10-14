#pragma once

#include "CoreMinimal.h"
#include "Model/GridlockTypes.h"

class UAIPersonality;
struct FRulesContext;

/** Score terms for one position, from one player's point of view (before weighting). */
struct FAIScoreTerms
{
	float Progress = 0.0f;
	float Threat = 0.0f;
	float Economy = 0.0f;
	float Exposure = 0.0f;
	float Points = 0.0f;
};

/**
 * Utility scoring. A position is worth the weighted sum of five terms; an action is worth the
 * position it leads to. Nothing hidden about the opponent is ever read: not their Route cards
 * (their goals are estimated from every card the scoring player hasn't seen), not the colors of
 * their tickets, and not the tiers of their property cards. Only counts are public.
 */
struct GRIDLOCK_API FAIScoring
{
	/** The position's value for Player: higher is better. A finished game is +/-10000. */
	static float Evaluate(const FBoardState& State, const FRulesContext& Context, const UAIPersonality& Personality, int32 Player);

	/** The five terms behind Evaluate, unweighted (for logs and tests). */
	static FAIScoreTerms ComputeTerms(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** The value of the position after Action. */
	static float ScoreAction(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context,
		const UAIPersonality& Personality, int32 Player);

	/**
	 * Cash-like cost still needed to complete the card: claiming the missing routes, plus reaching
	 * them from the player's network. With bKnowsTickets (the player's own view), routes it lacks
	 * tickets for cost more. INDEX_NONE when it can't be done right now.
	 */
	static int32 CardRemainingCost(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId, bool bKnowsTickets = true);

	/** A card's worth to hold: its points, discounted by how far it is from done. */
	static float CardValue(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId, bool bKnowsTickets = true);

	/** The unclaimed routes the player still needs for its most valuable unfinished card, network end first. */
	static TArray<FName> NeededRoutes(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** The same for one card: the cheapest unclaimed routes that would join its cities to the network. */
	static TArray<FName> NeededRoutesForCard(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId);

	/** The next route to claim toward the player's most valuable unfinished card, or NAME_None. */
	static FName FindIntent(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** Tickets the player is short of for its NeededRoutes, after spending what it holds (Wilds included). */
	static int32 TicketShortfall(const FBoardState& State, const FRulesContext& Context, int32 Player);
};
