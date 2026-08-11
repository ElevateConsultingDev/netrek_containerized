# Game constants from struct.h, packets.h, defs.h

# --- Geometry ---
GWIDTH = 100000
SCALE = 40
TWINSIDE = 500
GWINSIDE = 500
WARP1 = 20
EXPDIST = 350
DETDIST = 1700
PHASEDIST = 6000
ENTORBDIST = 900
ORBDIST = 800
ORBSPEED = 2

# --- Limits ---
MAXPLAYER = 36
MAXPLANETS = 40
MAXTORP = 8
MAXPLASMA = 1
NUM_TYPES = 8

# --- Teams ---
NOBODY = 0x0
FED = 0x1
ROM = 0x2
KLI = 0x4
ORI = 0x8
ALLTEAM = FED | ROM | KLI | ORI

TEAM_NAMES = {NOBODY: "Ind", FED: "Fed", ROM: "Rom", KLI: "Kli", ORI: "Ori"}
TEAM_LETTERS = {NOBODY: "X", FED: "F", ROM: "R", KLI: "K", ORI: "O"}
# COW data.c teamlet[]: indexed by team bitmask -> single letter
TEAMLET = {NOBODY: 'I', FED: 'F', ROM: 'R', KLI: 'K', ORI: 'O', ALLTEAM: 'A'}
SHIPNOS = "0123456789abcdefghijklmnopqrstuvwxyz"
TEAM_COLORS = {
    NOBODY: (170, 170, 170),  # grey
    FED: (255, 255, 0),       # yellow
    ROM: (255, 0, 0),         # red
    KLI: (0, 255, 0),         # green
    ORI: (0, 255, 255),       # cyan
}
# Map team bitmask to folder name for sprites
TEAM_SPRITE_DIR = {NOBODY: "Ind", FED: "Fed", ROM: "Rom", KLI: "Kli", ORI: "Ori"}

# --- Ship types ---
SCOUT = 0
DESTROYER = 1
CRUISER = 2
BATTLESHIP = 3
ASSAULT = 4
STARBASE = 5
SGALAXY = 6
ATT = 7

SHIP_NAMES = {
    SCOUT: "SC", DESTROYER: "DD", CRUISER: "CA", BATTLESHIP: "BB",
    ASSAULT: "AS", STARBASE: "SB", SGALAXY: "GA", ATT: "AT",
}

# --- Player status ---
PFREE = 0
POUTFIT = 1
PALIVE = 2
PEXPLODE = 3
PDEAD = 4
POBSERV = 5

# --- Player flags ---
PFSHIELD = 0x0001
PFREPAIR = 0x0002
PFBOMB = 0x0004
PFORBIT = 0x0008
PFCLOAK = 0x0010
PFWEP = 0x0020
PFENG = 0x0040
PFROBOT = 0x0080
PFBEAMUP = 0x0100
PFBEAMDOWN = 0x0200
PFSELFDEST = 0x0400
PFGREEN = 0x0800
PFYELLOW = 0x1000
PFRED = 0x2000
PFPLOCK = 0x4000
PFPLLOCK = 0x8000
PFCOPILOT = 0x10000
PFWAR = 0x20000
PFPRACTR = 0x40000
PFDOCK = 0x80000
PFREFIT = 0x100000
PFREFITTING = 0x200000
PFTRACT = 0x400000
PFPRESS = 0x800000
PFDOCKOK = 0x1000000
PFOBSERV = 0x8000000

# --- Torp status ---
TFREE = 0
TMOVE = 1
TEXPLODE = 2
TDET = 3
TOFF = 4
TSTRAIGHT = 5

# --- Plasma status ---
PTFREE = 0
PTMOVE = 1
PTEXPLODE = 2
PTDET = 3

# --- Phaser status ---
PHFREE = 0x00
PHHIT = 0x01
PHMISS = 0x02
PHHIT2 = 0x04

# --- Planet flags ---
PLREPAIR = 0x010
PLFUEL = 0x020
PLAGRI = 0x040
PLHOME = 0x100
PLCORE = 0x800

# --- Message flags ---
MVALID = 0x01
MINDIV = 0x02
MTEAM = 0x04
MALL = 0x08
MGOD = 0x10
MDISTR = 0xC0

# Address header length (COW defs.h ADDRLEN)
ADDRLEN = 10

# --- Death reasons ---
KQUIT = 0x01
KTORP = 0x02
KPHASER = 0x03
KPLANET = 0x04
KSHIP = 0x05
KDAEMON = 0x06
KWINNER = 0x07
KGHOST = 0x08
KGENOCIDE = 0x09

# --- Server packet types (SP_) ---
SP_MESSAGE = 1
SP_PLAYER_INFO = 2
SP_KILLS = 3
SP_PLAYER = 4
SP_TORP_INFO = 5
SP_TORP = 6
SP_PHASER = 7
SP_PLASMA_INFO = 8
SP_PLASMA = 9
SP_WARNING = 10
SP_MOTD = 11
SP_YOU = 12
SP_QUEUE = 13
SP_STATUS = 14
SP_PLANET = 15
SP_PICKOK = 16
SP_LOGIN = 17
SP_FLAGS = 18
SP_MASK = 19
SP_PSTATUS = 20
SP_BADVERSION = 21
SP_HOSTILE = 22
SP_STATS = 23
SP_PL_LOGIN = 24
SP_RESERVED = 25
SP_PLANET_LOC = 26
SP_SCAN = 27
SP_UDP_REPLY = 28
SP_SEQUENCE = 29
SP_SC_SEQUENCE = 30
SP_RSA_KEY = 31
SP_MOTD_PIC = 32
SP_STATS2 = 33
SP_STATUS2 = 34
SP_PLANET2 = 35
SP_NEW_MOTD = 36
SP_THINGY = 37
SP_THINGY_INFO = 38
SP_SHIP_CAP = 39
SP_S_REPLY = 40
SP_S_MESSAGE = 41
SP_S_WARNING = 42
SP_S_YOU = 43
SP_S_YOU_SS = 44
SP_S_PLAYER = 45
SP_PING = 46
SP_S_TORP = 47
SP_S_TORP_INFO = 48
SP_S_8_TORP = 49
SP_S_PLANET = 50
SP_GPARAM = 51
SP_PARADISE_EXT1 = 52
SP_TERRAIN2 = 53
SP_TERRAIN_INFO2 = 54
SP_S_SEQUENCE = 56
SP_S_PHASER = 57
SP_S_KILLS = 58
SP_S_STATS = 59
SP_FEATURE = 60

# --- Client packet types (CP_) ---
CP_MESSAGE = 1
CP_SPEED = 2
CP_DIRECTION = 3
CP_PHASER = 4
CP_PLASMA = 5
CP_TORP = 6
CP_QUIT = 7
CP_LOGIN = 8
CP_OUTFIT = 9
CP_WAR = 10
CP_PRACTR = 11
CP_SHIELD = 12
CP_REPAIR = 13
CP_ORBIT = 14
CP_PLANLOCK = 15
CP_PLAYLOCK = 16
CP_BOMB = 17
CP_BEAM = 18
CP_CLOAK = 19
CP_DET_TORPS = 20
CP_DET_MYTORP = 21
CP_COPILOT = 22
CP_REFIT = 23
CP_TRACTOR = 24
CP_REPRESS = 25
CP_COUP = 26
CP_SOCKET = 27
CP_OPTIONS = 28
CP_BYE = 29
CP_DOCKPERM = 30
CP_UPDATES = 31
CP_RESETSTATS = 32
CP_RESERVED = 33
CP_SCAN = 34
CP_UDP_REQ = 35
CP_SEQUENCE = 36
CP_PING_RESPONSE = 42
CP_S_REQ = 43
CP_S_THRS = 44
CP_S_MESSAGE = 45
CP_FEATURE = 60

# --- Protocol constants ---
SOCKVERSION = 4
UDPVERSION = 10

# --- UDP handshake constants (socket.c / udp.c) ---
# CP_UDP_REQ request codes
COMM_TCP = 0
COMM_UDP = 1
COMM_VERIFY = 2

# SP_UDP_REPLY reply codes
SWITCH_TCP_OK = 0
SWITCH_UDP_OK = 1
SWITCH_VERIFY = 2
SWITCH_DENIED = 3

# Connection modes
CONNMODE_PORT = 0
CONNMODE_PACKET = 1

# Receive modes
MODE_TCP = 0
MODE_SIMPLE = 1

UDP_HANDSHAKE_TIMEOUT = 5.0

# --- Short packet request codes (SPK_*) ---
SPK_VOFF = 0          # variable packets off
SPK_VON = 1           # variable packets on
SPK_MOFF = 2          # obsolete
SPK_MON = 3           # messages on
SPK_M_KILLS = 4       # kill messages on
SPK_M_NOKILLS = 5     # obsolete
SPK_THRESHOLD = 6     # set threshold
SPK_M_WARN = 7        # warning messages on
SPK_M_NOWARN = 8      # obsolete
SPK_SALL = 9          # small update: weapons+planets+kills
SPK_ALL = 10          # full update minus stats

# Short packet protocol version
SHORTVERSION = 11     # SP2 version (server: SP2SHORTVERSION)
OLDSHORTVERSION = 10  # SP1 version

# Short warning special types
SHORT_WARNING = 33    # like CP_S_MESSAGE (variable length)
STEXTE_STRING = 34    # variable length text

# Coordinate constants used by short packets
SPWINSIDE = 500       # server tactical window size
VPLANET_SIZE = 6      # bytes per planet in SP_S_PLANET

MSG_LEN = 80
NAME_LEN = 16
KEYMAP_LEN = 96
RESERVED_SIZE = 16

# --- Layout (from COW newwin.c) ---
BORDER = 3
MESSAGESIZE = 20
STATSIZE = MESSAGESIZE * 2 + BORDER  # 43 - dashboard height

# --- FPS ---
TARGET_FPS = 30
UPDATE_RATE = 10  # server updates per second
