"""Entry point and game loop for the Netrek pygame client."""
import os
import sys
import argparse
import pygame

from .constants import TARGET_FPS
from .config import Config
from .network import Connection
from .gamestate import GameState
from .statemachine import StateMachine
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
    print(f"Connecting to {args.server}:{args.port}...")
    try:
        conn.connect(args.server, args.port)
    except OSError as e:
        print(f"Connection failed: {e}")
        pygame.quit()
        sys.exit(1)

    print("Connected. Sending handshake...")
    sm.start()

    # Main game loop
    running = True
    while running:
        # Process pygame events
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
        packets = conn.recv_packets(timeout=0.0)
        for ptype, pkt in packets:
            sm.handle_packet(ptype, pkt)

        # Tick state machine timers
        sm.tick()

        # Tick input handler timers (info window auto-dismiss)
        input_handler.tick_info()

        # Tick sound state-change detection
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
