# Muzzle: rounds leave the rifle, not the Eye

A Shot leaves the shooter's Eye today, lowered for its stance (ADR-0040): the
point a player sees from is the point its rounds start from. In VR the rifle is
in the player's hand, far from the Eye, and on a desktop a round from the Eye
fires from the head. A round leaves the rifle's Muzzle instead, in both Play
modes (ADR-0063). This takes effect in its own change, after ADR-0061's, so hits
change once and on purpose; that change amends ADR-0038, ADR-0039, ADR-0040 and
ADR-0044 to match.

**The rifle is authored.** Like a Character, the rifle is a USD stage, the one a
scenario's manifest names as its `rifle` (ADR-0041), whose default prim holds
four markers, each a frame pointing along its -Z: its **Grip**, where the hand
holds it; its **Muzzle**, the end of its barrel and the way it points; its
**Sight**, the point the Eye looks through in ADS and its line of sight; and its
**Fore-end**, where the support hand holds it (ADR-0066). Both packs carry the
four and no rifle mesh, since the server places the Muzzle and nothing that
reads a pack draws the rifle yet; the cooker refuses a stage missing any of
them, and a server does not start on a pack without them. The Muzzle in the
world is the holding hand's pose, times the Grip's inverse, times the Muzzle;
the Sight likewise.

**Where the hand is.** A VR player's hand is its bounded Tracked pose's
(ADR-0063). With the support hand on the Fore-end the barrel points from the
Grip toward it, as a rifle held in two hands does; with it elsewhere, along the
holding hand. A Desktop player holds the rifle where its pose holds it: its Grip
on the weapon socket of its posed right hand (ADR-0066), so the Muzzle follows
its stance, its Aim offset and its ADS grip, which shoulders the rifle with its
Sight at the Eye. The Posing phase places it, after posing the body, and fires
each round of the tick from it there. A client shows its local player's own
muzzle flash, predicted ahead of the Shot, at the Muzzle placed the same way:
its predicted body posed where it aims, the rifle held and turned by the Recoil
offset the round left with. It draws no rifle.

**Zeroing.** The barrel is pitched and turned so that a round crosses the line
of sight at the rifle's zero range, a Parameter (ADR-0039): above or below it a
round lands above or below the sight, as with a real rifle, and from the hip to
the side of it too. A desktop's line of sight is from the Eye along the view,
turned by the Recoil offset the round is fired with: the one before its own
kick, as the rifle the round leaves is turned by. It is solved on the
gravity-only ballistics, with no raycast. A VR player brings the sight to its
eye itself.

**Recoil turns the rifle about its Grip.** A desktop's Recoil offset turns the
view a Shot leaves along, and the rifle its pose holds turns with it: the Posing
phase turns the hand that holds the Grip by the turn the offset gives the view,
about the Grip, before it places the Muzzle and the Sight. The pivot is the
Grip, not the shoulder, because only the rifle takes the recoil: the body's
posed Joints and Hitboxes do not, since the Recoil offset is not replicated and
a client must be able to pose the same body from the Authoritative State. A turn
about the Grip keeps the rifle in the posed hand, where one about the shoulder
would carry it out of it; the left hand stays on the Fore-end as the pose held
it. A VR player's Recoil offset turns the barrel about the same Grip, with the
same pattern of kicks and the same return, and the player brings the rifle back
with its hands.

**ADS in VR.** A VR player is in ADS when its Tracked pose's Eye is on the
rifle's line of sight within a tolerance, which scales its recoil as ADS does; a
VR Command's ADS flag is ignored.

**A Muzzle in a wall fires into the wall.** The server tests the segment from
the holding arm's shoulder joint, the grandparent of the weapon socket's right
hand, to the Muzzle against the Map; if it is blocked, the round hits the Map
where the segment enters it, and does not fly on (ADR-0044).

## Considered Options

- **A Muzzle offset in the Parameters**: rejected. In VR the rifle has to be
  where the hand holds it, which an offset from the body cannot say.
- **Rounds parallel to the view from the Muzzle**: rejected. They miss the sight
  by the Muzzle's offset at every range.
- **Converging on what the view meets**: rejected. It needs a raycast per Shot
  and the convergence moves with whatever is in front.
- **Pushing the rifle back out of a wall**: rejected. A VR player's hand is
  where it is.
