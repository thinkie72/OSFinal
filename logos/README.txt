Drop one PNG per team into this folder. The app renders each PNG as-is,
fit aspect-preserving and centered inside its tile — so the easiest way
to get a consistent look is to make every screenshot the same shape and
size with the logo centered on white.

== Filename rules ==

The filename must match the team name exactly as it appears in data.csv,
with spaces replaced by underscores, plus .png:

  Charlotte Hornets        ->  Charlotte_Hornets.png
  Golden State Warriors    ->  Golden_State_Warriors.png
  Los Angeles Lakers       ->  Los_Angeles_Lakers.png
  Oklahoma City Thunder    ->  Oklahoma_City_Thunder.png
  Portland Trail Blazers   ->  Portland_Trail_Blazers.png

If a team has no PNG, its tile shows a colored rectangle with the team
name overlaid, so the app still works while you're collecting logos.

== Screenshot recipe ==

For visual consistency, screenshot each logo so it lives inside a
uniformly-sized white square or rectangle, e.g.:

  - Image size: 400 x 400 (square) — every logo same size on screen.
    Each tile shows the logo as an ~84x84 white card on dark background.

  - OR 600 x 400 (3:2 aspect) — fills the tile edge-to-edge with white.

Either works; the only thing that matters is that you use the same
dimensions for all 30 logos. The fit-and-center code in the app handles
the rest.

== Where the app looks ==

The binary checks (in order):

  logos/        <- relative to wherever you launched the binary
  ../logos/     <- one level up
  ../../logos/  <- two levels up

So you can keep PNGs here in the source tree and the binary finds them
whether it's run from build/, cmake-build-debug/, or the project root.
No CMake reconfigure needed after adding a logo.

== Image guidance ==

- White background is recommended (gives a "trading-card" look on the
  dark UI tile). Transparent PNGs also work — they just render directly
  on the dark tile background.
- 200-600px on the longest side is plenty; bigger PNGs just waste memory.
- NBA team logos are copyrighted. Source from official press kits or
  your school's licensed assets. The .gitignore in this project already
  excludes logos/*.png so they won't get committed to a public repo.
