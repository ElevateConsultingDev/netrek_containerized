"""Torpedo auto-aim: lead calculation ported from robotd/warfare.c:get_torp_course().

Provides visual aim indicator — computes the intercept point where a
torpedo would meet a moving target, so the player can see where to aim.
"""
import math
from .constants import PALIVE, PFCLOAK

# WARP1: 20 game-coords per speed-unit per server update
_WARP1 = 20

# Default torpedo fuse in server updates.  The server doesn't send
# s_torpfuse via SP_SHIP_CAP; 40 matches cruiser/battleship defaults.
_DEFAULT_TORPFUSE = 40


def torp_range(torp_speed):
    """Max torpedo range in game coordinates."""
    return torp_speed * _WARP1 * _DEFAULT_TORPFUSE


def compute_intercept(me, target, torp_speed):
    """Compute intercept point for a torpedo to hit a moving target.

    Port of robotd/warfare.c:get_torp_course().  Decomposes the target's
    velocity into components parallel and perpendicular to the line of
    sight.  The torpedo must match the perpendicular component exactly;
    remaining speed goes along the line of sight toward the target.

    WARP1 cancels out of both the direction and intercept-point
    calculations, so raw speed values are used throughout.

    Returns (intercept_gx, intercept_gy) in game coordinates, or None if
    no intercept is possible (target's perpendicular velocity exceeds
    torpedo speed, or target is outrunning torpedo along LOS).
    """
    # Target velocity from dir and speed
    # Netrek: Cos[d] = sin(d*pi/128), Sin[d] = -cos(d*pi/128)
    t_angle = target.dir * math.pi / 128
    vxs = math.sin(t_angle) * target.speed
    vys = -math.cos(t_angle) * target.speed

    # Unit vector from shooter to target
    dx = target.x - me.x
    dy = target.y - me.y
    dist = math.hypot(dx, dy)
    if dist == 0:
        return None
    vxa = dx / dist
    vya = dy / dist

    # Parallel component of target velocity along line of sight
    dp = vxs * vxa + vys * vya

    # Perpendicular component of target velocity
    perp_x = vxs - dp * vxa
    perp_y = vys - dp * vya
    perp_sq = perp_x * perp_x + perp_y * perp_y

    # Torpedo must match perpendicular component; check feasibility
    torp_sq = torp_speed * torp_speed
    remainder = torp_sq - perp_sq
    if remainder <= 0:
        return None

    # Parallel torpedo speed along LOS
    para = math.sqrt(remainder)

    # Closing speed along LOS (torpedo toward target minus target fleeing)
    closing = para - dp
    if closing <= 0:
        return None

    # Time to intercept (WARP1 cancels: dist is game-coords, closing is
    # speed-units, but the ratio dist/closing * vxs gives game-coords)
    t = dist / closing
    ix = target.x + t * vxs
    iy = target.y + t * vys

    # Check that the intercept is within torpedo range
    intercept_dist = math.hypot(ix - me.x, iy - me.y)
    if intercept_dist > torp_range(torp_speed):
        return None

    return (ix, iy)


def find_target(gs, cursor_gx, cursor_gy, max_range):
    """Find nearest alive enemy player to the cursor position.

    Only considers enemies within max_range of the player's ship (torp
    range) and within half the tactical view of the cursor.  Skips
    cloaked, dead, and friendly ships.

    Returns the Player closest to (cursor_gx, cursor_gy), or None.
    """
    me = gs.me
    if not me:
        return None
    # Only consider enemies near the cursor (half tactical view width)
    cursor_max_sq = (10000) ** 2
    best_dist = float('inf')
    best = None
    for p in gs.players:
        if p.pnum == gs.me_pnum:
            continue
        if p.status != PALIVE:
            continue
        if p.team == me.team:
            continue
        if p.flags & PFCLOAK:
            continue
        # Must be within torp range of our ship
        ship_dist_sq = (p.x - me.x) ** 2 + (p.y - me.y) ** 2
        if ship_dist_sq > max_range * max_range:
            continue
        # Must be reasonably close to cursor
        cursor_dist_sq = (p.x - cursor_gx) ** 2 + (p.y - cursor_gy) ** 2
        if cursor_dist_sq > cursor_max_sq:
            continue
        if cursor_dist_sq < best_dist:
            best_dist = cursor_dist_sq
            best = p
    return best
