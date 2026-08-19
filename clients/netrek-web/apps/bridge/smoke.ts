// Self-check: talk to a real netrek server through the bridge, log in, pick a
// ship, and prove we decode the galaxy. Run the bridge first, then:
//   npx tsx apps/bridge/smoke.ts
import WebSocket from "ws";
import { PacketStream } from "../client/lib/netrek/stream";
import { SP, TEAM, SHIP } from "../client/lib/netrek/constants";
import * as p from "../client/lib/netrek/protocol";

const URL = process.env.BRIDGE_URL ?? "ws://127.0.0.1:8080";
const NAME = process.env.NETREK_NAME ?? "webtest";

const ws = new WebSocket(URL);
const stream = new PacketStream();
const send = (b: Uint8Array) => ws.send(b);
const seen = new Map<number, number>();
const planets = new Map<number, string>();
let motd = 0;
let me = -1;
let outfitted = false;

ws.on("open", () => {
  send(p.cpSocket());
  for (const f of ["FEATURE_PACKETS", "SHIP_CAP", "RC_DISTRESS", "NEWMACRO", "WHY_DEAD"])
    send(p.cpFeature("S", 1, 0, 1, f));
  send(p.cpLogin(0, NAME, "", NAME)); // query=0: guest login
});

ws.on("message", (data: Buffer) => {
  for (const pkt of stream.feed(new Uint8Array(data))) {
    seen.set(pkt.type, (seen.get(pkt.type) ?? 0) + 1);
    switch (pkt.type) {
      case SP.MOTD:
        motd++;
        break;
      case SP.LOGIN:
        console.log("SP_LOGIN accept=", pkt.accept);
        if (pkt.accept === 1 && !outfitted) {
          outfitted = true;
          send(p.cpOutfit(TEAM.FED, SHIP.CA));
          send(p.cpUpdates(100000)); // 10 updates/sec
        }
        break;
      case SP.PICKOK:
        console.log("SP_PICKOK state=", pkt.state);
        break;
      case SP.YOU:
        me = pkt.pnum as number;
        break;
      case SP.PLANET_LOC:
        planets.set(pkt.pnum as number, `${pkt.name}@${pkt.x},${pkt.y}`);
        break;
      case SP.PING:
        send(p.cpPingResponse(pkt.number as number));
        break;
      case SP.MESSAGE:
        if (String(pkt.mesg).trim()) console.log("msg:", String(pkt.mesg).trim());
        break;
      case SP.BADVERSION:
        console.error("SP_BADVERSION why=", pkt.why);
        break;
    }
  }
});

ws.on("error", (e) => console.error("ws error:", e.message));

setTimeout(() => {
  console.log(`\nmotd lines: ${motd}`);
  console.log(`my slot: ${me}`);
  console.log(`planets known: ${planets.size}`, [...planets.values()].slice(0, 3));
  console.log("packet counts:", Object.fromEntries([...seen].sort((a, b) => a[0] - b[0])));
  const ok = me >= 0 && planets.size === 40 && (seen.get(SP.PLAYER) ?? 0) > 10;
  console.log(ok ? "\nPASS" : "\nFAIL");
  send(p.cpBye());
  ws.close();
  process.exit(ok ? 0 : 1);
}, 6000);
