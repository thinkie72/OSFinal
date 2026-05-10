Drop one PNG per team into this folder. Don't worry about cropping,
centering, or whether you have a transparent background — the app
auto-trims whatever border color it finds (white, gray, transparent,
whatever the top-left pixel is) and fits the result into each tile.

== Filename rules ==

The filename must match the team name exactly as it appears in data.csv,
with spaces replaced by underscores, plus .png:

  Charlotte Hornets        ->  Charlotte_Hornets.png
  Golden State Warriors    ->  Golden_State_Warriors.png
  Los Angeles Lakers       ->  Los_Angeles_Lakers.png
  Oklahoma City Thunder    ->  Oklahoma_City_Thunder.png
  Portland Trail Blazers   ->  Portland_Trail_Blazers.png

If a team has no PNG here, its tile shows a colored rectangle with the
team name overlaid, so the app still works while you're collecting logos.

== Where the app looks ==

The binary checks (in order):

  logos/        <- relative to wherever you launched the binary
  ../logos/     <- one level up
  ../../logos/  <- two levels up

So you can keep PNGs here in the source tree and the binary finds them
whether it's run from build/, cmake-build-debug/, or the project root.
No CMake reconfigure needed after adding a logo.

== Image guidance ==

Anything works, but a few tips:

- A clean background (solid white or transparent) gives the best auto-trim
  result. The trimmer uses the top-left pixel as its background reference.
- Square-ish logos look best in the 150x100 tile, but tall/wide logos are
  preserved at their aspect ratio.
- 200-400px on the longest side is plenty; bigger PNGs just waste memory.
- NBA team logos are copyrighted. Source from official press kits or your
  school's licensed assets, and don't commit them to a public repo.

(The .gitignore in this project already excludes logos/*.png for that
reason, so commits skip them by default.)
