#start executing this script under silo-sto directory, before follower.py
#you may want to change the server and port variable before executing
#input: three integers. The minimal number of cores, the maximal number of cores and the flag
import logging
import os
import socket
import time
import sys

logging.basicConfig(
         format='%(asctime)s %(levelname)-8s %(message)s',
         level=logging.INFO)

server_ip = "0.0.0.0"
port = 23333

minCPU = int(sys.argv[1])
maxCPU = int(sys.argv[2])
flag = int(sys.argv[3])

# madars
#killCommand = "sudo pkill -f dbtest"
killCommand = "sudo pkill -9 dbtest ; sleep 1"

if __name__ == "__main__":
    server = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    server.bind((server_ip,port))
    server.listen(5)
    connection, address = server.accept()

    connection.send("msg2".encode())

    for cores in range(minCPU,maxCPU+1):
        connection.recv(1024) # received msg4
        logging.info("number of cores :" + str(cores))
        os.system(killCommand)
        logging.info("kill dbtest")
        connection.send("msg5".encode())
        connection.recv(1024) # received msg8
        cmd="sudo ./mb2.sh "+str(cores)
        logging.info("start to execute: " + cmd)
        os.system(cmd)
    time.sleep(maxCPU+30+180)
    connection.close()
    server.close()
    os.system(killCommand)

