# Frame Window Counter (`vinz.frame-counter`)

Geode mod for Geometry Dash 2.2081 (Android + Windows). Loads an xdBot / Eclipse macro and counts the
frame window of every input, shown as ticks, **FP** (e.g. 120 FP) and ms.

`FP = TPS / window ticks` -> at 240 TPS: 1 tick = 240 FP, 2 = 120 FP, 3 = 80 FP, 4 = 60 FP.

See `about.md` for usage. Build: push to GitHub, the Actions workflow produces the `.geode`.

```
git init && git add -A && git commit -m "Frame Window Counter"
gh repo create frame-counter --public --source=. --push
```
Then open the Actions tab -> "Build Binaries" -> download the artifact `Build Output`.
