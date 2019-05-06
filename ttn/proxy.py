#!/usr/bin/env python3
import logging
import os
import time
import json

import base64
import requests
import ttn

from datetime import datetime


app_id = "ttn-app.example"
app_key = "ttn-account-v2.example"
datastreams = {}

today = "{0:%Y%m%d}".format(datetime.now())
f_secrets = "ttn.secrets"
f_tracker = "datastreams.json"
f_logging = today + "_proxy.log"
scale = 10.0;

def uplink_callback(msg, client):
    print("Uplink message")
    print("  FROM: ", msg.dev_id)
    print("  TIME: ", msg.metadata.time)
    print("   RAW: ", msg.payload_raw)
    with open(f_logging, 'a') as fl:
        print(str(msg), file = fl)
    buf = base64.decodebytes(msg.payload_raw.encode('ascii'))
    # parse values from buffer
    humidity    = buf[0]
    temperature = ((0x0f & buf[2]) << 8)  |  (0xff & buf[1])
    windspeed   = ((0xff & buf[3]) << 4)  | ((0xf0 & buf[2]) >> 4)
    devid       = ((0xff & buf[7]) << 24) | ((0xff & buf[6]) << 16) | ((0xff & buf[5]) << 8) | (0xff & buf[4] << 0)
    # offset +500 and scale temperature
    temperature = (temperature - 500) / scale
    # convert to m/s and scale windspeed
    windspeed   = (windspeed * 3.600) / scale

    print("  DATA: ({}, {}, {}, {})".format(devid, humidity, temperature, windspeed))

    if msg.dev_id in datastreams:
        data = {}
        data['temperature'] = { 'result': temperature }
        data['humidity']    = { 'result': humidity }
        data['windspeed']   = { 'result': windspeed }
        for sensor in datastreams[msg.dev_id]:
            url = datastreams[msg.dev_id][sensor]
            print("  POST {} to {}".format(json.dumps(data[sensor]), url))
            r = requests.post(url, json=data[sensor])
            print("  POST status: %d" % r.status_code)
    else:
        print("  invalid device: ", msg.dev_id)


def main():
    if os.path.isfile(f_secrets):
        with open(f_secrets, "r") as f:
            secrets = json.loads(f.read())
            app_id = secrets['app_id']
            app_key = secrets['app_key']
    else:
        print("cannot find file with APP ID and KEY")
        exit(1)

    print("APP ID: ", app_id)
    print("APP KEY:", app_key)

    if os.path.isfile(f_tracker):
        with open(f_tracker, "r") as f:
            global datastreams
            datastreams = json.loads(f.read())

    print("URLS: ", json.dumps(datastreams, indent=2))
    with open(f_logging, 'a') as fl:
        print(json.dumps(datastreams), file = fl)

    ttncli = ttn.HandlerClient(app_id, app_key)

    mqttcli = ttncli.data()
    mqttcli.set_uplink_callback(uplink_callback)
    mqttcli.connect()

    while 1:
        time.sleep(10)


if __name__ == "__main__":
    main()
