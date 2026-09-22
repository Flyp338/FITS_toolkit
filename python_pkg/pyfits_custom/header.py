import re
from typing import Any, Dict, Iterator, Optional, Tuple

class Header:
    """Pythonic dict-like wrapper around FITS 80-column card collections."""

    def __init__(self) -> None:
        self._cards: Dict[str, Tuple[Any, str]] = {}  # key -> (value, comment)

    @classmethod
    def _parse_value(cls, raw_val: str) -> Any:
        raw = raw_val.strip()
        if not raw:
            return ""

        # String value (enclosed in single quotes)
        if raw.startswith("'"):
            match = re.match(r"^'(.*?)'(\s*/.*)?$", raw)
            if match:
                return match.group(1).rstrip()
            return raw.strip("'")

        # Boolean
        if raw in ("T", "F"):
            return raw == "T"

        # Integer
        try:
            return int(raw)
        except ValueError:
            pass

        # Float / Complex
        try:
            return float(raw.replace("D", "E").replace("d", "e"))
        except ValueError:
            pass

        return raw

    def add_card(self, key: str, value_str: str, comment_str: str) -> None:
        key_clean = key.strip().upper()
        if not key_clean:
            return
        parsed_val = self._parse_value(value_str)
        self._cards[key_clean] = (parsed_val, comment_str.strip())

    def __getitem__(self, key: str) -> Any:
        return self._cards[key.upper()][0]

    def __setitem__(self, key: str, value: Any) -> None:
        key_clean = key.upper()
        comment = self._cards[key_clean][1] if key_clean in self._cards else ""
        self._cards[key_clean] = (value, comment)

    def __contains__(self, key: str) -> bool:
        return key.upper() in self._cards

    def get(self, key: str, default: Optional[Any] = None) -> Any:
        key_clean = key.upper()
        if key_clean in self._cards:
            return self._cards[key_clean][0]
        return default

    def get_comment(self, key: str) -> str:
        return self._cards[key.upper()][1]

    def keys(self):
        return self._cards.keys()

    def values(self):
        return [v[0] for v in self._cards.values()]

    def items(self):
        return [(k, v[0]) for k, v in self._cards.items()]

    def __iter__(self) -> Iterator[str]:
        return iter(self._cards)

    def __len__(self) -> int:
        return len(self._cards)

    def __repr__(self) -> str:
        lines = ["=== FITS Header ==="]
        for key, (val, comment) in self._cards.items():
            com_str = f" / {comment}" if comment else ""
            lines.append(f"{key:8s} = {repr(val):<20s}{com_str}")
        return "\n".join(lines)