"""Pair B: vault approval. Send CHAL with send_line(), check RESP in on_line().

Week 1: request_approval() sends a fresh challenge; the watch answers at once and on_line()
checks the answer. Test: python laptop/security_test.py --usb
Week 2: the watch answers only after a double-tap; open the vault window when approved.
"""
import hashlib
import hmac
import secrets

# Must be byte-for-byte the same as SEC_KEY in watch/security.ino.
# Hardcoded on both sides: a known limitation, stated in the video.
KEY = bytes.fromhex("3d5e34e66d2ad3c7421b7821ac420151d07a67bc848e9930ac3d959cd7dbc62a")
ACTION = "VAULT"

send_line = None
pending = None  # the challenge we are waiting on; each one is used once, so a replayed RESP fails


def setup(send):
    global send_line
    send_line = send


def expected_hex(challenge, action=ACTION):
    """HMAC-SHA256 over the 16 raw challenge bytes then the action's ASCII bytes, as lowercase hex."""
    return hmac.new(KEY, challenge + action.encode("ascii"), hashlib.sha256).hexdigest()


def request_approval():
    """Send a new random challenge to the watch. Returns the challenge, or None if not connected."""
    global pending
    challenge = secrets.token_bytes(16)
    if not send_line(f"CHAL:{challenge.hex()}:{ACTION}"):
        return None
    pending = challenge
    return challenge


def check_resp(line):
    """'approved', 'rejected' or 'invalid' for a RESP line. Uses up the pending challenge."""
    global pending
    challenge, pending = pending, None
    payload = line[len("RESP:"):]
    if challenge is None:
        return "invalid"  # nothing was asked, e.g. an old reply sent again
    if payload == "REJECT":
        return "rejected"
    if hmac.compare_digest(payload, expected_hex(challenge)):  # constant time: no timing hints
        return "approved"
    return "invalid"


def on_line(line):
    """Return True if this line was for you."""
    if not line.startswith("RESP:"):
        return False
    result = check_resp(line)
    print(f"Vault request: {result}")  # Week 2: open the vault window when approved
    return True


if __name__ == "__main__":
    # Known answer: the watch must print the same for this challenge (see watch/security.ino).
    fixed = bytes.fromhex("8f3a1c0d9e2b7a6f5c4d3e2f1a0b9c8d")
    print("CHAL:8f3a1c0d9e2b7a6f5c4d3e2f1a0b9c8d:VAULT ->", "RESP:" + expected_hex(fixed))

    sent = []
    setup(lambda text: sent.append(text) or True)
    c = request_approval()
    good = "RESP:" + expected_hex(c)
    assert sent == [f"CHAL:{c.hex()}:VAULT"]
    assert check_resp(good) == "approved"
    assert check_resp(good) == "invalid"  # replay: the challenge was already used
    request_approval()
    assert check_resp(good) == "invalid"  # old reply to a new challenge
    request_approval()
    assert check_resp("RESP:REJECT") == "rejected"
    print("security ok")
