import os
names = os.listdir(r"\\.\pipe\\")
ggtb = [n for n in names if "GGTB" in n]
print("GGTB pipes:", ggtb)
