"""Entry point and game loop for the Netrek pygame client."""
import os
import sys
import argparse
import time
import pygame

from .constants import TARGET_FPS
from .config import Config
from .network import Connection, ServerDisconnected
from .gamestate import GameState
from .statemachine import StateMachine, State
from .sprites import SpriteManager
from .renderer import Renderer
from .input_handler import InputHandler
from .sound import SoundManager


def _find_rc():
    """Auto-discover netrek.rc: ~/.netrekrc, then sibling netrek_containerized/."""
    home_rc = os.path.expanduser("~/.netrekrc")
    if os.path.isfile(home_rc):
        return home_rc

    # Sibling directory relative to this package's parent
    pkg_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parent = os.path.dirname(pkg_dir)
    for sibling in ("vanilla_netrek_containerized", "netrek_containerized"):
        sibling_rc = os.path.join(parent, sibling, "netrek.rc")
        if os.path.isfile(sibling_rc):
            return sibling_rc

    return None


def parse_args():
    parser = argparse.ArgumentParser(description="Netrek pygame client")
    parser.add_argument("-s", "--server", default="localhost",
                        help="Netrek server hostname (default: localhost)")
    parser.add_argument("-p", "--port", type=int, default=2592,
                        help="Netrek server port (default: 2592)")
    parser.add_argument("--name", default="guest",
                        help="Character name (default: guest)")
    parser.add_argument("--password", default="",
                        help="Password for character")
    parser.add_argument("--login", default="pygame",
                        help="Login name (default: pygame)")
    parser.add_argument("--rc", default=None, metavar="PATH",
                        help="Path to netrek.rc config file")
    parser.add_argument("--no-udp", action="store_true",
                        help="Force TCP-only (needed for Docker servers)")
    return parser.parse_args()


def main():
    args = parse_args()

    # Discover rc file
    rc_path = args.rc or _find_rc()

    # Initialize pygame
    pygame.init()
    pygame.font.init()
    clock = pygame.time.Clock()

    # Create core objects
    config = Config(rc_path=rc_path)
    if args.no_udp:
        config.try_udp = False
    if config.rc_path:
        print(f"Loaded config: {config.rc_path}")
    else:
        print("No netrek.rc found, using defaults")

    sound = None
    if config.sound_enabled:
        sound = SoundManager()
        sound.load()

    conn = Connection()
    gs = GameState()
    gs.conn = conn
    sm = StateMachine(conn, gs, name=args.name, password=args.password,
                      login=args.login, sound=sound, config=config)
    sprites = SpriteManager()
    input_handler = InputHandler(conn, gs, sm, config, sound=sound)
    renderer = Renderer(gs, sprites, config, statemachine=sm,
                        server_host=args.server, input_handler=input_handler)

    # Initialize display (must happen before sprite loading for convert_alpha)
    renderer.init()

    # Load sprites
    sprites.load()

    # Connect to server
    def do_connect():
        """Establish TCP connection and send handshake. Returns True on success."""
        print(f"Connecting to {args.server}:{args.port}...")
        try:
            conn.connect(args.server, args.port)
        except OSError as e:
            print(f"Connection failed: {e}")
            return False
        print("Connected. Sending handshake...")
        sm.start()
        return True

    if not do_connect():
        pygame.quit()
        sys.exit(1)

    # Main game loop with reconnection
    running = True
    reconnect_delay = 0.0
    max_reconnect_delay = 10.0

    while running:
        # --- Reconnection state ---
        if sm.state == State.DISCONNECTED:
            # Drain pygame events so the window stays responsive
            for event in pygame.event.get():
                if event.type == pygame.VIDEORESIZE:
                    renderer.handle_resize(event)
                elif event.type == pygame.QUIT:
                    running = False
                    break
                elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                    running = False
                    break
            if not running:
                break

            # Show reconnecting message and render frame
            renderer.render()
            clock.tick(TARGET_FPS)

            # Wait for reconnect delay
            if reconnect_delay > 0:
                time.sleep(min(reconnect_delay, 0.5))
                reconnect_delay -= 0.5
                continue

            # Attempt reconnection
            conn.reset()
            gs.reset()
            sm.reset_for_reconnect(conn)

            if do_connect():
                reconnect_delay = 0.0
                print("Reconnected successfully.")
            else:
                sm.state = State.DISCONNECTED
                reconnect_delay = min(reconnect_delay + 2.0, max_reconnect_delay)
                print(f"Reconnect failed, retrying in {reconnect_delay:.0f}s...")
            continue

        # --- Normal game loop ---
        for event in pygame.event.get():
            if event.type == pygame.VIDEORESIZE:
                renderer.handle_resize(event)
                continue
            if input_handler.handle_event(event, renderer.tactical_offset,
                                          renderer.scale_info):
                running = False
                break

        if not running:
            break

        # Receive and process network packets
        try:
            packets = conn.recv_packets(timeout=0.0)
        except ServerDisconnected as e:
            print(f"Disconnected: {e}")
            sm.state = State.DISCONNECTED
            reconnect_delay = 1.0
            if sound:
                sound.on_death()
            continue

        for ptype, pkt in packets:
            sm.handle_packet(ptype, pkt)

        # Tick state machine timers
        sm.tick()

        # Tick input handler timers (info window auto-dismiss, auto-aim)
        input_handler.tick_info()
        input_handler.tick_auto_aim(renderer.scale_info)

        # Tick sound state-change detection
        if sound:
            sound.tick(gs, gs.me_pnum)

        # Interpolate positions for smooth rendering
        gs.interpolate()

        # Render
        renderer.render()

        # Cap framerate
        clock.tick(TARGET_FPS)

    # Cleanup
    print("Quitting...")
    try:
        sm.quit()
    except Exception:
        pass
    conn.close()
    pygame.quit()


if __name__ == "__main__":
    main()
