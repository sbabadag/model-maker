# Adapting a Tekla component dialog (tabs, greyed fields, figures)

Use when the user asks to make a model-maker component dialog look like the Tekla one
(`support.tekla.com/doc/tekla-structures/<year>/conn_<N>_help`).

## Procedure

1. `web_extract` the help page. It is truncated in the middle: read the saved full-text file
   (path in the footer) with `read_file` offset/limit until every tab section is covered. Tab
   order on the page IS the dialog tab order (Picture, Plates, Stiffeners, Haunch, Notch, Bolts, ...).
2. Diff against the engine: list the component's `ComponentType::params` and the dialog groups.
   Produce a gap table (Tekla tab -> missing fields) for the user before coding.
3. Regroup by the Tekla page, not by the old grouping (e.g. beam gap / end cut belong to Picture,
   tab position / gap to Plates, bolt edge / count to Bolts).
4. Fields with no engine geometry: `CompParamSpec.documentedOnly=true` (grey, tooltip, not written
   to `resultParams()`, not in `ComponentType::params`). Never enable a field the generator ignores.
   Do not widen an engine enum (e.g. add Bevel/Partial) without writing the geometry.
5. Download the page's per-tab dimension figures (see below) and attach them to the tab groups.
6. Update tests together: tab titles/order, `!documentedOnly` spec count, grey fields absent from
   `resultParams()`, grey names absent from engine params, every non-grey name present in engine
   params, figure labels loaded (`findChildren<QLabel*>` with non-null pixmap).
7. Build + full `ctest` (Qt6 is installed on the Linux host; widget test runs with
   `QT_QPA_PLATFORM=offscreen`), commit, push, then give the Windows commands one per line.

## 1:1 copy from the user's Tekla screenshots ("canvas" tabs)

When the user wants the dialog to BE the Tekla dialog ("tab'lari direk kopyala"), do not lay
fields out in forms. Use `ComponentDialogGroup.layout = L"canvas"`: background = the tab panel
crop of the user's screenshot, every control placed on its white box (`DialogPlacedControl`).

- Pipeline (146 reference): `tools/tekla146_canvas/` -> per-tab `maps/map_<Tab>.json` ->
  `gen.py <screenshot_dir>` writes `resources/tekla146/tabs/*.png` + generated
  `src/component_dialog_146_canvas.cpp` (`applyShearPlate146Canvas`). Never hand-edit the .cpp.
- Box detection without vision: Win11 edit/combo = white fill + light frame + DARK UNDERLINE
  (0x83) one px below; a plain "dark frame on >=2 sides" test misses them. Panel crop = between
  the 2nd and 3rd long horizontal frame lines below y=90.
- Box -> parameter mapping needs vision. If `vision_analyze` errors in the parent, delegate with
  `images=` (children run a different model whose vision works): give 2x-upscaled screenshots
  with red numbered boxes + boxes.json + the existing param list; demand JSON per tab.
- QA the same way: render each tab offscreen (`MM_DIALOG_SNAPSHOT_DIR` in the widget test),
  compose original|ours side by side, have a vision child list misalignments/wrong control types.
- Icon dropdowns: transparent QComboBox over the Tekla picture (badge shows a changed choice);
  numeric field drawn as a Tekla combo (Bolt size) = editable QComboBox, and `valueFromEditor`
  must accept a QComboBox for Length.
- A Tekla left-of-field checkbox governs the controls to its right on the same row (until the next
  one) -> register in `optionalChecks_`.

## Making a greyed tab functional (146 Bolts pattern)

- Promote a field = add it to `shearPlate146ComponentType().params` AND delete its `docEnum/docLen`
  line in `component_dialog.cpp` (else duplicate spec). Canvas controls bound to a name with no spec
  are silently inert (e.g. `boltHorizontalPos` was on screen but ignored). Invalid enum values are
  reset to the spec default by `alignParamsToSpecs`, so a legacy enum name never reaches the generator.
- Per-part hole shape: generator cannot know owned plate ids -> `BoltGroup.holeShapeTarget`
  (1 plates / 2 user parts / 3 all) + `resolveHoleShapeParts` called at BOTH materializer sites
  (commit and recompute). Slot cut = wire (2 lines + 2 arcs) -> face -> prism, never a Fuse.
- Fields the geometry cannot honour (Sloped orientation, Main-part vertical ref) -> `plan.error`
  (fail-closed), never a silent fallback. Unknown icon meaning (bolt-assembly checkboxes) -> keep grey.
- Verify by ANALYTIC VOLUME (plate - N*hole area*t), not modifier counts.
- OCC tests on Linux: write a build script that compiles the shared source list to objects
  (xargs -P2, incremental) once, then links each test; ~5 min first time, seconds after.
  A heredoc that writes many files can be blocked by approval; use write_file + a small script.
- Vision: `vision_analyze` and vision children may both fail; PIL ASCII dumps of the screenshot crop
  show checkbox ticks (filled `#` interior) well enough to read default states.

## Stiffeners tab pattern (146)

- Section frame convention: `profileSectionFrame(axis, refNormal)` -> width W = refNormal x axis =
  flange-width / WEB-NORMAL direction, height H = axis x W = web direction. A beam along +-H hits
  the flange face (Case A); along +-W hits the web side (Case B). Classify with |lenDir.W| vs |lenDir.H|.
- Tekla 'opposite web stiffener' = plate on the web side OPPOSITE the shear tab; help-figure gap 1 =
  vertical clearance to flanges, gap 2 = clearance to the web (view along the main part).
- Plates with chamfers: `PlateSpec::chamfers` -> wire (lines + GC_MakeArcOfCircle) -> face -> prism.
  Analytic check: line = a*b/2, convex (a=b) = a^2(1-pi/4), concave (centre at corner) = pi*a^2/4.
- Part attributes (Material/Name/Finish/Pos_No) ride on `PlateSpec` and are applied in BOTH
  materializer sites; Pos_No has no model field -> `description = "Pos_No: <prefix><n>"`.
- Anything placed INSIDE the main part's section must use the section CENTRE, not the axis:
  `colC = colMid + colSec.height * col.sectionCenterOffset` (same sign as `halfExtent`). The app
  default anchor is Top, so the centre sits h/2 off the axis; a plate centred on the axis sticks
  out of the column. Case B (beam on the web) also needs the tab-plane H position projected onto
  the web mid-plane. Tests must run at least one case with `SectionAnchor::Top` + offset `-h/2`
  (default fixture is Center and hides this) and assert plate bbox ⊂ column bbox; mutation-check
  by `git checkout` of the engine file and confirm the new case fails.
- Reading a user's model screenshot without vision: classify pixels by the known display colours
  (plates 0x808080 grey, bolts brown, column colour from the screenshot) into an ASCII map; a grey
  region beyond the column's coloured extent shows a plate sticking out.
- Orientation semantics the user confirmed: 'Parallel to secondary' = vertical plate in the BEAM plane
  (also for skewed beams: clip the beam-direction line to the column pocket rectangle); 'Perpendicular
  to main' = XY-parallel plates at the beam top/bottom flange levels INSIDE the column (beam on flange:
  both pockets, 2x2 = 4; beam on web: opposite pocket only, 2). Several plates per role use keys
  'stiffener', 'stiffener2'... A vertical plate that cannot fit the pocket -> `plan.error`, never outside.
- Icon-row QComboBox popups open misaligned (Qt overlays the current row on the combo; 70px rows shift it
  ~85px). Fix: event filter on `view()->window()` Show that moves the popup under the combo (above if
  no room, width >= icon+text, clamped to the screen); test with offscreen `showPopup()` vs
  `mapToGlobal(0, height)` and mutation-check by removing the filter.
- Test trap: a helper returning a pointer into a temporary `GenerationPlan` (e.g.
  `stiff(gen(...))`) dangles; return `std::optional<PlateSpec>` by value.
- A canvas control that Tekla draws grey must be enabled when its spec is a real engine param:
  grey only on `spec->documentedOnly`.

## Footer buttons + modeless window (146)

- Tekla dialog is MODELESS: user keeps selecting in the model while it is open. Open tabbed
  dialogs with a static `ModelessHandle{QPointer<QDialog>, load}` map keyed by typeId; a second
  double-click on the same type reuses the open window and calls `load` for the new record.
- Button semantics: Apply = write dialog values to the edited instance, stay open; Modify = write
  to ALL selected instances of the same type (`Application::selectedComponentInstances(typeId)`,
  de-duplicated through ownership); Get = load the selected instance's values (`setParams`);
  Create = store params + start placement; OK = apply + close. A button is enabled only if its
  callback is assigned (`ButtonActions`). Do not auto-apply on selection change; offer it as an
  opt-in if the user asks.

## Text colour and highlight

- Canvas fields sit on a WHITE Tekla bitmap; under a dark system palette the default text is white
  and invisible. Set `color: black` explicitly in the canvas stylesheet for QLineEdit/SpinBox/
  ComboBox (disabled keeps grey). Test with a dark `QPalette` and grab the widget: assert dark pixels
  exist, and mutation-check by removing the colour rule.
- "Active field" highlight = highlight the TEXT (select-all on focus, blue/bold label), never an
  orange row/box background.

## Option pictures in dropdowns (Tekla help-page icons)

- The user's help PDF has no usable image ids; the web version of the same page keeps option icons
  in option ORDER per `##` section. Parse the saved page text, map `param -> (section, [option
  index per engine choice])` by hand (choice lists differ from Tekla's; Tekla order starts with
  Default, Automatic), curl the `GUID-<id>-publishing.png` files to `resources/tekla146/opt/`,
  emit the qrc block and a generated `option_icons.cpp` table. Generator lives in
  `tools/tekla146_canvas/mk_opt_icons.py`; re-run it, never hand-edit the table.
- Where the order is ambiguous (paired top/bottom images, captions missing) leave the dropdown
  without icons rather than guess.
- Apply the icons in the enum editor factory so canvas and plain forms both get them: item icon +
  text, `QListView` popup with a big `iconSize` (about 120x70). For transparent canvas IconCombos
  set the combo-face `iconSize` to 0 (the Tekla bitmap already shows the current choice) and keep
  the popup view's iconSize.
- Adding a core `.cpp` for the table means adding it to BOTH the exe source list and the widget
  test target in CMake, plus a reconfigure.
- A changed choice must NOT spawn a separate text box/label beside the combo (the user reads it as a
  stray edit field). Show the chosen option's picture (or its text when it has none) in the combo's own
  rectangle via a mouse-transparent QLabel under the transparent combo; hide it when the choice equals
  the Tekla bitmap's initial one. Test: change the index, assert one visible label with a pixmap at the
  combo geometry and no new labels; use `!isHidden()` (not `isVisible()`) because the tab may not be current.

## Component interaction rules (open / delete)

- Alt+left-click on a component glyph, plate or bolt display opens its parameter dialog. Handle it in
  the canvas mouse-down BEFORE the gumball hit test: the gumball of the selected part sits on the glyph
  and swallows the click. Skip it while a pending command, transform, grip drag or gumball drag is
  active; clicks that hit no component still fall through to Alt+click cut-flip / profile grips.
- Deleting a component's MAIN or SECONDARY part deletes the whole component: add the cascade in
  `Document::deleteModels` BEFORE the end-plate/bolt/weld cascades (they would otherwise leave
  half-removed children). Mirror `removeEndPlateConnection`: record removal, owned bolt groups
  (`removeBoltGroup`: holes + display), welds from `children` (kind Weld, `removeWeld`), cuts with
  `owner == component id`, then owned plates via the cascade-free `eraseModelLeaf`; push the surviving
  parts to `affectedTargets`. Deleting only an owned child keeps the old behaviour unless asked.
- Undo tests need `doc.pushSnapshot()` first (`recordUndoOp` silently drops ops when no record is
  open); then ONE `undo()` must restore record + plates + bolts + deleted part.
- Approval-gated shell commands that edit sources via inline scripts (mutation checks) can be blocked
  when the client cannot answer; run the mutation through `patch`/`write_file` edits or skip it and say so.

## "The new dialog doesn't show up" on the user's Windows machine

Code paths are shared (built-in 146 and the derived "... - Özel" template both go through
`applyShearPlate146Canvas`), so check delivery before debugging code:

1. Work lives on the active feature branch, not `main` -> a bare `git pull` on `main` brings
   nothing. Confirm with `git ls-remote origin-ssh refs/heads/<branch>` and give the user
   `git fetch origin` + `git checkout <branch>` + `git pull` + `git log -1 --oneline` (state the
   expected hash). A machine switch (home Asus ↔ work ZBook) is the usual trigger: each machine has
   its own checkout and build dir, and the one just left may have been on the feature branch.
2. A new `.qrc`/source needs `cmake -S . -B <build>` before the build; a half-failed build leaves the OLD exe.
   Also check whether `git merge --ff-only`/`pull` was REFUSED ("local changes would be overwritten"):
   a checkout inside a MEGA folder gets half-synced working-tree files from the other machine, so
   HEAD stays old and `ninja` says "no work to do". Do not fight it with stash/reset inside MEGA;
   clone the branch OUTSIDE MEGA (`git clone -b <branch> <url> /c/mm-src`) and configure there with
   the OCC flags read from the old `build-occ/CMakeCache.txt` (`OCC_ROOT`, `MM_OCC_LINK_DIRECT`).
3. Ask for `git branch --show-current`, `git log -1 --oneline`, `ls -la <build>/model-maker.exe`, and
   what the dialog shows (old 5-tab form / 13 tabs without images / nothing) before touching code.

## Hidden-tab trap

The widget skips a tab whose param rows resolve to zero (`form->rowCount()==0`). A tab made only
of `documentedOnly` fields therefore disappears wherever those specs are missing from the dialog's
spec list. User-catalog templates ("... - Özel") carry the spec list saved by an older build, so
`UserCatalog::registerInto` must merge the family dialog's `documentedOnly` specs into the derived
dialog. Symptom: the user's screenshot shows fewer tabs/fields than the code defines.

## Figures

- Data: `ComponentDialogGroup.figures` = `DialogFigure{L":/<prefix>/<file>.png", caption}`; widget
  helper `wrapWithFigures` puts them under the tab form. Figures are informational, never params.
- Files: `resources/tekla146/*.png` + `resources/tekla146.qrc` (aliased entries). Add the qrc to the
  `model-maker` executable and to the widget test target (AUTORCC is on). Adding a qrc needs a
  CMake reconfigure on Windows (`cmake -B build-occ`).
- Finding image ids: awk over the saved page text, remembering the last `##` heading and printing
  each `GUID-<id>-publishing.png` once; curl `https://support.tekla.com/dist/suf/product-guides/
  tekla-structures/en/<year>/GUID-<id>-publishing.png` (HTTP 200 without auth). Take the dimension /
  detail figures per tab; leave the option-icon strips until the option has geometry.
- If the vision tool errors, `tesseract <img> - --psm 11` still reads dialog text (tab names, field
  labels) and is enough to tell which build/dialog the user is looking at; say plainly that the
  drawing itself could not be judged.
- Copyright is Trimble's: fine for an internal tool, flag it before distributing the app.
