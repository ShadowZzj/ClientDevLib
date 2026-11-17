import os
import logging
print("asdasasd")
# get current python file path, and set the log file packetfilter.log
logPath = os.path.dirname(os.path.abspath(__file__))
logPath = os.path.join(logPath, "packetfilter.log")
logging.basicConfig(filename=logPath,level=logging.DEBUG,format='%(asctime)s %(message)s')

def PacketSniffer(bytesArray: bytearray):
    # format the bytes to 00 aa ff
    bytesArrayString = " ".join("{:02x}".format(c) for c in bytesArray)
    logging.info("Packet: " + bytesArrayString)
    packetLen = int.from_bytes(bytesArray[0:2], byteorder='little')
    packetId = int.from_bytes(bytesArray[4:8], byteorder='little')
    if packetId == 0x64578:
        packetType = "Move"
        direction = int.from_bytes(bytesArray[8:12], byteorder='little')
        x = int.from_bytes(bytesArray[12:16], byteorder='little')
        z = int.from_bytes(bytesArray[16:20], byteorder='little')
        mapId = int.from_bytes(bytesArray[20:24], byteorder='little')
        logging.info("Move: direction: " + str(direction) + " x: " + str(x) + " z: " + str(z) + " mapId: " + str(mapId))
    elif packetId == 0x6458C:
        packetType = "Buy"
        itemId = int.from_bytes(bytesArray[12:16], byteorder='little')
        amount = int.from_bytes(bytesArray[16:20], byteorder='little')
        bagPos = int.from_bytes(bytesArray[20:24], byteorder='little')
        sellerId = int.from_bytes(bytesArray[24:28], byteorder='little')
        logging.info(f"Buy: itemId: {itemId:#x} amount: {amount:#x} bagPos: {bagPos:#x} sellerId: {sellerId:#x}")
    elif packetId == 0x6458D:
        packetType = "Sell"
        bagPos = int.from_bytes(bytesArray[8:12], byteorder='little')
        amount = int.from_bytes(bytesArray[12:16], byteorder='little')
        sellerId = int.from_bytes(bytesArray[16:20], byteorder='little')
        logging.info(f"Sell: bagPos: {bagPos:#x} amount: {amount:#x} sellerId: {sellerId:#x}")
    else:
        data = bytesArray[8:]
        packetDataString = " ".join("{:02x}".format(c) for c in data)
        logging.info(f"PacketLen: {packetLen:#x} PacketId: {packetId:#x} PacketData: {packetDataString}")
    return 0