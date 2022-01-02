from paho.mqtt.client import Client
import sys, json

def getVectorMultiplication(A, B):
  C = []
  for i in A:
    sum = 0
    for j in B:
      sum = sum + (i*j)
    C.append(sum)
  return C

def getWordCount(sentence):
  return(len(sentence.split()))

def getPrimeNumbers(start, end):
  l = []
  j=0
  flag=0
  while(start <= end):
    flag = 0
    for j in range(2, int(start/2)+1):
      if(start%j == 0):
        flag = 1
        break
    if(flag == 0):
      l.append(start)
    start= start+1
  return l



def on_connect(client, userdata, flags, rc):  
    print("Connected with result code {0}".format(str(rc))) 
    client.subscribe(topic = "leader/task/prime_num")
    client.subscribe(topic = "leader/task/word_count")
    client.subscribe(topic = "leader/task/vect_mult")
    client.publish(topic = clients_status_topic, payload = "1", retain = True)
    mydict = {"prime_num":dev_cap_prime_num, "word_count":dev_cap_word_count, "vect_mult":dev_cap_vect_mult}
    client.publish(topic = clients_capability_topic, payload = str(json.dumps(mydict)), retain = True)

def on_message(client, userdata, message):
    print(message.payload.decode())
    print(message.topic)
    if(message.topic == "leader/task/prime_num"):
      out = []
      y = json.loads(message.payload.decode())
      for i in y["dev"]:
        if i["id"] == dev_id:
          for j in range(dev_cap_prime_num if i["num_sec"]>dev_cap_prime_num else i["num_sec"]):
            out = getPrimeNumbers(2, y["param"])

          #Convert each integer to a string
          string_ints = [str(int) for int in out]
          #Combine each string with a comma
          str_of_ints = " ".join(string_ints)
          mydict = {"task": "prime_num", "res": str_of_ints}
          client.publish(topic = "task/result", payload = str(json.dumps(mydict)))


    if(message.topic == "leader/task/word_count"):
      out = 0
      y = json.loads(message.payload.decode())
      for i in y["dev"]:
        if i["id"] == dev_id:
          for j in range(dev_cap_prime_num if i["num_sec"]>dev_cap_prime_num else i["num_sec"]):
            out = getWordCount(y["param"])
          mydict = {"task": "word_count", "res": str(out)}
          client.publish(topic = "task/result", payload = str(json.dumps(mydict)))


    if(message.topic == "leader/task/vect_mult"):
      y = json.loads(message.payload.decode())
      A = y["A"].split()
      A = map(int, A)
      A = list(A)
      
      B = y["B"].split()
      B = map(int, B)
      B = list(B)

      for i in y["dev"]:
        if i["id"] == dev_id:
          for j in range(dev_cap_prime_num if i["num_sec"]>dev_cap_prime_num else i["num_sec"]):
            C = getVectorMultiplication(A,B)
          #Convert each integer to a string
          string_ints = [str(int) for int in C]
          #Combine each string with a comma
          str_of_ints = " ".join(string_ints)
          mydict = {"task": "vect_mult", "res": str_of_ints}
          client.publish(topic = "task/result", payload = str(json.dumps(mydict)))





dev_id = sys.argv[1]
dev_cap_prime_num = int(sys.argv[2])
dev_cap_word_count = int(sys.argv[3])
dev_cap_vect_mult = int(sys.argv[4])

clients_status_topic = "clients/"+dev_id+"/status"
clients_capability_topic = "clients/"+dev_id+"/capability"

client = Client(client_id = dev_id)
client.will_set(clients_status_topic, payload="0", retain=True)

client.on_message = on_message
client.on_connect = on_connect

client.connect("localhost")

client.loop_forever()