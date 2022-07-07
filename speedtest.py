import os
import re
import subprocess
import time
from influxdb import InfluxDBClient
import random
import sys
from Adafruit_IO import MQTTClient

ADAFRUIT_IO_USERNAME = '******'
ADAFRUIT_IO_KEY = '********'

response = subprocess.Popen('/usr/bin/speedtest --accept-license --accept-gdpr', shell=True, stdout=subprocess.PIPE).stdout.read().decode('utf-8')

ping = re.search('Latency:\s+(.*?)\s', response, re.MULTILINE)
download = re.search('Download:\s+(.*?)\s', response, re.MULTILINE)
upload = re.search('Upload:\s+(.*?)\s', response, re.MULTILINE)
jitter = re.search('\((.*?)\s.+jitter\)\s', response, re.MULTILINE)

ping = ping.group(1)
download = download.group(1)
upload = upload.group(1)
jitter = jitter.group(1)

speed_data = [
    {
        "measurement" : "internet_speed",
        "tags" : {
            "host": "RaspberryPi"
        },
        "fields" : {
            "download": float(download),
            "upload": float(upload),
            "ping": float(ping),
            "jitter": float(jitter)
        }
    }
]



file1 = open("/home/ankit/speedtest/results2.csv", "a+")  # append mode

if os.stat("/home/ankit/speedtest/results2.csv").st_size == 0:
    file1.write('Date,Time,Ping (ms),Jitter (ms),Download (Mbps),Upload (Mbps)\r\n')

file1.write('{},{},{},{},{},{}\r\n'.format(time.strftime('%m/%d/%y'), time.strftime('%H:%M'), ping, jitter, download, upload))
file1.close()


client = InfluxDBClient('localhost', 8086, 'speedmonitor', 'pimylifeup', 'internetspeed')
client.write_points(speed_data)


#client = InfluxDBClient('localhost', 8086, 'speedmonitor', 'pimylifeup', 'internetspeed')
#client.write_points(speed_data)




def connected(client):
    client.subscribe('speedtest.upload_rate_feed')

def disconnected(client):
    print('Disconnected from Adafruit IO!')
    sys.exit(1)

def message(client, feed_id, payload):
    pass

client = MQTTClient(ADAFRUIT_IO_USERNAME, ADAFRUIT_IO_KEY)


client.on_connect    = connected
client.on_disconnect = disconnected
client.on_message    = message


client.connect()
client.loop_background()


client.publish('speedtest.upload_rate_feed', upload)
client.publish('speedtest.download_rate_feed', download)
client.publish('speedtest.ping_feed', ping)
client.publish('speedtest.jitter_feed', jitter)
print ("Message sent, exiting now!")




