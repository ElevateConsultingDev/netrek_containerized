// WebSocket <-> TCP relay for the classic netrek protocol.
// Browsers cannot open raw TCP, so the browser client speaks the real netrek
// packet stream over a binary WebSocket and this process relays the bytes
// verbatim to a netrek server (the containerized sturgeon build on :2692).
// No protocol knowledge lives here on purpose.
import net from "node:net";
import { WebSocketServer } from "ws";

const PORT = Number(process.env.BRIDGE_PORT ?? 8080);
const HOST = process.env.NETREK_HOST ?? "127.0.0.1";
const SERVER_PORT = Number(process.env.NETREK_PORT ?? 2692);

const wss = new WebSocketServer({ port: PORT });
console.log(`netrek bridge: ws://localhost:${PORT} -> ${HOST}:${SERVER_PORT}`);

wss.on("connection", (ws) => {
  const tcp = net.createConnection({ host: HOST, port: SERVER_PORT });
  const pending = [];
  let open = false;

  tcp.on("connect", () => {
    open = true;
    for (const b of pending.splice(0)) tcp.write(b);
  });
  tcp.on("data", (d) => ws.readyState === ws.OPEN && ws.send(d));
  tcp.on("close", () => ws.close());
  tcp.on("error", (e) => {
    console.error("tcp:", e.message);
    ws.close();
  });

  ws.on("message", (d) => (open ? tcp.write(d) : pending.push(d)));
  ws.on("close", () => tcp.destroy());
  ws.on("error", () => tcp.destroy());
});
