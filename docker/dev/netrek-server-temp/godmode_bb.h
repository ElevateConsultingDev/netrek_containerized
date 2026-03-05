/*
 * godmode_bb.h — 10x Battleship stats for human players only.
 * Robots keep standard BB values.
 *
 * Call godmode_bb_check() right after getship() in enter.c and interface.c.
 */
#ifndef GODMODE_BB_H
#define GODMODE_BB_H

static inline void godmode_bb_check(struct player *p, struct ship *s, int stype)
{
    if (!(p->p_flags & PFROBOT) && stype == BATTLESHIP) {
        s->s_maxspeed    *= 10;
        s->s_accint      *= 100;
        s->s_decint      *= 100;
        s->s_torpdamage  *= 10;
        s->s_phaserdamage *= 10;
        s->s_plasmadamage *= 10;
        s->s_torpspeed   *= 10;
        s->s_torpfuse    *= 10;
        s->s_plasmaspeed *= 10;
        s->s_plasmafuse  *= 10;
        s->s_maxfuel     *= 10;
        s->s_recharge    *= 10;
        s->s_repair      *= 10;
        s->s_maxshield   *= 10;
        s->s_maxdamage   *= 10;
        s->s_maxarmies   *= 10;
        s->s_wpncoolrate *= 10;
        s->s_egncoolrate *= 10;
        s->s_tractstr    *= 10;
        s->s_tractrng    *= 10;
        s->s_turns       *= 10;
        new_warning(UNDEF, "GOD-MODE BB ACTIVATED -- 10x all stats");
        ERROR(1, ("GOD-MODE BB: player %d (%s) got 10x battleship\n",
                  p->p_no, p->p_name));
    }
}

#endif
