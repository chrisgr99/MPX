# Tabs and encapsulation in VCV Rack

A brainstorm. Nothing here is built, nothing is decided, and no work is scheduled on it.

The question behind it: DreamRack splits a rack into tabs, and a tab is a voice — it has ports on it, so a cable connects to the tab and, inside the tab, from the tab to the modules it contains. That encapsulates both the contents and the connections to them. Rack has nothing like it. What could be reproduced, and how closely?

## What Rack gives you already

**No encapsulation of any kind.** There is no subpatch, no container, no nesting, and no plugin provides one. A proper enclosure with its own panel and its own jacks is a long-standing community request that nobody has built.

**Selections, natively.** Drag a box round some modules, right-click and choose "Save selection as", and you get a `.vcvs` file holding those modules, their settings and the cables between them. File then Import selection brings it back. It is a stamp rather than a container — the modules arrive as ordinary separate modules — but a voice you like is one menu command away from any patch, wired and set.

**stoermelder STRIP**, which is the nearest existing thing. STRIP sits at one end of a row and claims every module touching it, with no gaps, as a "strip". It saves and loads that whole group with its cables as a `.vcvss` file, and `.vcvss` files in a `presets/Strip` folder appear in STRIP's own preset menu — so a voice becomes an item picked from a menu on a module. STRIP will also disable or randomise the group as a unit. PackOne is maintained and in the library.

The limits: the modules must be contiguous in a single row, they occupy their full width, and there is no custom panel and no single set of jacks on the outside. You still patch into whichever module inside the group needs the signal.

**Not the 4ms MetaModule.** It does run VCV patches as though they were one module, but it is a hardware Eurorack module that plays patches built on a computer. There is no in-Rack equivalent.

## The camera half: tabs that frame a group

Supported API, no hacks needed for the mechanism.

`RackScrollWidget` exposes `zoomToBound`, which takes a rectangle in module coordinates and sets both offset and zoom to frame it, plus `getZoom`, `setZoom`, `getGridOffset` and `setGridOffset`. None is marked private. `RackWidget` exposes `getModule` by module id, `getModules`, and `getSelected`; a module widget's `box` is in the same coordinates. So framing a group is one call, and the group's rectangle is the union of its members' boxes.

`getSelected` gives the authoring flow: drag a box round a voice, click "New tab from selection", type a name.

**Define a tab by its member module ids, not by a stored rectangle**, and recompute the bounding box on every click. Ids are stable within a patch and save with it, so the tab survives nudging a module, adding to the voice, or dragging the whole thing elsewhere. Keep the tab list in the module's own JSON so it travels with the patch.

`zoomToBound` snaps. Easing zoom and offset over a few frames is worth doing: the jump is most of what makes this feel like teleporting rather than navigating.

### Where the tab bar lives

This is the only part that is a hack, and it is a mild one. `Scene` exposes `menuBar` as a public widget pointer, so a plugin can add a child to it, which lands to the right of the existing items. But `createMenuBar` is marked private in the header, so the menu bar's internals are not plugin API and its layout could shift between Rack versions. There is precedent for this holding up: stoermelder's MB replaces Rack's module browser outright, and GLUE draws its labels as a scene overlay.

Putting the tab strip on a module instead is fully supported, but a navigation control that scrolls off screen when you navigate is self-defeating.

So: a module owns the tabs, holds their state and does the authoring, and a small fixed strip is added to the scene so the tabs stay reachable. Lose the overlay to a Rack update and the module still works — you click the tabs on it.

## The ports half: a tab as an interface

This is the part that makes it encapsulation rather than a view, and it needs one shift in thinking. Rack has no inside, so a tab's port cannot be one object. It is a **pair**: an outside face that cables plug into, and an inside face that the contents patch to — the same port seen from either side of a boundary that does not otherwise exist. Nothing crosses the boundary except through it.

**This works today with no code.** Little Utils has Teleport In and Teleport Out: type a four-character label on an In, pick it from a menu on an Out, and signals cross with no cable. They are polyphonic, and one In can feed several Outs. A bidirectional tab interface needs two pairs, one carrying signals in and one carrying them back out, so four modules per tab. Clunky, but enough to find out whether the idea feels right before anything is written.

**The version worth writing is one module placed twice**: a single type with both inputs and outputs, two instances sharing a tab name, each passing its inputs to the other's outputs. The mechanism already exists in this plugin — the MPX note bus is a cable-free publish and subscribe keyed by slot, with claim, release and generations to handle modules coming and going. The audio and CV version is the same machinery carrying sample frames instead of events.

### What this actually buys: substitutability

Once every signal crossing the boundary goes through the face, the contents become interchangeable. Save a voice as a strip, load a different voice into that row, and as long as its inside face carries the same tab name, the outside patching never knew.

That is what encapsulation is for — not tidiness, interchangeability. Tabs as a camera cannot give it at all. Tabs with ports can.

## The whole thing, bound by one name

- the tab bar frames the group, with `zoomToBound`
- the outside face is the group's interface
- STRIP saves, loads and disables the contents as a unit
- the tab's name binds all three, so swapping a voice is loading a strip

## What to settle before trusting it

**Process order.** A side channel does not get whatever ordering a real cable gets, so expect up to a sample of slippage depending on which instance processes first. Teleport modules have lived with that for years without trouble for audio or CV, but it matters in a feedback path and in anything meant to be sample-accurate. It is measurable with a bench rather than a mystery.

**Polyphony.** The faces must copy channel counts, not just values.

**Orphans.** A face whose partner is deleted has to fail quietly, which is what claim and release are already for.

## What it will not do

Hide anything. The modules inside a tab still sit in the rack taking up their width whether you are looking at them or not. This is encapsulation of the connections and of the voice as a reusable unit, but not of the panel: the tab is a boundary, not a lid.

That may be enough. It is most of what a DreamRack tab does, and the part that is missing is the part Rack is least willing to give up.

Worth noticing the design shift, too. With ports on it, a tab stops being a navigation aid that happens to group things and becomes a declaration of a voice's interface. The navigation turns into a side effect of something more useful.

## References

- stoermelder PackOne, including STRIP: https://github.com/stoermelder/vcvrack-packone
- Little Utils, including Teleport: https://library.vcvrack.com/LittleUtils
- Can modules be grouped?: https://community.vcvrack.com/t/can-modules-be-grouped/11063
- Share your patch selections: https://community.vcvrack.com/t/share-your-patch-selections-vcvs/21818
- Ways to condense a patch?: https://community.vcvrack.com/t/ways-to-condense-a-patch/7539
