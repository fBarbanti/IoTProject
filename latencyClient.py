from paho.mqtt.client import Client
import datetime

from threading import Timer
from time import sleep

startTime = datetime.datetime.now()

class RepeatedTimer(object):
    def __init__(self, interval, function, *args, **kwargs):
        self._timer     = None
        self.interval   = interval
        self.function   = function
        self.args       = args
        self.kwargs     = kwargs
        self.is_running = False
        self.start()

    def _run(self):
        self.is_running = False
        self.start()
        self.function(*self.args, **self.kwargs)

    def start(self):
        if not self.is_running:
            self._timer = Timer(self.interval, self._run)
            self._timer.start()
            self.is_running = True

    def stop(self):
        self._timer.cancel()
        self.is_running = False



def on_connect(client, userdata, flags, rc):  
    print("Connected with result code {0}".format(str(rc))) 
    client.subscribe(topic = "dashboard/latency/measure")


def on_message(client, userdata, message):
    endTime = datetime.datetime.now()
    print("StartTime:")
    print(startTime)
    print("EndTime:")
    print(endTime)
    delta = endTime - startTime
    print("Delta:")
    print(delta)
    latency = (delta.total_seconds() * 1000) / 2
    print(message.payload.decode())
    print("Latency")
    print(latency)
    client.publish(topic="clients/latency/value", payload=latency)
    


def publish():
    global startTime 
    startTime = datetime.datetime.now()
    client.publish(topic = "clients/latency/measure", payload = "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been thee", qos=1)


client = Client(client_id = "latency_client")

client.on_message = on_message
client.on_connect = on_connect

client.connect("localhost")

rt = RepeatedTimer(2, publish) 



client.loop_forever()
