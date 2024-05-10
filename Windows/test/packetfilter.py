import os
import logging

# get current python file path, and set the log file packetfilter.log
logPath = os.path.dirname(os.path.abspath(__file__))
logPath = os.path.join(logPath, "packetfilter.log")
logging.basicConfig(filename=logPath, level=logging.info)

def PacketSniffer(bytesArray):
    logging.info("PacketSniffer: bytesArray: " + str(bytesArray))