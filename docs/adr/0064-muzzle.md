# Muzzle: rounds leave the rifle, not the Eye

A Shot leaves the shooter's Eye today, lowered for its stance (ADR-0040): the
point a player sees from is the point its rounds start from. In VR the rifle is
in the player's hand, far from the Eye, and on a desktop a round from the Eye
fires from the head. A round leaves the rifle's Muzzle instead, in both Play
modes (ADR-0063). This takes effect in its own change, after ADR-0061's, so hits
change once and on purpose; that change amends ADR-0038, ADR-0039, ADR-0040 and
ADR-0044 to match.

**The rifle is authored.** Like a Character, the rifle is a USD stage with two
markers: its Grip, where the hand holds it, and its Muzzle, the end of its
barrel and the way it points. Both packs carry them, since the server places the
Muzzle. The Muzzle in the world is the holding hand's pose, times the Grip's
inverse, times the Muzzle.

**Where the hand is.** A VR player's hand is its bounded Tracked pose's
(ADR-0063). With the support hand on the handguard the barrel points from the
Grip toward it, as a rifle held in two hands does; with it elsewhere, along the
holding hand. A Desktop player holds the rifle as its synthesized pose does:
shouldered along its view, with its sight on the Eye in ADS, and from the hip
that hold moved by the rifle's **hip offset**, a Parameter (ADR-0039), along the
view's own axes. The hold is placed from the Eye lowered for the stance, along
the view turned by the Recoil offset. Until the Posing phase poses the right
hand (ADR-0066), the desktop hand is synthesized so from the view, after Posing;
a posed or a tracked hand takes its place and nothing else changes.

**Zeroing.** The barrel is pitched so that a round crosses the line of sight at
the rifle's zero range, a Parameter (ADR-0039): above or below it a round lands
above or below the sight, as with a real rifle. A desktop's line of sight is
from the Eye along the view; a VR player brings the sight to its eye itself.

**Recoil and ADS in VR.** A desktop's Recoil offset stays on its view. A VR
player's turns the barrel about the Grip, with the same pattern of kicks and the
same return, and the player brings the rifle back with its hands. A VR player is
in ADS when its Tracked pose's Eye is on the rifle's line of sight within a
tolerance, which scales its recoil as ADS does; a VR Command's ADS flag is
ignored.

**A Muzzle in a wall fires into the wall.** The server tests the segment from
the holding arm's shoulder joint to the Muzzle against the Map; if it is
blocked, the round hits the Map where it is, and does not fly on.

## Considered Options

- **A Muzzle offset in the Parameters**: rejected. In VR the rifle has to be
  where the hand holds it, which an offset from the body cannot say.
- **Rounds parallel to the view from the Muzzle**: rejected. They miss the sight
  by the Muzzle's offset at every range.
- **Converging on what the view meets**: rejected. It needs a raycast per Shot
  and the convergence moves with whatever is in front.
- **Pushing the rifle back out of a wall**: rejected. A VR player's hand is
  where it is.
