"""A fake RetroAchievements server for the desktop's RetroAchievements tests (desktop/README.md, RetroAchievements).

    python -I tools/fake-ra-server.py [--port 8765] [--log requests.jsonl] [--fail-awards N]

then run the desktop with --ra-host http://127.0.0.1:8765 (and --ra-user test --ra-token t0k3n to log in for the run). Python's standard
library only. It answers dorequest.php (POST form fields or a GET query, as rc_client 12.2 sends them) for:

    login2            a password or a token: any user, any password but "wrong" (invalid_credentials), any token but "expired"
                      (expired_token) or "invalid" (invalid_credentials); a password login's token is "t0k3n"
    achievementsets   (and the older patch and gameid) one game for ANY hash: "Fake Game" (game 1, console 18, the DS), with
                      A1 (101) "Three hundred frames", which unlocks 300 frames after the game's achievements start
                      A2 (102) "Never", which never unlocks
                      A3 (103) "Measured", with a measured target (as a percent, 36000 frames) that shows a progress indicator
                      a leaderboard (201) "Frames", which starts 60 frames in and submits 600 frames in (hardcore only: rc_client keeps
                      leaderboards off in softcore), its value the frames since it started
                      a rich presence script with one constant line
    startsession      no unlocks yet (every run starts from nothing)
    awardachievement  the unlock, with a score; the first N (--fail-awards) get HTTP 503, which rc_client retries
    submitlbentry     the entry, ranked first of one
    ping              accepted
    lbinfo, unlocks, allprogress   small fixed answers
and any other r= value gets {"Success":false} with an error. A GET of any path ending in .png (the badges, the game's icon, the avatar
the answers point at) gets a 64 x 64 PNG in a color made from the path.

--log FILE writes one JSON line per request: {"r": ..., "fields": {...}, "hardcore": the h field as a number or null, "status": ...},
with the password (p) and token (t) values replaced by "<redacted>". Each request is also printed.
"""
import argparse
import json
import struct
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

GAME_ID = 1
DS_CONSOLE_ID = 18
TOKEN = "t0k3n"
ACHIEVEMENTS = [
    (101, "Three hundred frames", "Play for 300 frames", 5, "0xH000000=0xH000000.300."),
    (102, "Never", "Never unlocks", 10, "0xH000000!=0xH000000"),
    (103, "Measured", "Play for 36000 frames (shows progress)", 25, "G:0xH000000=0xH000000.36000."),
]
LEADERBOARD = (201, "Frames", "Frames between the start and the submit",
               "STA:0xH000000=0xH000000.60.::CAN:0xH000000!=0xH000000::SUB:0xH000000=0xH000000.600.::VAL:M:0xH000000=0xH000000")
RICH_PRESENCE = "Display:\nPlaying Fake Game\n"
SECRET_FIELDS = ("p", "t")


def png(width, height, rgb):
    """A plain RGB PNG of one color."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


class State:
    def __init__(self, fail_awards, log_path):
        self.lock = threading.Lock()
        self.fail_awards = fail_awards
        self.awarded = set()
        self.score = 1000
        self.softcore_score = 100
        self.log = open(log_path, "a", encoding="utf-8") if log_path else None

    def write_log(self, entry):
        line = json.dumps(entry, sort_keys=True)
        with self.lock:
            print(line, flush=True)
            if self.log:
                self.log.write(line + "\n")
                self.log.flush()


def game_sets(base):
    icon = f"{base}/Images/000001.png"
    achievements = [{
        "ID": ident, "Title": title, "Description": description, "Flags": 3, "Points": points, "MemAddr": memaddr,
        "Author": "fake", "BadgeName": f"{ident:05d}", "Created": 1700000000, "Modified": 1700000000, "Type": "",
        "Rarity": 50.0, "RarityHardcore": 25.0,
        "BadgeURL": f"{base}/Badge/{ident:05d}.png", "BadgeLockedURL": f"{base}/Badge/{ident:05d}_lock.png",
    } for ident, title, description, points, memaddr in ACHIEVEMENTS]
    ident, title, description, mem = LEADERBOARD
    leaderboards = [{"ID": ident, "Title": title, "Description": description, "Mem": mem, "Format": "FRAMES",
                     "LowerIsBetter": False, "Hidden": False}]
    return {
        "Success": True, "GameId": GAME_ID, "Title": "Fake Game", "ConsoleId": DS_CONSOLE_ID, "ImageIconUrl": icon,
        "RichPresenceGameId": GAME_ID, "RichPresencePatch": RICH_PRESENCE,
        "Sets": [{"AchievementSetId": 1, "GameId": GAME_ID, "Title": "Fake Game", "Type": "core", "ImageIconUrl": icon,
                  "Achievements": achievements, "Leaderboards": leaderboards}],
    }


def patch_data(base):
    """The older r=patch answer, the same game."""
    sets = game_sets(base)
    first = sets["Sets"][0]
    return {"Success": True, "PatchData": {
        "ID": GAME_ID, "Title": "Fake Game", "ConsoleID": DS_CONSOLE_ID, "ImageIcon": "/Images/000001.png",
        "ImageIconURL": sets["ImageIconUrl"], "RichPresencePatch": RICH_PRESENCE,
        "Achievements": first["Achievements"], "Leaderboards": first["Leaderboards"]}}


def answer(state, api, fields, base):
    """(status, JSON object) for one dorequest.php call."""
    user = fields.get("u", "test")
    if api == "login2":
        if "p" in fields:
            if fields["p"] == "wrong":
                return 401, {"Success": False, "Error": "Invalid user/password combination. Please try again.",
                             "Code": "invalid_credentials"}
        elif fields.get("t") == "expired":
            return 401, {"Success": False, "Error": "The access token has expired. Please log in again.", "Code": "expired_token"}
        elif fields.get("t") in (None, "", "invalid"):
            return 401, {"Success": False, "Error": "Invalid user/token combination.", "Code": "invalid_credentials"}
        with state.lock:
            score, softcore = state.score, state.softcore_score
        return 200, {"Success": True, "User": user, "Token": TOKEN, "Score": score, "SoftcoreScore": softcore, "Messages": 0,
                     "AvatarUrl": f"{base}/UserPic/{user}.png"}
    if api == "achievementsets":
        return 200, game_sets(base)
    if api == "patch":
        return 200, patch_data(base)
    if api == "gameid":
        return 200, {"Success": True, "GameID": GAME_ID}
    if api == "startsession":
        return 200, {"Success": True, "Unlocks": [], "HardcoreUnlocks": [], "ServerNow": int(time.time())}
    if api == "awardachievement":
        with state.lock:
            if state.fail_awards > 0:
                state.fail_awards -= 1
                return 503, None
            ident = int(fields.get("a", "0"))
            points = next((p for i, _, _, p, _ in ACHIEVEMENTS if i == ident), 0)
            hardcore = fields.get("h") == "1"
            if (ident, hardcore) not in state.awarded:
                state.awarded.add((ident, hardcore))
                if hardcore:
                    state.score += points
                else:
                    state.softcore_score += points
            remaining = len(ACHIEVEMENTS) - len({i for i, h in state.awarded if h == hardcore})
            return 200, {"Success": True, "Score": state.score, "SoftcoreScore": state.softcore_score, "AchievementID": ident,
                         "AchievementsRemaining": max(remaining, 0)}
    if api == "submitlbentry":
        score = int(fields.get("s", "0"))
        return 200, {"Success": True, "Response": {
            "Score": score, "BestScore": score, "RankInfo": {"Rank": 1, "NumEntries": "1"},
            "TopEntries": [{"User": user, "Rank": 1, "Score": score}]}}
    if api == "ping":
        return 200, {"Success": True}
    if api == "lbinfo":
        return 200, {"Success": True, "LeaderboardData": {"LBID": LEADERBOARD[0], "GameID": GAME_ID, "LowerIsBetter": 0,
                                                          "LBTitle": LEADERBOARD[1], "LBDesc": LEADERBOARD[2], "LBFormat": "FRAMES",
                                                          "LBMem": LEADERBOARD[3], "LBAuthor": "fake", "LBCreated": "2023-11-14",
                                                          "LBUpdated": "2023-11-14", "TotalEntries": 0, "Entries": []}}
    if api == "unlocks":
        return 200, {"Success": True, "UserUnlocks": [], "GameID": GAME_ID, "HardcoreMode": fields.get("h") == "1"}
    if api == "allprogress":
        return 200, {"Success": True, "Response": {}}
    return 200, {"Success": False, "Error": f"Unknown request: {api}"}


class Handler(BaseHTTPRequestHandler):
    server_version = "FakeRA/1"

    def log_message(self, format, *args):
        pass

    def base(self):
        return f"http://{self.headers.get('Host', '127.0.0.1')}"

    def send(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def dorequest(self, fields):
        state = self.server.state
        api = fields.get("r", "")
        status, reply = answer(state, api, fields, self.base())
        hardcore = fields.get("h")
        state.write_log({
            "r": api,
            "fields": {k: ("<redacted>" if k in SECRET_FIELDS else v) for k, v in sorted(fields.items())},
            "hardcore": int(hardcore) if hardcore is not None and hardcore.isdigit() else None,
            "status": status,
        })
        if reply is None:
            self.send(status, b"<html><body>503 Service Temporarily Unavailable</body></html>", "text/html")
        else:
            self.send(status, json.dumps(reply).encode("utf-8"), "application/json")

    def do_POST(self):
        length = min(int(self.headers.get("Content-Length", "0") or 0), 1 << 20)
        body = self.rfile.read(length).decode("utf-8", "replace")
        url = urlsplit(self.path)
        if url.path != "/dorequest.php":
            self.send(404, b"not found", "text/plain")
            return
        fields = {k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()}
        fields.update({k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()})
        self.dorequest(fields)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/dorequest.php":
            self.dorequest({k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()})
        elif url.path.endswith(".png"):
            seed = zlib.crc32(url.path.encode("utf-8"))
            self.send(200, png(64, 64, ((seed >> 16) & 255, (seed >> 8) & 255, seed & 255)), "image/png")
        else:
            self.send(404, b"not found", "text/plain")


def main():
    parser = argparse.ArgumentParser(description="A fake RetroAchievements server for the desktop's tests")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--log", help="a JSON line per request, appended to this file")
    parser.add_argument("--fail-awards", type=int, default=0, help="answer the first N awardachievement requests with HTTP 503")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.state = State(args.fail_awards, args.log)
    print(f"fake RetroAchievements server on http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
