"""Pair B: the vault window. "Open vault" sends a challenge; the vault opens only on an approved reply.

The window runs in its own thread with its own Tk loop; the link runs in main.py's asyncio loop.
security.py starts it and reports each result with show_result().
"""
import queue
import threading

try:
    import tkinter as tk
except ImportError:  # e.g. Linux without the python3-tk package
    tk = None

SECRETS = [
    "Wi-Fi password: correct-horse-battery-staple",
    "Locker code: 4-1-7-3",
    "Exam notes: chapter 7, question 3",
]
NO_REPLY_S = 13  # the watch rejects after 10 s; give up waiting a little later

events = queue.Queue()  # results from security.py, read by the window thread


def show_result(result):
    """'approved', 'rejected', 'invalid' or 'sent'; safe to call from any thread."""
    events.put(result)


def start(request):
    """Open the vault window. request() asks the watch and returns False if no watch is connected."""
    if tk is None:
        print("Vault window needs tkinter (on Linux: sudo apt install python3-tk)")
        return
    threading.Thread(target=_window, args=(request,), daemon=True).start()


def _window(request):
    root = tk.Tk()
    root.title("TinyKey")
    root.resizable(False, False)
    status = tk.Label(root, text="Vault locked", width=34, pady=10)
    status.pack()
    button = tk.Button(root, text="Open vault", width=16)
    button.pack(pady=(0, 12))
    waiting = {"timer": None}

    def done(text):
        if waiting["timer"]:
            root.after_cancel(waiting["timer"])
            waiting["timer"] = None
        status.config(text=text)
        button.config(state=tk.NORMAL)

    def ask():
        if not request():
            status.config(text="No watch connected")
            return
        button.config(state=tk.DISABLED)
        status.config(text="Double-tap the watch to approve")
        waiting["timer"] = root.after(NO_REPLY_S * 1000, lambda: done("No reply from the watch"))

    def open_vault():
        win = tk.Toplevel(root)
        win.title("Vault")
        tk.Label(win, text="Vault", font=("TkDefaultFont", 14, "bold"), pady=8).pack()
        for line in SECRETS:
            tk.Label(win, text=line, anchor="w", padx=16).pack(fill="x")
        tk.Button(win, text="Close", command=win.destroy).pack(pady=12)

    def poll():
        while not events.empty():
            result = events.get()
            if result == "approved":
                done("Approved")
                open_vault()
            elif result == "rejected":
                done("Rejected on the watch")
            elif result == "invalid":
                done("Invalid reply: vault stays locked")
        root.after(100, poll)

    button.config(command=ask)
    poll()
    root.mainloop()
