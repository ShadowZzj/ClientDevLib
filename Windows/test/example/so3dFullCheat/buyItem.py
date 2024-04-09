import requests

url = "https://hx.guguseal.com:3334/purchase"
headers = {
    "Accept": "*/*",
    "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
    "Connection": "keep-alive",
    "Content-Type": "application/json",
    "Origin": "https://shop.guguseal.com",
    "Referer": "https://shop.guguseal.com/",
    "Sec-Fetch-Dest": "empty",
    "Sec-Fetch-Mode": "cors",
    "Sec-Fetch-Site": "same-site",
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36",
    "sec-ch-ua": "\"Not/A)Brand\";v=\"8\", \"Chromium\";v=\"126\", \"Google Chrome\";v=\"126\"",
    "sec-ch-ua-mobile": "?0",
    "sec-ch-ua-platform": "\"Windows\""
}

for i in range(1, 13):
    for j in range(1, 10):
        username = f"shenfeng{i}"
        data = {
            "username": username,
            "password": "901121",
            "itemID": 8036,
            "itemCount": "300",
            "itemname": "攤販呼叫券"
        }

        response = requests.post(url, headers=headers, json=data)

        print(f"Response for {username}:")
        print(response.status_code)
        print(response.text)
        print("-" * 50)
