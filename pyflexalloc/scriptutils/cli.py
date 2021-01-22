from scriptutils import colors


class Log:
    @staticmethod
    def _print(label: str, label_color: str, msg: str) -> None:
        print(
            f"{colors.B_WHITE}> [{label_color}{label}{colors.WHITE}] {colors.CLR}{msg}"
        )

    @classmethod
    def err(cls, msg: str) -> None:
        cls._print("ERR", colors.B_RED, msg)

    @classmethod
    def warn(cls, msg: str) -> None:
        cls._print("WARN", colors.B_YELLOW, msg)

    @classmethod
    def info(cls, msg: str) -> None:
        cls._print("INFO", colors.B_BLUE, msg)
