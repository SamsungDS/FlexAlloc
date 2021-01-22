"""ANSI color escape codes

NOTE: every color is preceeded by a RESET ANSI code."""

CLR = "\033[0m"  # clear styling

GREY = "\033[0m\033[90m"
RED = "\033[0m\033[91m"
GREEN = "\033[0m\033[92m"
YELLOW = "\033[0m\033[93m"
BLUE = "\033[0m\033[94m"
MAGENTA = "\033[0m\033[95m"
CYAN = "\033[0m\033[96m"
WHITE = "\033[0m\033[97m"

B_CLR = "\033[0m\033[1m"  # clear styling, then apply bold

B_GREY = "\033[0m\033[1m\033[90m"
B_RED = "\033[0m\033[1m\033[91m"
B_GREEN = "\033[0m\033[1m\033[92m"
B_YELLOW = "\033[0m\033[1m\033[93m"
B_BLUE = "\033[0m\033[1m\033[94m"
B_MAGENTA = "\033[0m\033[1m\033[95m"
B_CYAN = "\033[0m\033[1m\033[96m"
B_WHITE = "\033[0m\033[1m\033[97m"
