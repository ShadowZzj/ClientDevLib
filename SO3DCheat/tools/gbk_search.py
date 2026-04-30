import sys

def to_ce_pattern(text: str) -> str:
    raw = text.encode("gbk")
    return " ".join(f"{b:02X}" for b in raw)

if __name__ == "__main__":
    s= "黑暗料理界"
    print(to_ce_pattern(s))
