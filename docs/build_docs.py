from pathlib import Path
import subprocess
from contextlib import contextmanager
from typing import Union
import os
import sys


DOCS_ROOT = Path(__file__).resolve().parent


class CommandNotFoundError(Exception):
    def __init__(self, command: str):
        super().__init__(f"command '{command}' not found")
        self.command = command


@contextmanager
def cwd(path: Union[str, Path]):
    current_path = Path.cwd()

    if not isinstance(path, Path):
        path = Path(path)
    try:
        os.chdir(path)
        yield
    finally:
        os.chdir(current_path)


def run(cmd, **kwargs):
    if "check" not in kwargs:
        kwargs["check"] = True
    print(f"""> {" ".join(cmd)}""")
    try:
        return subprocess.run(cmd, **kwargs)
    except FileNotFoundError as e:
        raise CommandNotFoundError(cmd[0]) from e


def main():
    if not os.environ.get("VIRTUAL_ENV", ""):
        print("ERROR> script not running in ")
    try:
        import sphinx
    except ImportError:
        print("ERROR> 'sphinx' package not found")
        print("")
        print("To run this script, set up a virtual environment with all dependencies:")
        print("-------------------------")
        print("$ python3 -m venv .venv")
        print("$ source .venv/bin/activate")
        print("(.venv) $ pip install -r requirements.txt")
        print("-------------------------")
        print("")
        print("To run the script, enter the virtual environment, then run the script")
        print("-------------------------")
        print("$ source .venv/bin/activate")
        print("(.venv) $ python build_docs.py")
        sys.exit(1)

    print("generate API docs with doxygen...")
    print(f"DOCS ROOT: {DOCS_ROOT}")

    with cwd(DOCS_ROOT):
        try:
            run(["doxygen", ".doxygen"],
                cwd=DOCS_ROOT)

            run(["sphinx-build", "-b", "html", "source", "build"])
        except CommandNotFoundError as e:
            print(str(e))
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print("script aborted due to non-zero exit code from program")
            sys.exit(e.returncode)


if __name__ == "__main__":
    main()
