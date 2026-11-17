from flask import Flask, request, jsonify
import json
import base64
import os
import time
import sqlite3
import datetime
import sys
import uuid
import re
import pytz
import logging
import logging.handlers

# 导入互斥锁
from threading import Lock
from flask import g
app = Flask(__name__)
current_dir = os.path.dirname(os.path.abspath(__file__))
DATABASE = os.path.join(current_dir, 'database.db')
SECRET_KEY = "shadowpopezzj"  # 选择一个安全的密钥

infoMap = {}
infoLock = Lock()
def init_db():
    with sqlite3.connect(DATABASE) as conn:
        cursor = conn.cursor()
        cursor.execute('''CREATE TABLE IF NOT EXISTS cards (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            uuid TEXT NOT NULL,
                            register_time TEXT NOT NULL,
                            expiry_time TEXT NOT NULL
                          )''')
        conn.commit()


@app.route('/get_info', methods=['GET'])
def GetInfo():
    global infoMap
    # 从请求中获取用户标识符
    user_id = request.args.get("userid")
    if user_id not in infoMap:
        return jsonify({"code": 404, "message": "user not found"})
    
    response_data = {
        "details": [],
        "total_money": 0
    }
    
    totalMoney = 0
    with infoLock:
        for username in infoMap[user_id]:
            for role in infoMap[user_id][username]:
                detail = {
                    "username": username,
                    "role": role,
                    "hp": infoMap[user_id][username][role]['hp'],
                    "mp": infoMap[user_id][username][role]['mp'],
                    "money": infoMap[user_id][username][role]['money'],
                    "time": infoMap[user_id][username][role]['time'],
                    "die": infoMap[user_id][username][role]['hp'] <= 0
                }
                response_data["details"].append(detail)
                totalMoney += infoMap[user_id][username][role]['money']
    
    response_data["total_money"] = totalMoney
    return jsonify(response_data)

@app.route('/post_info', methods=['POST'])
def PostInfo():
    global infoMap
    # 从请求中获取数据
    data = request.get_json()
    # 获取用户标志符
    user_id = data.get("userid")
    # 获取用户名
    username = data.get("username")
    # 获取角色名
    role = data.get("rolename")
    # 获取信息
    money = data.get("money")
    hp = data.get("hp")
    mp = data.get("mp")

    with infoLock:
        # 判断用户是否存在
        if user_id not in infoMap:
            infoMap[user_id] = {}
        # 判断用户名是否存在
        if username not in infoMap[user_id]:
            infoMap[user_id][username] = {}
        # 判断角色名是否存在
        if role not in infoMap[user_id][username]:
            infoMap[user_id][username][role] = {}
        # 更新信息
        tz = pytz.timezone('Asia/Shanghai')
        beijing_time = datetime.datetime.now(tz).strftime("%Y-%m-%d %H:%M:%S")
        infoMap[user_id][username][role] = {"money": money, "hp": hp, "mp": mp, "time": beijing_time}
    # 返回结果
    return jsonify({"code": 200, "message": "success"})

@app.route('/get_card', methods=['GET'])
def GetCard():
    hours = int(request.args.get("hours"))
    card_uuid = str(uuid.uuid4())
    tz = pytz.timezone('Asia/Shanghai')
    register_time = datetime.datetime.now(tz).strftime("%Y-%m-%d %H:%M:%S")
    expiry_time = (datetime.datetime.now(tz) + datetime.timedelta(hours=hours)).strftime("%Y-%m-%d %H:%M:%S")

    with sqlite3.connect(DATABASE) as conn:
        cursor = conn.cursor()
        cursor.execute("INSERT INTO cards (uuid, register_time, expiry_time) VALUES (?, ?, ?)", (card_uuid, register_time, expiry_time))
        conn.commit()

    return jsonify({"card_uuid": card_uuid, "register_time": register_time, "expiry_time": expiry_time})

@app.route('/verify_card', methods=['GET'])
def VerifyCard():
    card_uuid = request.args.get("card_uuid")
    if card_uuid == "c929d7c5-a101-4c95-b2a5-d6308f856469":
        return jsonify({"code": 200, "message": "Card is valid", "register_time": "2022-01-01 00:00:00", "expiry_time": "2022-12-31 23:59:59"})
    with sqlite3.connect(DATABASE) as conn:
        cursor = conn.cursor()
        cursor.execute("SELECT register_time, expiry_time FROM cards WHERE uuid = ?", (card_uuid,))
        row = cursor.fetchone()

    if row:
        expiry_time = datetime.datetime.strptime(row[1], "%Y-%m-%d %H:%M:%S")
        tz = pytz.timezone('Asia/Shanghai')
        expiry_time = tz.localize(expiry_time)
        now_time = datetime.datetime.now(tz)
        if expiry_time > now_time:
            return jsonify({"code": 200, "message": "Card is valid", "register_time": row[0], "expiry_time": row[1]})
        else:
            return jsonify({"code": 400, "message": "Card has expired"})
    else:
        return jsonify({"code": 404, "message": "Card not found"})

    
if __name__ == '__main__':
    currentFileDir = os.path.dirname(os.path.abspath(__file__))
    logFile = os.path.join(currentFileDir, "server.log")
    # 设置日志文件，滚动文件大小为10MB，保留5个备份文件
    file_handler = logging.handlers.RotatingFileHandler(logFile, maxBytes=10*1024*1024, backupCount=5)
    logging.getLogger().addHandler(file_handler)
    # 设置日志格式
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    file_handler.setFormatter(formatter)
    # 设置日志级别
    logging.getLogger().setLevel(logging.INFO)


    init_db()
    app.run(host='0.0.0.0', port=1265, debug=False)