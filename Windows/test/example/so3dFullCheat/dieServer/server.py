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

moneyMap = {}
moneyLock = Lock() 
@app.route('/get_money', methods=['GET'])
def GetMoney():
    global moneyMap
    # 从请求中获取用户标识符
    user_id = request.args.get("userid")
    if user_id not in moneyMap:
        return jsonify({"code": 404, "message": "user not found"})
    
    # 返回用户标识符下的所有金钱
    retStr = ""
    totalMoney = 0
    with moneyLock:
        for username in moneyMap[user_id]:
            for role in moneyMap[user_id][username]:
                totalMoney += moneyMap[user_id][username][role]
                retStr += f"{username}({role}): {moneyMap[user_id][username][role]}\n"
    retStr += f"Total: {totalMoney}"
    return retStr


@app.route('/post_money', methods=['POST'])
def PostMoney():
    global moneyMap
    # 从请求中获取数据
    data = request.get_json()
    # 获取用户标志符
    user_id = data.get("userid")
    # 获取用户名
    username = data.get("username")
    # 获取角色名
    role = data.get("rolename")
    # 获取金钱
    money = data.get("money")

    with moneyLock:
        # 判断用户是否存在
        if user_id not in moneyMap:
            moneyMap[user_id] = {}
        # 判断用户名是否存在
        if username not in moneyMap[user_id]:
            moneyMap[user_id][username] = {}
        # 判断角色名是否存在
        if role not in moneyMap[user_id][username]:
            moneyMap[user_id][username][role] = 0
        # 更新金钱
        moneyMap[user_id][username][role] = money
    # 返回结果
    return jsonify({"code": 200, "message": "success"})
dieInfoMap = {}
dieLock = Lock()
@app.route('/post_dieinfo', methods=['POST'])
def PostDieInfo():
    global dieInfoMap
    # 从请求中获取数据
    data = request.get_json()
    # 获取用户标志符
    user_id = data.get("userid")
    # 获取用户名
    username = data.get("username")
    # 获取角色名
    role = data.get("rolename")
    

    with dieLock:
        # 判断用户是否存在
        if user_id not in dieInfoMap:
            dieInfoMap[user_id] = {}
        # 判断用户名是否存在
        if username not in dieInfoMap[user_id]:
            dieInfoMap[user_id][username] = {}
        # 判断角色名是否存在
        if role not in dieInfoMap[user_id][username]:
            dieInfoMap[user_id][username][role] = ""
        # 更新死亡时间点为当前时间
        tz = pytz.timezone('Asia/Shanghai')
        beijing_time = datetime.datetime.now(tz).strftime("%Y-%m-%d %H:%M:%S")
        dieInfoMap[user_id][username][role] = beijing_time
    return jsonify({"code": 200, "message": "success"})

@app.route('/get_dieinfo', methods=['GET'])
def GetDieInfo():
    global dieInfoMap
    # 从请求中获取用户标识符
    user_id = request.args.get("userid")
    if user_id not in dieInfoMap:
        return jsonify({"code": 404, "message": "user not found"})
    
    # 返回用户标识符下的所有死亡信息
    retStr = ""
    with dieLock:
        for username in dieInfoMap[user_id]:
            for role in dieInfoMap[user_id][username]:
                retStr += f"{username}({role}): {dieInfoMap[user_id][username][role]}\n"
        dieInfoMap[user_id] = {}
    logging.info(f"Die Info: {retStr}")
    return retStr

infoMap = {}
infoLock = Lock()
@app.route('/get_info', methods=['GET'])
def GetInfo():
    global infoMap
    # 从请求中获取用户标识符
    user_id = request.args.get("userid")
    if user_id not in infoMap:
        return jsonify({"code": 404, "message": "user not found"})
    
    # 返回用户标识符下的所有信息
    retStr = ""
    totalMoney = 0
    with infoLock:
        for username in infoMap[user_id]:
            for role in infoMap[user_id][username]:
                retStr += f"{username}({role}): hp={infoMap[user_id][username][role]['hp']}, mp={infoMap[user_id][username][role]['mp']}, money={infoMap[user_id][username][role]['money']}, time={infoMap[user_id][username][role]['time']}, Die={infoMap[user_id][username][role]['hp'] <= 0}\n"
                totalMoney += infoMap[user_id][username][role]['money']
    
    retStr += f"Total Money: {totalMoney}"
    return retStr

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



    app.run(host='0.0.0.0', port=1265, debug=False)