# Gridlock

A shared-map route-building strategy game. The design doc (Rules, Map and UE Project tabs) is the source of truth for rules and numbers.

- Engine: UE 5.8, C++ module `Gridlock`
- Layers: `Source/Gridlock/Model` holds the rules as plain data, `Game` owns the live match, `AI` picks moves, and `View` only draws.

## Play it now

Open `Gridlock.uproject` and press **Play**. The editor starts on the engine's empty Entry map, and `GridlockGameMode` adds the board, lights, camera and a built-in scenario, so no assets are needed. You play Blue against the AI as Orange (Builder, Normal), which moves once a second.

A how-to-play screen opens first. After that:

- The banner at the top says what to do right now.
- Anything you can click glows gold, and your Route cards' cities have gold rings.
- Hover over a route to see what a click would do and what it costs, or why you can't.
- Buttons at the bottom right do everything; steps with nothing to do are skipped for you.
- "What happened" lists every move, and each move flashes on the board. The AI's next target has an orange ring.

| Shortcut | Does |
| --- | --- |
| Left-click | A button, or a route/slot: a route opens the build window (Start or Build), buy out (Resolve), place a Lot or upgrade (Buy/Upgrade) |
| Enter / Esc | Build window: build / close |
| Right-click a property | Demolish (Resolve) |
| Space | Next step / Skip / End turn |
| 1-5 / T | Ticket to Ride: take that face-up ticket / take one from the deck (2 a turn; a face-up Wild is both) |
| T / Y / P | (v0.4) Buy 1 ticket / 2 tickets / a property card |
| S | Sell mode: click one of your properties to sell it for half of what it cost (1 a turn) |
| D | Draw 3 Route cards, keep at least 1 (that is your turn) |
| 1-5, Enter | Toggle which drawn cards to keep, then keep them |
| Z | Undo your last move (and the AI's moves after it) |
| F1 | How-to-play screen |
| H | Hot-seat on/off (play both sides) |

Dev only: `-GridlockDemoSteps=N` plays N random moves at startup; `-GridlockDemoPhase=Start|Build|BuyUpgrade|Resolve|Keep|AITurn` plays random moves until that moment; `-GridlockNoHelp` skips the help screen; `-GridlockShot=<path>` saves a screenshot once shaders finish compiling, then quits.

## Real assets

1. Right-click `Gridlock.uproject` and choose **Generate Visual Studio project files**.
2. In `Content/Gridlock/Data`, create these Data Assets:
   - `DA_Map_v01` (Map Definition), then click **Fill With Map V01** and **Validate Map**
   - `DA_RouteCards_v01` (Route Card Deck), then click **Fill With Deck V01**
   - `DA_Tuning_Default` (Tuning Data); the defaults already match Rules v0.1
   - `Data/AI/DA_AI_Builder` (AI Personality)
3. In `Data/Scenarios`, create `DA_Scenario_01` (Scenario Definition) and point it at the four assets above.
4. Create `Maps/L_Board` (an Empty Level), set its GameMode override to a `BP_GameMode` child of `GridlockGameMode`, and set `DefaultScenario` to `DA_Scenario_01`. Then point `GameDefaultMap` and `EditorStartupMap` in `Config/DefaultEngine.ini` at it instead of `/Engine/Maps/Entry`.
5. For real art, make `BP_City`, `BP_Route` and `BP_Property` children of the C++ actors, override `OnBoardStateChanged` / `OnSlotChanged`, and set them as `CityClass`, `RouteClass` and `PropertyClass` on `BP_GameMode`.

## Tests

In the editor: **Tools > Session Frontend > Automation** and run `Gridlock`. Or headless:

```
UnrealEditor-Cmd.exe Gridlock.uproject -ExecCmds="Automation RunTests Gridlock; Quit" -unattended -nullrhi
```

- `Gridlock.Data.*` checks the map and deck against the Map tab.
- `Gridlock.Rules.*` checks each rule from the Rules tab, plus 30 random full games that check cash, hand size and card counts after every move.
- `Gridlock.AI.*` checks the AI: it beats random play, makes only legal moves, decides fast, and never reads your hand. `Gridlock.AI.Matchups`, `Gridlock.AI.BalanceReport` and `Gridlock.AI.PropertyReport` log win rates, game length, how games end, and what happens to properties, for balancing.

## Rule options

The game plays **Rules v0.5: plain Ticket to Ride on our map** (8 ticket colors + Wild, 5 face-up tickets, 45 trains each; each turn take 2 tickets, claim a route, or draw 3 Route cards; routes score 1/2/4/7/10/15/18/21/27 by length; Route cards score or cost their points at the end; +10 for the longest route; the game ends a round after someone is down to 2 trains). On top of that: **v0.6** tickets cost $1 (a face-up Wild $2), with unlimited money for now (the panel shows what each player has spent); **v0.7** adds 12 Lot cards to the ticket deck (face-up $4 and your whole turn; from the deck $1, then keep for $3 more or discard), placed free on routes (1-3 lot slots by length): own all of a route's lots and only you can build it. **v0.8** adds city properties: click a city your routes reach to buy it ($10, your turn); the other player pays you $3 each time they build into it, and again at the end for each finished Route card whose path visits it. The numbers are in `ApplyRulesV05`..`V08` in `Source/Gridlock/Data/TuningData.cpp`; the game plays V08. Money and properties are switched off for now and come back one step at a time.

The earlier **Rules v0.4** (the field defaults in `TuningData.h`): the card economy (colored tickets and property cards you buy blind and pay again to play) with one main action a turn: buy 2 tickets or 1 property card, or build a route, or remove a property. Players start with $35 and earn only from tolls, sales and finished Route cards ($1 a point; no income); routes can be built anywhere, paying with the ticket color and Wilds you choose in the build window; the first to 5 Route cards wins, or the most points after 40 rounds (see the Rules v0.3 draft tab, "Rules v0.4" section). The tuning asset's **Presets** buttons (Apply Rules V01/V02/V03/V04) switch every number to one version (V05 included), and switchable options sit under **Cards > Options**, **Route Cards > Options** and **Removal > Options**. The reports above compare them in AI-vs-AI games.

## What's stubbed

Search for `TODO(milestone N)`; N matches the build order on the UE Project tab. Milestones 1-4 are done: rules, board, properties and removal, and the AI (Builder, Saboteur and Tycoon personalities; Easy, Normal and Hard). Next is the UMG HUD that replaces the canvas one in `GridlockHUD` (milestone 5).

## Source control

Git with LFS: `.gitattributes` sends `.uasset`, `.umap` and source art through LFS.
