# Frame Window Counter

Counts the **frame window** of every input in a macro, and shows it as **FP** (frame perfect rate).

- A window of **1 tick** at 240 TPS is **240 FP**
- A window of **2 ticks** is **120 FP**
- A window of **4 ticks** is **60 FP**

`FP = TPS / window in ticks`

## How to use
1. Record your clear with **xdBot** or **Eclipse** (240 TPS, no physics bypass, no frame fixes).
2. Open the level and pause. Press the **FP** button in the bottom right corner.
3. Pick your macro from the list, then press **Count**.
4. Wait for the count to finish. The results screen shows every input, the tightest one and how many inputs there are of each FP.
5. **Export** saves a text/CSV file in the mod's save folder (`results`).

Macros are read from the xdBot and Eclipse save folders, plus the mod's own `macros` folder (drop `.gdr`, `.gdr2` or `.json` files there).

## How it counts
The level is restarted and the macro is played back with one input moved earlier or later by a few ticks. If you survive the next *Survive Ticks* (default 240), that shift is inside the window. The window is how many ticks in a row pass.

## Limits
- 240 TPS macros only, classic mode only (no platformer)
- Not practice mode
- Spam sections and orb buffering can show windows that are tighter than they feel
- Macros with frame fixes or physics bypass may not play back exactly
- Counting a long level takes a while, the screen shows an ETA

## Credits
Idea inspired by Antic's Counter by anticroom. This mod is written from scratch.
Macro format: GDReplayFormat by maxnut.
