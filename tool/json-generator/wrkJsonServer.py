#!/usr/bin/python3
from flup.server.fcgi import WSGIServer
from threading import Thread
from cgi import parse_qs
import logging.handlers
import logging
import threading
import platform
import patoolib
import json
import time
import sys
import os
import shutil
import hashlib
from jsonGenerator import jsonGenerator

g_data_dir = "data/"

def remove_expire_dir():
    files = os.listdir(g_data_dir)
    file_num = len(files)
    now = time.time()
    for file in files: 
        file_path = g_data_dir + file
        if (now - os.path.getctime(file_path)) > 60 * 60:
            remove_dir(file_path)
            g_logger.info("remove " + file_path)

def webapp(environ, start_response):
    method = environ['REQUEST_METHOD']
    response = {}
    response['resultCode'] = 0
    request_url = environ['REQUEST_URI']
    result_msg = time.asctime(time.localtime(time.time())) + request_url + " " + method + ":"
    if method == 'PUT' and request_url.startswith("/wrk_json/api/v1/upload", 0):
        file_path = request_url.split("/upload/")[-1]
        if file_path:
            body = environ['wsgi.input'].read()
            file_path = g_data_dir + file_path
            fp = open(file_path, "wb")
            try:
                fp.write(body)
                response['resultCode'] = 1
                result_msg = result_msg + "Upload success"
            except Exception as e:
                result_msg = result_msg + type(e) + str(e)
            fp.close()
        else:
            result_msg = result_msg + "Without file name(" + file_name + ") in url"
    elif method == 'GET':
        remove_expire_dir()
        query = parse_qs(environ['QUERY_STRING'])
        if query and request_url.startswith("/wrk_json/api/v1/upload"):
            try:
                md5_string = query.get('md5')
                new_dir = g_data_dir + md5_string[0]
                if not os.path.exists(new_dir):
                    os.makedirs(new_dir, mode=0o777)
                response['resultCode'] = 1
            except Exception as e:
                result_msg = result_msg + type(e) + str(e)
        elif query and request_url.startswith("/wrk_json/api/v1/convert"):
            md5_string = query.get('md5')
            convert_type = query.get('convert_type')
            src_file_name = query.get('file_name')
            src_path = g_data_dir + md5_string[0]
            if md5_string == None or convert_type == None:
                result_msg = result_msg + "Query string incomplete!"
            elif os.path.exists(src_path) == False:
                result_msg = result_msg + "Convert dir(" + src_path + ") doesn't exist!"
            else:
                creater = jsonGenerator()
                urls_file_path = src_path + "/urls.txt"
                bodies_file_path = src_path + "/bodies.txt"
                json_content = []
                if convert_type[0] == 'convert-get':
                    json_content = creater.jsonByGetFile(urls_file_path)
                elif convert_type[0] == 'convert-post-char':
                    json_content = creater.jsonByPostFile(bodies_file_path, urls_file_path)
                elif convert_type[0] == 'convert-post-bin':
                    rar_path = bodies_file_path
                    try:
                        unpacked_dir = src_path + "/base64/"
                        if os.path.exists(unpacked_dir) == False:
                            os.makedirs(unpacked_dir, mode=0o777)
                            patoolib.extract_archive(rar_path, outdir=unpacked_dir)
                        json_content = creater.jsonByBase64Dir(unpacked_dir, urls_file_path)
                    except Exception as e:
                        result_msg = result_msg + type(e) + str(e)
                else:
                    result_msg = result_msg + "Conversion type is incorrent!"

                if json_content != None:
                    if src_file_name == None:
                        dst_file_name = "result.json"
                    else:
                        dst_file_name = os.path.splitext(src_file_name[0])[0] + ".json"
                    dst_path = g_data_dir + md5_string[0] + "/" + dst_file_name
                    json_fp = open(dst_path, "w")
                    try:
                        json_fp.write(json_content)
                        response['convertedFile'] = md5_string[0] + "/" + dst_file_name
                        response['resultCode'] = 1
                        result_msg = result_msg + "Convert success"
                    except Exception as e:
                        result_msg = result_msg + type(e) + str(e)
                    json_fp.close()
                else:
                    result_msg = result_msg + "Conversion failed, please check source file"
        else:
            result_msg = result_msg + "Unknown url(" + request_url + ")"

    g_logger.info(result_msg)
    response["resultMsg"] = result_msg
    start_response('200 OK', [('Content-Type', 'application/json')])
    content = json.dumps(response, sort_keys=True, indent=4, separators=(',', ':'))
    return content

class serverThread(threading.Thread):
    def __init__(self, name, loop, stop):
        threading.Thread.__init__(self)
        self.name = name
        self.loop = loop
        self.stop = stop

    def run(self):
        self.loop()

    def stop(self):
        self.stop()

LOG_FILE = 'jsonGenerator.log'

def remove_dir(top):
    for root, dirs, files in os.walk(top, topdown=False):
        print("dirs:%s, files:%s, root:%s" %(dirs, files, root))
        for name in files:
            os.remove(os.path.join(root, name))
            print(root+name)
        for name in dirs:
            os.rmdir(os.path.join(root, name))
            print(root+name)
    os.rmdir(root)

log_handler = logging.handlers.RotatingFileHandler(LOG_FILE, maxBytes = 1024*1024, backupCount = 5) 
g_logger = logging.getLogger('jsonGenerator')
g_logger.addHandler(log_handler)
g_logger.setLevel(logging.DEBUG)

if __name__ == '__main__':
    remove_expire_dir()
    print(platform.python_version())
    WSGIServer(webapp, bindAddress='/etc/ncserver/wrk-json-generator/.ncserver.sock', umask=0000).run()
