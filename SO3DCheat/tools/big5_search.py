import sys

def to_ce_pattern(text: str) -> str:
    raw = text.encode("big5")
    return " ".join(f"{b:02X}" for b in raw)

if __name__ == "__main__":
    s= "多重粉碎"
    print(to_ce_pattern(s))
