// Netrek packet encode/decode. Packet formats are the "py-struct" comments in
// netrek-server/include/packets.h; this table is a port of the working python
// client (clients/netrek-client-pygame/netrek/protocol.py).
import { calcsize, cstr, pack, str, unpack, type Field } from "./struct";
import { CP, SHORTVERSION, SOCKVERSION, SP, UDPVERSION } from "./constants";

type Def = readonly [name: string, fmt: string, fields: readonly string[]];

export const SP_PACKETS: Record<number, Def> = {
  [SP.MESSAGE]: ["SP_MESSAGE", "!bBBB80s", ["type", "m_flags", "m_recpt", "m_from", "mesg"]],
  [SP.PLAYER_INFO]: ["SP_PLAYER_INFO", "!bbbb", ["type", "pnum", "shiptype", "team"]],
  [SP.KILLS]: ["SP_KILLS", "!bbxxI", ["type", "pnum", "kills"]],
  [SP.PLAYER]: ["SP_PLAYER", "!bbBbll", ["type", "pnum", "dir", "speed", "x", "y"]],
  [SP.TORP_INFO]: ["SP_TORP_INFO", "!bbbxhxx", ["type", "war", "status", "tnum"]],
  [SP.TORP]: ["SP_TORP", "!bBhll", ["type", "dir", "tnum", "x", "y"]],
  [SP.PHASER]: ["SP_PHASER", "!bbbBlll", ["type", "pnum", "status", "dir", "x", "y", "target"]],
  [SP.PLASMA_INFO]: ["SP_PLASMA_INFO", "!bbbxhxx", ["type", "war", "status", "pnum"]],
  [SP.PLASMA]: ["SP_PLASMA", "!bxhll", ["type", "pnum", "x", "y"]],
  [SP.WARNING]: ["SP_WARNING", "!bxxx80s", ["type", "mesg"]],
  [SP.MOTD]: ["SP_MOTD", "!bxxx80s", ["type", "line"]],
  [SP.YOU]: ["SP_YOU", "!bbbbbbxxIlllhhhh", ["type", "pnum", "hostile", "swar", "armies", "tractor", "flags", "damage", "shield", "fuel", "etemp", "wtemp", "whydead", "whodead"]],
  [SP.QUEUE]: ["SP_QUEUE", "!bxh", ["type", "pos"]],
  [SP.STATUS]: ["SP_STATUS", "!bbxxIIIIIL", ["type", "tourn", "armsbomb", "planets", "kills", "losses", "time", "timeprod"]],
  [SP.PLANET]: ["SP_PLANET", "!bbbbhxxl", ["type", "pnum", "owner", "info", "flags", "armies"]],
  [SP.PICKOK]: ["SP_PICKOK", "!bbxx", ["type", "state"]],
  [SP.LOGIN]: ["SP_LOGIN", "!bbxxl96s", ["type", "accept", "flags", "keymap"]],
  [SP.FLAGS]: ["SP_FLAGS", "!bbbxI", ["type", "pnum", "tractor", "flags"]],
  [SP.MASK]: ["SP_MASK", "!bbxx", ["type", "mask"]],
  [SP.PSTATUS]: ["SP_PSTATUS", "!bbbx", ["type", "pnum", "status"]],
  [SP.BADVERSION]: ["SP_BADVERSION", "!bbxx", ["type", "why"]],
  [SP.HOSTILE]: ["SP_HOSTILE", "!bbbb", ["type", "pnum", "war", "hostile"]],
  [SP.STATS]: ["SP_STATS", "!bbxx13l", ["type", "pnum", "tkills", "tlosses", "kills", "losses", "tticks", "tplanets", "tarmies", "sbkills", "sblosses", "armies", "planets", "maxkills", "sbmaxkills"]],
  [SP.PL_LOGIN]: ["SP_PL_LOGIN", "!bbbx16s16s16s", ["type", "pnum", "rank", "name", "monitor", "login"]],
  [SP.RESERVED]: ["SP_RESERVED", "!bxxx16s", ["type", "data"]],
  [SP.PLANET_LOC]: ["SP_PLANET_LOC", "!bbxxll16s", ["type", "pnum", "x", "y", "name"]],
  [SP.SCAN]: ["SP_SCAN", "!bbbbiiiiii", ["type", "pnum", "success", "pad1", "p_fuel", "p_armies", "p_shield", "p_damage", "p_etemp", "p_wtemp"]],
  [SP.UDP_REPLY]: ["SP_UDP_REPLY", "!bbxxi", ["type", "reply", "port"]],
  [SP.SEQUENCE]: ["SP_SEQUENCE", "!bBH", ["type", "flag8", "sequence"]],
  [SP.SC_SEQUENCE]: ["SP_SC_SEQUENCE", "!bxH", ["type", "sequence"]],
  [SP.RSA_KEY]: ["SP_RSA_KEY", "!bbbb32s", ["type", "pad1", "pad2", "pad3", "data"]],
  [SP.MOTD_PIC]: ["SP_MOTD_PIC", "!bbhhhhh1016s", ["type", "pad1", "x", "y", "page", "width", "height", "bits"]],
  [SP.STATS2]: ["SP_STATS2", "!bbxx22i", ["type", "pnum", "genocides", "maxkills", "di", "kills", "losses", "armsbomb", "resbomb", "dooshes", "planets", "tticks", "sbkills", "sblosses", "sbticks", "sbmaxkills", "wbkills", "wblosses", "wbticks", "wbmaxkills", "jsplanets", "jsticks", "rank", "royal"]],
  [SP.STATUS2]: ["SP_STATUS2", "!bbxx16I", ["type", "tourn", "dooshes", "armsbomb", "resbomb", "planets", "kills", "losses", "sbkills", "sblosses", "sbtime", "wbkills", "wblosses", "wbtime", "jsplanets", "jstime", "time", "timeprod"]],
  [SP.PLANET2]: ["SP_PLANET2", "!bbbbiii", ["type", "pnum", "owner", "info", "flags", "timestamp", "armies"]],
  [SP.NEW_MOTD]: ["SP_NEW_MOTD", "!bbxx", ["type", "pad1"]],
  [SP.THINGY]: ["SP_THINGY", "!bBhii", ["type", "dir", "tnum", "x", "y"]],
  [SP.THINGY_INFO]: ["SP_THINGY_INFO", "!bbhhh", ["type", "war", "shape", "tnum", "owner"]],
  [SP.SHIP_CAP]: ["SP_SHIP_CAP", "!bbHHHiiiiiiHHH1sx16s2sH", ["type", "operation", "s_type", "s_torpspeed", "s_phaserrange", "s_maxspeed", "s_maxfuel", "s_maxshield", "s_maxdamage", "s_maxwpntemp", "s_maxegntemp", "s_width", "s_height", "s_maxarmies", "s_letter", "s_name", "s_desig", "s_bitmap"]],
  [SP.PING]: ["SP_PING", "!bBHBBBB", ["type", "number", "lag", "tloss_sc", "tloss_cs", "iloss_sc", "iloss_cs"]],
  [SP.FEATURE]: ["SP_FEATURE", "!bbbbi80s", ["type", "feature_type", "arg1", "arg2", "value", "name"]],
};

export const PACKET_SIZES: Record<number, number> = Object.fromEntries(
  Object.entries(SP_PACKETS).map(([t, [, fmt]]) => [Number(t), calcsize(fmt)]),
);

const TEXT_FIELDS = new Set([
  "mesg",
  "line",
  "name",
  "monitor",
  "login",
  "data",
  "keymap",
  "s_name",
]);

export type Packet = Record<string, Field | string> & { type: number };

export function decodePacket(ptype: number, data: Uint8Array): Packet | null {
  const def = SP_PACKETS[ptype];
  if (!def) return null;
  const [, fmt, fields] = def;
  const values = unpack(fmt, data);
  const out: Record<string, Field | string> = {};
  fields.forEach((f, i) => {
    const v = values[i]!;
    out[f] = v instanceof Uint8Array && TEXT_FIELDS.has(f) ? cstr(v) : v;
  });
  return out as Packet;
}

// --- client -> server ---

export const cpSocket = () => pack("!bbbxI", CP.SOCKET, SOCKVERSION, UDPVERSION, 0);
export const cpLogin = (query: number, name: string, password: string, login: string) =>
  // pad2/pad3 magic 0x69,0x42 marks a paradise-aware client, as COW sends.
  pack("!bbbb16s16s16s", CP.LOGIN, query, 0x69, 0x42, str(name, 16), str(password, 16), str(login, 16));
export const cpOutfit = (team: number, ship = 0) => pack("!bbbx", CP.OUTFIT, team, ship);
export const cpSpeed = (speed: number) => pack("!bbxx", CP.SPEED, speed);
export const cpDirection = (dir: number) => pack("!bBxx", CP.DIRECTION, dir & 0xff);
export const cpPhaser = (dir: number) => pack("!bBxx", CP.PHASER, dir & 0xff);
export const cpTorp = (dir: number) => pack("!bBxx", CP.TORP, dir & 0xff);
export const cpPlasma = (dir: number) => pack("!bBxx", CP.PLASMA, dir & 0xff);
export const cpShield = (on: number) => pack("!bbxx", CP.SHIELD, on);
export const cpCloak = (on: number) => pack("!bbxx", CP.CLOAK, on);
export const cpRepair = (on: number) => pack("!bbxx", CP.REPAIR, on);
export const cpOrbit = (on: number) => pack("!bbxx", CP.ORBIT, on);
export const cpBomb = (on: number) => pack("!bbxx", CP.BOMB, on);
/** state: 1 = beam up, 2 = beam down */
export const cpBeam = (state: number) => pack("!bbxx", CP.BEAM, state);
export const cpDetTorps = () => pack("!bxxx", CP.DET_TORPS);
export const cpDetMyTorp = (tnum: number) => pack("!bxh", CP.DET_MYTORP, tnum);
export const cpWar = (mask: number) => pack("!bbxx", CP.WAR, mask);
export const cpRefit = (ship: number) => pack("!bbxx", CP.REFIT, ship);
export const cpPlanlock = (pnum: number) => pack("!bbxx", CP.PLANLOCK, pnum);
export const cpPlaylock = (pnum: number) => pack("!bbxx", CP.PLAYLOCK, pnum);
export const cpTractor = (on: number, pnum: number) => pack("!bbbx", CP.TRACTOR, on, pnum);
export const cpRepress = (on: number, pnum: number) => pack("!bbbx", CP.REPRESS, on, pnum);
export const cpCoup = () => pack("!bxxx", CP.COUP);
export const cpPractr = () => pack("!bxxx", CP.PRACTR);
export const cpDockperm = (on: number) => pack("!bbxx", CP.DOCKPERM, on);
export const cpQuit = () => pack("!bxxx", CP.QUIT);
export const cpBye = () => pack("!bxxx", CP.BYE);
export const cpUpdates = (usecs: number) => pack("!bxxxI", CP.UPDATES, usecs);
export const cpMessage = (group: number, indiv: number, mesg: string) =>
  pack("!bBBx80s", CP.MESSAGE, group, indiv, str(mesg, 80));
export const cpPingResponse = (number = 0, pingme = 1, sent = 0, recv = 0) =>
  pack("!bBbxll", CP.PING_RESPONSE, number, pingme, sent, recv);
export const cpFeature = (kind: string, arg1: number, arg2: number, value: number, name: string) =>
  pack("!bbbbi80s", CP.FEATURE, kind.charCodeAt(0), arg1, arg2, value, str(name, 80));
export const cpSReq = (req: number, version = SHORTVERSION) =>
  pack("!bbbb", CP.S_REQ, req, version, 0);
